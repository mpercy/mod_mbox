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

/* This is a module which will present an mbox file stored locally to the
 * world in a nice format.
 *
 * This Apache-2.x module is dependent upon mbox_parse.c and mbox_thread.c.
 * Most of the logic for storing and retrieving data about the mbox is
 * located in mbox_parse.c.  The logic for determining threading information
 * is located in mbox_thread.c.
 *
 * All presentation logic for the mbox is stored here in the module.
 *
 * By placing the appropriate AddHandler configuration in httpd.conf
 * (mbox-handler), you can then access the list of messages of any mbox
 * file by accessing it like the following:
 * http://www.example.com/foo/bar.mbox/
 *
 * Direct link to raw messages are also available:
 * http://www.example.com/foo/bar.mbox/raw?%3c12345@example.com%3e
 *
 * Please note that the actual indexing of the messages does not occur
 * here in the module, but in the "generate_index" standalone program.
 */

#include "mod_mbox.h"

#include "apr_lib.h"
#include "apr_file_io.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

typedef struct {
    const char *base_uri;
    const char *base_path;
    const char *base_name;
} mbox_req_cfg_t;

/* Register module hooks.
 */
static void mbox_register_hooks(apr_pool_t *p)
{
    ap_hook_handler(mbox_file_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(mbox_index_handler, NULL, NULL, APR_HOOK_FIRST);
}

/* Module configuration management.
 */
static void *mbox_create_dir_config(apr_pool_t *p, char *x)
{
    mbox_dir_cfg_t *conf;

    conf = apr_pcalloc(p, sizeof(mbox_dir_cfg_t));

    conf->enabled = 0;
    conf->hide_empty = 1;
    conf->antispam = 1;
    conf->root_path = NULL;
    conf->style_path = NULL;
    conf->script_path = NULL;
    conf->header_include_file = NULL;
    conf->footer_include_file = NULL;

    return conf;
}

#define MBOX_CONFIG_MERGE_BOOL(merge_in, to_in, from_in, fieldname) \
    do { \
        int *_merge = &(merge_in->fieldname); \
        int *_to = &(to_in->fieldname); \
        int *_from = &(from_in->fieldname); \
        if (_merge) { \
            *_to = *_merge; \
        } else { \
            *_to = *_from; \
        } \
    } while (0);

#define MBOX_CONFIG_MERGE_STRING(merge_in, to_in, from_in, fieldname) \
    do { \
        const char **_merge = &(merge_in->fieldname); \
        const char **_to = &(to_in->fieldname); \
        const char **_from = &(from_in->fieldname); \
        if (*_merge) { \
            *_to = apr_pstrdup(p, *_merge); \
        } else { \
            *_to = apr_pstrdup(p, *_from); \
        } \
    } while (0);

/* Merge two configs, return a dynamically allocated copy. */
static void *mbox_merge_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    mbox_dir_cfg_t *from = (mbox_dir_cfg_t *) basev;
    mbox_dir_cfg_t *merge = (mbox_dir_cfg_t *) addv;
    mbox_dir_cfg_t *to;

    to = apr_palloc(p, sizeof(mbox_dir_cfg_t));

    MBOX_CONFIG_MERGE_BOOL(merge, to, from, enabled);
    MBOX_CONFIG_MERGE_BOOL(merge, to, from, hide_empty);
    MBOX_CONFIG_MERGE_BOOL(merge, to, from, antispam);

    MBOX_CONFIG_MERGE_STRING(merge, to, from, root_path);
    MBOX_CONFIG_MERGE_STRING(merge, to, from, style_path);
    MBOX_CONFIG_MERGE_STRING(merge, to, from, script_path);
    MBOX_CONFIG_MERGE_STRING(merge, to, from, header_include_file );
    MBOX_CONFIG_MERGE_STRING(merge, to, from, footer_include_file );

    return to;
}

