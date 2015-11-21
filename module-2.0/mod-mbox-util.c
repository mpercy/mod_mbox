/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define CORE_PRIVATE
#include "httpd.h"
#include "apr_pools.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "mbox_cache.h"
#include "mbox_parse.h"
#include "apr_getopt.h"
#include "apr_date.h"
#include "apr_lib.h"
#include "apr_fnmatch.h"

#define NL APR_EOL_STR

static char errbuf[128];

static int update_mode;
static int verbose;
static const char *upath;
static const char *shortname;
static apr_file_t *errfile;

static void usage(void)
{
    apr_file_printf(errfile,
                    "%s -- Program to Create and Update mod_mbox cache files"
                    NL "Usage: %s [-v] -u MBOX_PATH" NL
                    "       %s [-v] -c MBOX_PATH" NL
                    "       %s [-v] -m MBOX_FILE" NL NL "Options: " NL
                    " -v    More verbose output" NL NL
                    " -u    Updates an existing cache. If this cache does not exist it will"
                    NL
                    "       be created.  If it contains an older cache format, it will be "
                    NL "       upgraded. " NL NL
                    " -c    Force creation of a new cache. If there is an existing cache, it"
                    NL "       will be ignored and overwritten " NL NL
                    " -m    Dumps the Message-ID cache to stdout for the specified file."
                    NL NL " -s    Set the path to store a Full Text Index." NL
                    NL, shortname, shortname, shortname, shortname);
}

static int file_alphasort(const void *fn1, const void *fn2)
{
    /* Reverse Order */
    return strcmp(*(char **) fn2, *(char **) fn1);
}

static int process_mbox(request_rec *r, mbox_cache_info *mli, char *path,
                        const char *list, const char *domain)
{
    apr_status_t rv;
    apr_file_t *f;
    int count;
    char *temp;
    char *absfile = apr_pstrcat(r->pool, r->filename, path, NULL);

    if (update_mode) {
        /* check the last update time */
        apr_finfo_t finfo;
        apr_stat(&finfo, absfile, APR_FINFO_MTIME, r->pool);

        if (finfo.mtime < mli->mtime) {
            if (verbose) {
                apr_file_printf(errfile, "\tNot Modified, Skipping." NL);
            }
            return 0;
        }
    }

    rv = apr_file_open(&f, absfile, APR_READ, APR_OS_DEFAULT, r->pool);

    if (rv != APR_SUCCESS) {
        apr_file_printf(errfile, "Error: Cannot open '%s': %s" NL,
                        absfile, apr_strerror(rv, errbuf, sizeof(errbuf)));
        return EXIT_FAILURE;
    }

    temp = r->filename;
    r->filename = absfile;
    rv = mbox_generate_index(r, f, list, domain);
    r->filename = temp;

    if (rv != APR_SUCCESS) {
        apr_file_printf(errfile,
                        "Error: Index Generation for '%s' failed: %s" NL,
                        absfile, apr_strerror(rv, errbuf, sizeof(errbuf)));
        return 0;
    }

    count = mbox_msg_count(r, path);
    if (verbose) {
        apr_file_printf(errfile, "\tscanned %d messages" NL, count);
    }
    mbox_cache_set_count(mli, count, path);
    return 0;
}

