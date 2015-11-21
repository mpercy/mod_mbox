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

/* The file contains an alternative Directory Index Handler that
 * displays information about the mailing list.
 */

#include "mod_mbox.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif


/* Compare two file entries (in reverse order).
 *
 * This function is only Used when sorting file list to :
 *  200501
 *  200412
 *  200411
 */
static int filename_rsort(const void *fn1, const void *fn2)
{
    mbox_file_t *f1 = (mbox_file_t *) fn1;
    mbox_file_t *f2 = (mbox_file_t *) fn2;

    return strcmp(f2->filename, f1->filename);
}

/* Fetches the .mbox files from the directory and return a chained
 * list of mbox_files_t containing all the information we need to
 * output a complete box listing.
 */
apr_array_header_t *mbox_fetch_boxes_list(request_rec *r,
                                          mbox_cache_info *mli, char *path)
{
    apr_status_t rv = APR_SUCCESS;
    apr_finfo_t finfo;
    apr_dir_t *dir;
    mbox_file_t *fi;
    apr_array_header_t *files;

    rv = apr_dir_open(&dir, path, r->pool);

    /* If we couldn't open the directory, something like file
       permissions are stopping us. */
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "mod_mbox(fetch_boxes_list): Failed to open directory '%s' for index",
                      path);
        return NULL;
    }

    if (!mli) {
        return NULL;
    }

    /* 15 Years of Mail Archives */
    files = apr_array_make(r->pool, 15 * 12, sizeof(mbox_file_t));

    /* Foreach file in the directory, add its name and the message
       count to our array */
    while (apr_dir_read(&finfo, APR_FINFO_NAME, dir) == APR_SUCCESS) {
        if ((apr_fnmatch("*.mbox", finfo.name, 0) == APR_SUCCESS) &&
            (ap_strstr_c(finfo.name, "incomplete") == NULL)) {
            fi = (mbox_file_t *) apr_array_push(files);
            fi->filename = apr_pstrdup(r->pool, finfo.name);
            mbox_cache_get_count(mli, &(fi->count), (char *) finfo.name);
        }
    }

    apr_dir_close(dir);

    if (files->nelts == 0) {
        return NULL;
    }

    /* Sort by reverse filename order */
    qsort((void *) files->elts, files->nelts, sizeof(mbox_file_t),
          filename_rsort);

    return files;
}