/* Wrap text to MBOX_WRAP_TO. Changes passed string. */
char *mbox_wrap_text(char *str)
{
    int i, pos;
    apr_size_t len;

    if (!str)
        return NULL;

    len = strlen(str);

    /* Don't wrap messages with a size larger than MBOX_MAX_WRAP or
       smaller than a line length. */
    if ((len < MBOX_WRAP_TO) || (len > MBOX_MAX_WRAP))
        return str;

    for (i = 0, pos = 0; i < len; i++, pos++) {
        /* Reset the position counter if we pass a newline character */
        if (str[i] == '\n') {
            pos = 0;
        }

        /* If the position counter is after the wrap limit, wrap text at
           first space available */
        if ((pos >= MBOX_WRAP_TO) && ((str[i] == ' ') || (str[i] == '\t'))) {
            str[i] = '\n';
            pos = 0;
        }
    }

    return str;
}

/* Escape control chars */
char *mbox_cntrl_escape(apr_pool_t *p, char *s)
{
    int i, j;
    char *x;

    /* first, count the number of extra characters */
    for (i = 0, j = 0; s[i] != '\0'; i++)
        if (apr_iscntrl(s[i]))
            j += 5;

    if (j == 0)
        return s;

    x = apr_palloc(p, i + j + 1);
    for (i = 0, j = 0; s[i] != '\0'; i++, j++) {
        if (apr_iscntrl(s[i])) {
            snprintf(&x[j], 7, "&#%3.3d;", (unsigned char)s[i]);
            j += 5;
        }
        else
            x[j] = s[i];
    }

    x[j] = '\0';
    return x;
}

/* Escape chars in msg ids which are have special meaning in URIs */
char *mbox_msg_id_escape(apr_pool_t *p, char *s)
{
    int i, j;
    char *x;

    /* first, count the number of extra characters */
    for (i = 0, j = 0; s[i] != '\0'; i++)
        if (s[i] == '&')
            /* Length of "%26" minus 1 (original character "&") */
            j += 2;

    if (j == 0)
        return s;

    x = apr_palloc(p, i + j + 1);
    for (i = 0, j = 0; s[i] != '\0'; i++, j++) {
        if (s[i] == '&') {
            strncpy(&x[j], "%26", 3);
            j += 2;
        }
        else
            x[j] = s[i];
    }

    x[j] = '\0';
    return x;
}

static mbox_req_cfg_t *get_req_conf(request_rec *r)
{
    mbox_req_cfg_t *conf = ap_get_module_config(r->request_config, &mbox_module);
    const char *temp;
    if (conf)
        return conf;

    conf = apr_pcalloc(r->pool, sizeof(*conf));

    temp = ap_strstr_c(r->uri, r->path_info);
    if (temp)
        conf->base_uri = apr_pstrmemdup(r->pool, r->uri, temp - r->uri);
    else
        conf->base_uri = r->uri;

    temp = ap_strstr_c(conf->base_uri, ".mbox");
    /* 7 is length of "/yyyymm" */
    if (temp && temp >= conf->base_uri + 7) {
        conf->base_path = apr_pstrmemdup(r->pool, conf->base_uri,
                                         temp - 7 - conf->base_uri);

        conf->base_name = ap_strrchr_c(conf->base_path, '/') + 1;
    }
    ap_set_module_config(r->request_config, &mbox_module, conf);
    return conf;
}

/* Returns the archives base path */
const char *get_base_path(request_rec *r)
{
    mbox_req_cfg_t *conf = get_req_conf(r);
    return conf->base_path;
}

/* Returns the base URI, stripping the path_info */
const char *get_base_uri(request_rec *r)
{
    mbox_req_cfg_t *conf = get_req_conf(r);
    return conf->base_uri;
}

/* Returns the base name, stripping the path */
const char *get_base_name(request_rec *r)
{
    mbox_req_cfg_t *conf = get_req_conf(r);
    return conf->base_name;
}

char *resolve_rel_path(request_rec *r, const char *rel_path) {
    /* TODO: strip ".." components? */
    return apr_pstrcat(r->pool, ap_context_document_root(r), "/", rel_path,
                       NULL);
}

apr_status_t open_for_sendfile(request_rec *r, const char *fname,
                               apr_file_t **file, apr_finfo_t *finfo) {
    apr_finfo_t finfo_tmp;
    RETURN_NOT_SUCCESS(apr_stat(&finfo_tmp, fname, APR_FINFO_SIZE, r->pool));
    RETURN_NOT_SUCCESS(apr_file_open(file, fname,
                                     APR_FOPEN_READ|APR_FOPEN_SENDFILE_ENABLED,
                                     APR_OS_DEFAULT, r->pool));
    *finfo = finfo_tmp;
    return APR_SUCCESS;
}