static int scan_dir(request_rec *r)
{
    apr_status_t rv;
    apr_dir_t *dir;
    apr_finfo_t finfo;
    apr_array_header_t *files;
    char *file = NULL;
    char *ml;
    char *domain;
    char *list;
    mbox_cache_info *mli;
    apr_pool_t *rpool;
    int i;
    apr_pool_t *mpool;
    apr_pool_t *bpool;
    apr_time_t newtime;
    char date[APR_RFC822_DATE_LEN];
    apr_pool_create(&mpool, r->pool);

    bpool = r->pool;
    r->pool = mpool;

    rv = apr_dir_open(&dir, r->filename, mpool);

    if (rv != APR_SUCCESS) {
        apr_file_printf(errfile, "Error: Directory Open Failed: %s" NL,
                        apr_strerror(rv, errbuf, sizeof(errbuf)));
        return EXIT_FAILURE;
    }

    files = apr_array_make(mpool, 30, sizeof(char *));

    while (apr_dir_read(&finfo, APR_FINFO_NAME, dir) == APR_SUCCESS) {
        if (apr_fnmatch("*.mbox", finfo.name, 0) == APR_SUCCESS &&
            ap_strstr_c(finfo.name, "incomplete") == NULL) {
            *(const char **) apr_array_push(files) =
                apr_pstrdup(mpool, finfo.name);
        }
    }

    apr_dir_close(dir);

    if (files->nelts == 0) {
        apr_file_printf(errfile, "Error: No mbox Files found in '%s'" NL,
                        r->filename);
        return EXIT_FAILURE;
    }

    qsort((void *) files->elts, files->nelts, sizeof(char *), file_alphasort);

    if (verbose) {
        apr_file_printf(errfile, "Found %d mbox files to process" NL,
                        files->nelts);
    }

    /* Look for first non-empty file. */
    for (i = 0; i < files->nelts; i++) {
        file = ((char **) files->elts)[i];
        rv = apr_stat(&finfo, file, APR_FINFO_SIZE, mpool);
        if (rv == APR_SUCCESS && finfo.size > 0) {
            break;
        }
    }

    if (verbose) {
        apr_file_printf(errfile, "Scaning %s for Mailing List info" NL, file);
    }

    ml = mbox_get_list_post(r, file);

    if (!ml) {
        apr_file_printf(errfile, "Error: Reading List-Post header "
                        "from '%s' failed" NL, file);
        return EXIT_FAILURE;
    }

    domain = strchr(ml, '@');
    if (!domain) {
        /* Domains are not mandatory */
        domain = "";
    }
    else {
        *domain++ = '\0';
    }

    list = strchr(ml, ':');
    if (!list) {
        apr_file_printf(errfile, "Error: Failed to parse list name "
                        "from '%s'" NL, ml);
        return EXIT_FAILURE;
    }
    *list++ = '\0';

    ml = strrchr(domain, '>');

    if (ml) {
        *ml = '\0';
    }

    if (verbose) {
        apr_file_printf(errfile, "Building Cache for %s@%s" NL, list, domain);
    }

    rv = mbox_cache_update(&mli, r->filename, mpool, list, domain);

    if (rv != APR_SUCCESS) {
        apr_file_printf(errfile, "Error: Cache Create Failed: (%d) %s" NL, rv,
                        apr_strerror(rv, errbuf, sizeof(errbuf)));
        return EXIT_FAILURE;
    }

    newtime = apr_time_now();

    if (verbose) {
        apr_rfc822_date(date, mli->mtime);
        apr_file_printf(errfile, "Last Update: %s" NL, date);
        apr_rfc822_date(date, newtime);
        apr_file_printf(errfile, "Current Time: %s" NL, date);
    }

    /* Iterate the .mbox files */
    apr_pool_create(&rpool, mpool);
    r->pool = rpool;
    for (i = 0; i < files->nelts; i++) {
        file = ((char **) files->elts)[i];
        if (verbose) {
            apr_file_printf(errfile, "Processing '%s'" NL, file);
        }
        rv = process_mbox(r, mli, file, list, domain);
        apr_pool_clear(rpool);
        if (rv) {
            break;
        }
    }

    mli->mtime = newtime;
    rv = mbox_cache_touch(mli);

    if (rv != APR_SUCCESS) {
        apr_file_printf(errfile, "Error: Updating Modified Time: %s" NL,
                        apr_strerror(rv, errbuf, sizeof(errbuf)));
        return EXIT_FAILURE;
    }

    mbox_cache_close(mli);
    apr_pool_clear(mpool);
    r->pool = bpool;
    return rv;
}

static int load_msgid(request_rec *r)
{
    apr_status_t rv;
    apr_file_t *f;
    MBOX_LIST *l;

    rv = apr_file_open(&f, r->filename, APR_READ, APR_OS_DEFAULT, r->pool);
    if (rv != APR_SUCCESS)
        return rv;

    l = mbox_load_index(r, f, NULL);

    while (l) {
        Message *m;
        m = (Message *) l->value;
        printf("%s\n", m->msgID);
        l = l->next;
    }

    return APR_SUCCESS;
}