int mbox_atom_handler(request_rec  *r, mbox_cache_info *mli)
{
    int errstatus;
    char dstr[100];
    apr_size_t dlen;
    char *etag;
    apr_time_exp_t extime;

    /* Only allow GETs */
    r->allowed |= (AP_METHOD_BIT << M_GET);
    if (r->method_number != M_GET) {
        return HTTP_METHOD_NOT_ALLOWED;
    }

    ap_set_content_type(r, "application/xml; charset=utf-8");

    /* Try to make the index page more cache friendly */
    ap_update_mtime(r, mli->mtime);
    ap_set_last_modified(r);
    etag = ap_make_etag(r, 1);
    apr_table_setn(r->headers_out, "ETag", etag);

    if (r->header_only) {
        return OK;
    }

    if ((errstatus = ap_meets_conditions(r)) != OK) {
        r->status = errstatus;
        return r->status;
    }

    ap_rputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", r);
    ap_rputs("<feed xmlns=\"http://www.w3.org/2005/Atom\">\n", r);
    ap_rprintf(r, "<title>%s@%s Archives</title>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain));
    ap_rprintf(r, "<link rel=\"self\" href=\"%s?format=atom\"/>\n",
               ap_construct_url(r->pool, r->uri, r));
    ap_rprintf(r, "<link href=\"%s\"/>\n",
               ap_construct_url(r->pool, r->uri, r));
    ap_rprintf(r, "<id>%s</id>\n", ap_construct_url(r->pool, r->uri, r));

    apr_time_exp_gmt(&extime, mli->mtime);

    apr_strftime(dstr, &dlen, sizeof(dstr), "%G-%m-%dT%H:%M:%SZ", &extime);

    ap_rprintf(r, "<updated>%s</updated>\n", dstr);
    mbox_atom_entries(r, mli);
    ap_rputs("</feed>\n", r);
    return OK;
}

/* The default index handler, using mbox_display_static_index() */
int mbox_index_handler(request_rec *r)
{
    int errstatus;
    apr_status_t rv = APR_SUCCESS;

    mbox_dir_cfg_t *conf;
    mbox_cache_info *mli;

    char dstr[APR_RFC822_DATE_LEN];
    char *etag;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);

    /* Don't serve index if it's not a directory or if it's not
       enabled */
    if (strcmp(r->handler, DIR_MAGIC_TYPE) || !conf->enabled) {
        return DECLINED;
    }

    /* Open mbox cache */
    rv = mbox_cache_get(&mli, r->filename, r->pool);
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, rv, r,
                      "mod_mbox: Can't open directory cache '%s' for index",
                      r->filename);
        return DECLINED;
    }

    if (r->args && strstr(r->args, "format=atom") != NULL) {
        return mbox_atom_handler(r, mli);
    }

    if (r->args && strstr(r->args, "format=sitemap") != NULL) {
        return mbox_sitemap_handler(r, mli);
    }
    
    /* Only allow GETs */
    r->allowed |= (AP_METHOD_BIT << M_GET);
    if (r->method_number != M_GET) {
        return HTTP_METHOD_NOT_ALLOWED;
    }

    ap_set_content_type(r, "text/html; charset=utf-8");

    /* Try to make the index page more cache friendly */
    ap_update_mtime(r, mli->mtime);
    ap_set_last_modified(r);
    etag = ap_make_etag(r, 1);
    apr_table_setn(r->headers_out, "ETag", etag);

    if (r->header_only) {
        return OK;
    }

    if ((errstatus = ap_meets_conditions(r)) != OK) {
        r->status = errstatus;
        return r->status;
    }

    ap_rputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", r);
    ap_rputs("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n",
             r);
    ap_rputs("\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n\n", r);

    ap_rputs("<html xmlns=\"http://www.w3.org/1999/xhtml\">\n", r);
    ap_rputs(" <head>\n", r);
    ap_rputs
        ("  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n",
         r);
    if (mli->list && mli->domain) {
        ap_rprintf(r, "  <title>%s@%s Archives</title>\n",
                   ESCAPE_OR_BLANK(r->pool, mli->list),
                   ESCAPE_OR_BLANK(r->pool, mli->domain));
    }
    else {
        ap_rputs("  <title>Mailing list archives</title>\n", r);
    }

    ap_rprintf(r, "<link rel=\"alternate\" title=\"%s@%s Archives\" "
               "type=\"application/atom+xml\" href=\"%s?format=atom\" />",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain),
               ap_construct_url(r->pool, r->uri, r));

    if (conf->style_path) {
        ap_rprintf(r,
                   "   <link rel=\"stylesheet\" type=\"text/css\" href=\"%s\" />\n",
                   conf->style_path);
    }

    if (conf->script_path) {
        ap_rprintf(r,
                   "   <script type=\"text/javascript\" src=\"%s\"></script>\n",
                   conf->script_path);
    }

    ap_rputs(" </head>\n\n", r);

    ap_rputs(" <body id=\"archives\" onload=\"indexLinks ();\">\n", r);
    ap_rprintf(r, "  <h1>Mailing list archives: %s@%s</h1>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain));

    if (conf->root_path) {
        ap_rprintf(r,
                   "  <h5><a href=\"%s\" title=\"Back to the archives depot\">"
                   "Site index</a></h5>\n\n", conf->root_path);
    }

    /* Output header and list information */
    ap_rputs("  <table id=\"listinfo\">\n", r);
    ap_rputs
        ("   <thead><tr><th colspan=\"2\">List information</th></tr></thead>\n",
         r);
    ap_rputs("   <tbody>\n", r);

    ap_rprintf(r, "    <tr><td class=\"left\">Writing to the list</td>"
               "<td class=\"right\">%s@%s</td></tr>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain));

    ap_rprintf(r, "    <tr><td class=\"left\">Subscription address</td>"
               "<td class=\"right\">%s-subscribe@%s</td></tr>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain));

    ap_rprintf(r,
               "    <tr><td class=\"left\">Digest subscription address</td>"
               "<td class=\"right\">%s-digest-subscribe@%s</td></tr>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list), ESCAPE_OR_BLANK(r->pool,
                                                                    mli->
                                                                    domain));

    ap_rprintf(r, "    <tr><td class=\"left\">Unsubscription addresses</td>"
               "<td class=\"right\">%s-unsubscribe@%s</td></tr>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain));

    ap_rprintf(r, "    <tr><td class=\"left\">Getting help with the list</td>"
               "<td class=\"right\">%s-help@%s</td></tr>\n",
               ESCAPE_OR_BLANK(r->pool, mli->list),
               ESCAPE_OR_BLANK(r->pool, mli->domain));

    ap_rputs("<tr><td class=\"left\">Feeds:</td>"
             "<td class=\"right\">"
             "<a href=\"?format=atom\">Atom 1.0</a></td></tr>\n", r);

    ap_rputs("   </tbody>\n", r);
    ap_rputs("  </table>\n", r);

    /* Display the box list */
    rv = mbox_static_index_boxlist(r, conf, mli);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    apr_rfc822_date(dstr, mli->mtime);
    ap_rprintf(r, "<p id=\"lastupdated\">Last updated on: %s</p>\n", dstr);

    ap_rputs("  </body>\n", r);
    ap_rputs("</html>", r);

    return OK;
}