// sendfile() the given filename to the remote.
// TODO: Add support for caching the fd instead of opening it every time.
apr_status_t mbox_send_include_file(request_rec *r, const char* include_fname) {
    const char *include_fname_abs = resolve_rel_path(r, include_fname);
    apr_file_t* file = NULL;
    apr_finfo_t finfo;
    LOG_RETURN_NOT_SUCCESS(open_for_sendfile(r, include_fname_abs,
                                              &file, &finfo),
                            APLOG_WARNING, r,
                            "open_for_sendfile", include_fname_abs);
    apr_size_t bytes_sent = 0;
    LOG_RETURN_NOT_SUCCESS(ap_send_fd(file, r, 0, finfo.size, &bytes_sent),
                            APLOG_WARNING, r,
                            "ap_send_fd", finfo.name);
    return apr_file_close(file);
}

apr_status_t mbox_send_header_includes(request_rec *r, mbox_dir_cfg_t *conf) {
    if (conf->header_include_file) {
        RETURN_NOT_SUCCESS(mbox_send_include_file(r, conf->header_include_file));
    }

    if (conf->style_path) {
        ap_rprintf(r,
                   "  <link rel=\"stylesheet\" type=\"text/css\" href=\"%s\" />\n",
                   conf->style_path);
    }
    return APR_SUCCESS;
}

apr_status_t mbox_send_footer_includes(request_rec *r, mbox_dir_cfg_t *conf) {
    if (conf->footer_include_file) {
        RETURN_NOT_SUCCESS(mbox_send_include_file(r, conf->footer_include_file));
    }

    if (conf->script_path) {
        ap_rprintf(r,
                   "  <script type=\"text/javascript\" src=\"%s\"></script>\n",
                   conf->script_path);
    }
    return APR_SUCCESS;
}

static const command_rec mbox_cmds[] = {
    AP_INIT_FLAG("mboxindex", ap_set_flag_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, enabled), OR_INDEXES,
                 "Enable mod_mbox to create directory listings of .mbox files."),
    AP_INIT_FLAG("mboxantispam", ap_set_flag_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, antispam), OR_INDEXES,
                 "Enable mod_mbox email obfuscation."),
    AP_INIT_FLAG("mboxhideempty", ap_set_flag_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, hide_empty),
                 OR_INDEXES,
                 "Whether to display empty mboxes in index listing."),
    AP_INIT_TAKE1("mboxrootpath", ap_set_string_slot,
                  (void *) APR_OFFSETOF(mbox_dir_cfg_t, root_path),
                  OR_INDEXES,
                  "Set the path to the site index."),
    AP_INIT_TAKE1("mboxstyle", ap_set_string_slot,
                  (void *) APR_OFFSETOF(mbox_dir_cfg_t, style_path),
                  OR_INDEXES,
                  "Set the path to Css stylesheet file."),
    AP_INIT_TAKE1("mboxscript", ap_set_string_slot,
                  (void *) APR_OFFSETOF(mbox_dir_cfg_t, script_path),
                  OR_INDEXES,
                  "Set the path to the Javascript file."),
    AP_INIT_TAKE1("mboxheaderincludefile", ap_set_string_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, header_include_file),
                 OR_INDEXES,
                 "Path to a file whose contents will be included verbatim in "
                 "the <head> of every HTML document."),
    AP_INIT_TAKE1("mboxfooterincludefile", ap_set_string_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, footer_include_file),
                 OR_INDEXES,
                 "Path to a file that will be included verbatim at the end of "
                 "the <body> of every HTML document."),
    {NULL}
};

module mbox_module = {
    STANDARD20_MODULE_STUFF,
    mbox_create_dir_config,     /* per-directory config creator */
    mbox_merge_dir_config,      /* dir config merger */
    NULL,                       /* server config creator */
    NULL,                       /* server config merger */
    mbox_cmds,                  /* command table */
    mbox_register_hooks         /* set up other request processing hooks */
};