int main(int argc, const char *const argv[])
{
    apr_status_t rv = APR_SUCCESS;
    request_rec r;
    server_rec s;
    apr_getopt_t *opt;
    const char *optarg;
    char ch;

    apr_initialize();
    atexit(apr_terminate);

    update_mode = -1;
    verbose = 0;
    upath = NULL;

    r.server = &s;
    s.limit_req_fieldsize = DEFAULT_LIMIT_REQUEST_FIELDSIZE;
    s.limit_req_fields = DEFAULT_LIMIT_REQUEST_FIELDS;

    apr_pool_create(&r.pool, NULL);

    if (argc) {
        shortname = apr_filepath_name_get(argv[0]);
    }
    else {
        shortname = "mod-mbox-util";
    }

    apr_file_open_stderr(&errfile, r.pool);
    rv = apr_getopt_init(&opt, r.pool, argc, argv);

    if (rv != APR_SUCCESS) {
        apr_file_printf(errfile, "Error: apr_getopt_init failed." NL NL);
        return EXIT_FAILURE;
    }

    if (argc <= 1) {
        usage();
        return EXIT_FAILURE;
    }

    while ((rv =
            apr_getopt(opt, "vc::u::s::m::", &ch, &optarg)) == APR_SUCCESS) {
        switch (ch) {
        case 'v':
            if (verbose) {
                apr_file_printf(errfile,
                                "Error: -v can only be passed once" NL NL);
                usage();
                return EXIT_FAILURE;
            }
            verbose = 1;
            break;
        case 'c':
            if (update_mode != -1) {
                apr_file_printf(errfile,
                                "Error: -u and -c are exclusive" NL NL);
                usage();
                return EXIT_FAILURE;
            }
            update_mode = 0;
            upath = apr_pstrdup(r.pool, optarg);
            break;
        case 'u':
            if (update_mode != -1) {
                apr_file_printf(errfile,
                                "Error: -u and -c are exclusive" NL NL);
                usage();
                return EXIT_FAILURE;
            }
            update_mode = 1;
            upath = apr_pstrdup(r.pool, optarg);
            break;
        case 'm':
            if (update_mode != -1) {
                apr_file_printf(errfile,
                                "Error: -m can't be used with -u and -c " NL
                                NL);
                usage();
                return EXIT_FAILURE;
            }
            update_mode = 2;
            upath = apr_pstrdup(r.pool, optarg);
            break;
        }
    }

    if (rv != APR_EOF) {
        apr_file_printf(errfile, "Error: Parsing Arguments Failed" NL NL);
        usage();
        return EXIT_FAILURE;
    }

    if (update_mode == -1) {
        apr_file_printf(errfile, "Error: -u, -c, or -m must be passed" NL NL);
        usage();
        return EXIT_FAILURE;
    }

    if (upath == NULL) {
        apr_file_printf(errfile, "Error: no path specified" NL NL);
        usage();
        return EXIT_FAILURE;
    }

    if (update_mode != 2) {
        rv = apr_filepath_merge(&r.filename, NULL, upath,
                                APR_FILEPATH_TRUENAME, r.pool);
        if (rv != APR_SUCCESS) {
            apr_file_printf(errfile, "Error: Unable to resolve path: %s" NL,
                            apr_strerror(rv, errbuf, sizeof(errbuf)));
            return EXIT_FAILURE;
        }

        rv = apr_filepath_set(r.filename, r.pool);

        if (rv != APR_SUCCESS) {
            apr_file_printf(errfile, "Error: Unable to resolve path: %s" NL,
                            apr_strerror(rv, errbuf, sizeof(errbuf)));
            return EXIT_FAILURE;
        }

        if (verbose) {
            apr_file_printf(errfile, "Base Path: %s" NL, r.filename);
        }

        if (r.filename[strlen(r.filename) - 1] != '/') {
            r.filename = apr_pstrcat(r.pool, r.filename, "/", NULL);
        }

        rv = scan_dir(&r);
    }
    else {
        r.filename = (char *) upath;

        rv = load_msgid(&r);
    }

    apr_pool_clear(r.pool);
    return rv;
}
