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

/* This file contains all output functions.
 */

#include "mod_mbox.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

static const char *mbox_months[12][2] = {
    {"Jan", "January"},
    {"Feb", "February"},
    {"Mar", "March"},
    {"Apr", "April"},
    {"May", "May"},
    {"Jun", "June"},
    {"Jul", "July"},
    {"Aug", "August"},
    {"Sep", "September"},
    {"Oct", "October"},
    {"Nov", "November"},
    {"Dec", "December"}
};

/* Display an ATOM feed entry from given message structure */
static void display_atom_entry(request_rec *r, Message *m, const char *mboxfile,
                               apr_pool_t *pool, apr_file_t *f)
{
    char dstr[100];
    apr_size_t dlen;
    apr_time_exp_t extime;
    char *uid;
    char *c;

    ap_rputs("<entry>\n", r);
    ap_rprintf(r, "<title>%s</title>\n", ESCAPE_AND_CONV_HDR(pool, m->subject));
    ap_rprintf(r, "<author><name>%s</name></author>\n",
               ESCAPE_AND_CONV_HDR(pool, m->from));

    ap_rprintf(r, "<link rel=\"alternate\" href=\"%s%s/%s\"/>\n",
               ap_construct_url(r->pool, r->uri, r),
               mboxfile, MSG_ID_ESCAPE_OR_BLANK(pool, m->msgID));

    uid = MSG_ID_ESCAPE_OR_BLANK(pool, m->msgID);

    c = uid;
    while (*c != '\0') {
        if (*c == '.') {
            *c = '-';
        }
        c++;
    }

    ap_rprintf(r, "<id>urn:uuid:%s</id>\n", uid);

    apr_time_exp_gmt(&extime, m->date);

    apr_strftime(dstr, &dlen, sizeof(dstr), "%G-%m-%dT%H:%M:%SZ", &extime);

    ap_rprintf(r, "<updated>%s</updated>\n", dstr);
    ap_rputs("<content type=\"xhtml\">\n"
             "<div xmlns=\"http://www.w3.org/1999/xhtml\">\n" "<pre>\n", r);

    load_message(pool, f, m);
    /* Parse multipart information */
    m->mime_msg = mbox_mime_decode_multipart(r, pool, m->raw_body,
                                             m->content_type,
                                             m->charset,
                                             m->cte, m->boundary);

    ap_rprintf(r, "%s",
               mbox_cntrl_escape(pool, mbox_wrap_text(mbox_mime_get_body(r, pool, m->mime_msg))));

    ap_rputs("\n</pre>\n</div>\n</content>\n", r);
    ap_rputs("</entry>\n", r);
}

static int mbox2atom(request_rec *r, const char *mboxfile,
                     mbox_cache_info *mli, int max)
{
    apr_status_t rv;
    char *filename;
    char *origfilename;
    apr_file_t *f;
    MBOX_LIST *head;
    Message *m;
    int i;
    apr_pool_t *tpool;

    apr_pool_create(&tpool, r->pool);

    filename = apr_pstrcat(r->pool, r->filename, mboxfile, NULL);

    rv = apr_file_open(&f, filename, APR_READ, APR_OS_DEFAULT, r->pool);

    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "mod_mbox(mbox2atom): Can't open mbox '%s' for atom feed",
                      filename);
        return 0;
    }
    origfilename = r->filename;

    r->filename = filename;

    head = mbox_load_index(r, f, NULL);

    /* Sort the list */
    head = mbox_sort_list(head, MBOX_SORT_REVERSE_DATE);

    for (i = 0; i < max && head != NULL; i++) {
        m = (Message *) head->value;
        display_atom_entry(r, m, mboxfile, tpool, f);
        head = head->next;
        apr_pool_clear(tpool);
    }

    r->filename = origfilename;
    apr_pool_destroy(tpool);
    return i;
}

void mbox_atom_entries(request_rec *r, mbox_cache_info *mli)
{
    mbox_file_t *fi;
    apr_array_header_t *files;
    int i, entries = 0;

    files = mbox_fetch_boxes_list(r, mli, r->filename);
    if (!files) {
        return;
    }

    fi = (mbox_file_t *) files->elts;
    for (i = 0; i < files->nelts && entries < MBOX_ATOM_NUM_ENTRIES; i++) {
        if (!fi[i].count) {
            continue;
        }
        entries +=
            mbox2atom(r, fi[i].filename, mli,
                      MBOX_ATOM_NUM_ENTRIES - entries);
    }
}


/* Outputs an XML list of available mailboxes */
apr_status_t mbox_xml_boxlist(request_rec *r)
{
    apr_status_t rv = APR_SUCCESS;

    mbox_dir_cfg_t *conf;
    mbox_cache_info *mli;
    mbox_file_t *fi;
    apr_array_header_t *files;
    int i;
    char *path, *k;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);

    path = apr_pstrdup(r->pool, r->filename);

    k = strstr(path, ".mbox");
    if (!k) {
        return HTTP_NOT_FOUND;
    }

    k = k - 6;
    *k = 0;

    /* Open mbox cache */
    rv = mbox_cache_get(&mli, path, r->pool);
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "mod_mbox(xml_boxlist): Can't open directory cache '%s' for index",
                      r->filename);
        return HTTP_FORBIDDEN;
    }

    files = mbox_fetch_boxes_list(r, mli, path);
    if (!files) {
        return HTTP_FORBIDDEN;
    }

    ap_rputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", r);
    ap_rputs("<boxlist>\n", r);

    fi = (mbox_file_t *) files->elts;

    for (i = 0; i < files->nelts; i++) {
        if (fi[i].count || !conf->hide_empty) {
            ap_rprintf(r, "<mbox id=\"%s\" count=\"%d\" />\n",
                       fi[i].filename, fi[i].count);
        }
    }

    ap_rputs("</boxlist>\n", r);

    return APR_SUCCESS;
}

/* Outputs a statix XHTML list of available mailboxes */
apr_status_t mbox_static_boxlist(request_rec *r)
{
    apr_status_t rv = APR_SUCCESS;

    mbox_dir_cfg_t *conf;
    mbox_cache_info *mli;
    mbox_file_t *fi;
    apr_array_header_t *files;

    int i;
    const char *base_path;
    char *path, *k;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);
    base_path = get_base_path(r);

    path = apr_pstrdup(r->pool, r->filename);
    k = strstr(path, ".mbox");
    if (!k) {
        return HTTP_NOT_FOUND;
    }

    /* Roll back before the '/YYYYMM' part of the filename */
    k = k - 7;
    *k = 0;

    /* Open mbox cache */
    rv = mbox_cache_get(&mli, path, r->pool);
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "mod_mbox(static_boxlist): Can't open directory cache '%s' for index",
                      path);
        return HTTP_FORBIDDEN;
    }

    files = mbox_fetch_boxes_list(r, mli, path);
    if (!files) {
        return HTTP_FORBIDDEN;
    }

    ap_rputs("  <div id=\"boxlist-cont\">\n", r);


    ap_rputs("  <h5>\n", r);
    if (conf->root_path) {
        ap_rprintf(r, "<a href=\"%s\" title=\"Back to the archives depot\">"
                   "Site index</a> &middot; ", conf->root_path);
    }
    ap_rprintf(r, "<a href=\"%s\" title=\"Back to the list index\">"
               "List index</a>", get_base_path(r));
    ap_rputs("</h5>\n\n", r);


    ap_rputs("  <table id=\"boxlist\">\n", r);
    ap_rputs("   <thead><tr><th colspan=\"2\">Month</th></tr></thead>\n",
             r);
    ap_rputs("   <tbody>\n", r);

    fi = (mbox_file_t *) files->elts;

    for (i = 0; i < files->nelts; i++) {
        if (fi[i].count || !conf->hide_empty) {
            if (strcmp(k + 1, fi[i].filename) == 0) {
                ap_rputs("   <tr id=\"boxactive\">", r);
            }
            else {
                ap_rputs("   <tr>", r);
            }

            ap_rprintf(r,
                       "    <td class=\"box\"><a href=\"%s/%s%s\" title=\"Browse %s %.4s archives\">"
                       "%s %.4s</a></td><td class=\"msgcount\">%d</td>\n",
                       base_path, fi[i].filename, r->path_info,
                       mbox_months[atoi
                                   (apr_pstrndup
                                    (r->pool, fi[i].filename + 4, 2)) - 1][1],
                       fi[i].filename,
                       mbox_months[atoi
                                   (apr_pstrndup
                                    (r->pool, fi[i].filename + 4, 2)) - 1][0],
                       fi[i].filename, fi[i].count);

            ap_rputs("   </tr>\n", r);
        }
    }

    ap_rputs("   </tbody>\n", r);
    ap_rputs("  </table>\n", r);
    ap_rputs("  </div><!-- /#boxlist-cont -->\n", r);

    return APR_SUCCESS;
}

/* Outputs an XHTML list of available mailboxes */
apr_status_t mbox_static_index_boxlist(request_rec *r, mbox_dir_cfg_t *conf,
                                       mbox_cache_info *mli)
{
    mbox_file_t *fi;
    apr_array_header_t *files;
    int side = 0, year_hdr = 0, i;

    files = mbox_fetch_boxes_list(r, mli, r->filename);
    if (!files) {
        return HTTP_FORBIDDEN;
    }

    ap_rputs("  <table id=\"grid\">\n", r);

    fi = (mbox_file_t *) files->elts;

    for (i = 0; i < files->nelts; i++) {
        /* Only display an entry if it has messages or if we don't
           hide empty mailboxes */
        if (fi[i].count || !conf->hide_empty) {
            if (!year_hdr) {
                if (!side) {
                    ap_rputs("  <tr><td class=\"left\">\n", r);
                    side = 1;
                }
                else {
                    ap_rputs("  <td class=\"right\">\n", r);
                    side = 0;
                }

                ap_rputs("   <div class=\"year-cont\">\n", r);
                ap_rputs("   <table class=\"year\">\n", r);
                ap_rputs("    <thead><tr>\n", r);
                ap_rprintf(r, "     <th colspan=\"3\">Year %.4s</th>\n",
                           fi[i].filename);
                ap_rputs("    </tr></thead>\n", r);
                ap_rputs("    <tbody>\n", r);

                year_hdr = 1;
            }

            ap_rputs("    <tr>\n", r);
            ap_rprintf(r, "     <td class=\"date\">%s %.4s</td>\n",
                       mbox_months[atoi(apr_pstrndup(r->pool,
                                                     fi[i].filename + 4,
                                                     2)) - 1][0],
                       fi[i].filename);
            ap_rprintf(r,
                       "     <td class=\"links\"><span class=\"links\" id=\"%.4s%.2s\">"
                       "<a href=\"%.4s%.2s.mbox/thread\">Thread</a>"
                       " &middot; <a href=\"%.4s%.2s.mbox/date\">Date</a>"
                       " &middot; <a href=\"%.4s%.2s.mbox/author\">Author</a></span></td>\n",
                       fi[i].filename, fi[i].filename + 4, fi[i].filename,
                       fi[i].filename + 4, fi[i].filename, fi[i].filename + 4,
                       fi[i].filename, fi[i].filename + 4);
            ap_rprintf(r, "     <td class=\"msgcount\">%d</td>\n",
                       fi[i].count);
            ap_rputs("    </tr>\n", r);
        }

        /* Year separation */
        if (i + 1 < files->nelts && year_hdr
            && (fi[i].filename[3] != fi[i + 1].filename[3])) {
            ap_rputs("    </tbody>\n", r);
            ap_rputs("   </table>\n", r);
            ap_rputs("   </div><!-- /.year-cont -->\n", r);
            if (side) {
                ap_rputs("  </td>\n", r);
            }
            else {
                ap_rputs("  </td></tr>\n\n", r);
            }

            year_hdr = 0;
        }
    }

    ap_rputs("    </tbody>\n", r);
    ap_rputs("   </table>\n\n", r);

    if (side) {
        ap_rputs("  </td><td class=\"right\"></td></tr>\n", r);
    }

    ap_rputs("  </table>\n\n", r);

    return APR_SUCCESS;
}

/* Antispam protection,
 * proper order is:
 * apply mbox_cte_decode_header(), then email_antispam(), then
 * ESCAPE_OR_BLANK()
 */
static char *email_antispam(char *email)
{
    char *tmp;
    int i, atsign = 0, ltsign = 0;

    tmp = strrchr(email, '@');

    if (!tmp) {
        return email;
    }

    /* Before the '@' sign */
    atsign = tmp - email - 1;

    tmp = strstr(email, "&lt;");
    if (tmp) {
        /* After the '&lt;' */
        ltsign = tmp - email + strlen("&lt;") - 1;
    }

    /* Wipe out at most three chars preceding the '@' sign */
    for (i = 0; i < 3; i++) {
        if ((atsign - i) > ltsign) {
            email[atsign - i] = '.';
        }
    }

    return email;
}

/* Display an XHTML message list entry */
static void display_static_msglist_entry(request_rec *r, Message *m,
                                         int linked, int depth)
{
    mbox_dir_cfg_t *conf;

    char *tmp;
    int i;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);

    /* Message author */
    ap_rputs("   <tr>\n", r);

    tmp = mbox_cte_decode_header(r->pool, m->str_from);
    if (conf->antispam) {
        tmp = email_antispam(tmp);
    }
    tmp = ESCAPE_OR_BLANK(r->pool, tmp);

    if (linked) {
        ap_rprintf(r, "    <td class=\"author\">%s</td>\n", tmp);
    }
    else {
        ap_rputs("    <td class=\"author\"></td>\n", r);
    }

    /* Subject, linked or not */
    ap_rputs("     <td class=\"subject\">", r);
    for (i = 0; i < depth; i++) {
        ap_rputs("&nbsp;&nbsp;", r);
    }

    if (linked) {
        ap_rprintf(r, "<a href=\"%s\">",
                   MSG_ID_ESCAPE_OR_BLANK(r->pool, m->msgID));
    }

    ap_rprintf(r, "%s", ESCAPE_AND_CONV_HDR(r->pool, m->subject));
    if (linked) {
        ap_rputs("</a>", r);
    }
    ap_rputs("     </td>\n", r);

    /* Message date */
    if (linked) {
        ap_rprintf(r, "    <td class=\"date\">%s</td>\n",
                   ESCAPE_OR_BLANK(r->pool, m->str_date));
    }
    else {
        ap_rputs("    <td class=\"date\"></td>\n", r);
    }

    ap_rputs("   </tr>\n", r);
}

/* Display an XML message list entry */
static void display_xml_msglist_entry(request_rec *r, Message *m,
                                      int linked, int depth)
{
    mbox_dir_cfg_t *conf;

    char *from;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);

    from = mbox_cte_decode_header(r->pool, m->str_from);
    if (conf->antispam) {
        from = email_antispam(from);
    }
    from = ESCAPE_OR_BLANK(r->pool, from);

    ap_rprintf(r, " <message linked=\"%d\" depth=\"%d\" id=\"%s\">\n",
               linked, depth, ESCAPE_OR_BLANK(r->pool, m->msgID));

    ap_rprintf(r, "  <from><![CDATA[%s]]></from>\n", from);
    ap_rprintf(r, "  <date><![CDATA[%s]]></date>\n",
               ESCAPE_OR_BLANK(r->pool, m->str_date));

    ap_rprintf(r, "  <subject><![CDATA[%s]]></subject>\n",
               ESCAPE_AND_CONV_HDR(r->pool, m->subject));
    ap_rprintf(r, " </message>\n");
}

/* Display a threaded message list for Container 'c' */
static void display_msglist_thread(request_rec *r, Container *c,
                                   int depth, int mode)
{
    Message *m;
    int linked = 1;

    /* Under the rules of our threading tree, if we do not have a
     * message, we MUST have at least one child.  Therefore, print
     * that child's subject when we don't have a message.
     */

    if (c->message) {
        m = c->message;
    }
    else {
        m = c->child->message;
        linked = 0;
    }

    if (mode == MBOX_OUTPUT_STATIC) {
        display_static_msglist_entry(r, m, linked, depth);
    }
    else {
        display_xml_msglist_entry(r, m, linked, depth);
    }

    /* Display children :
     * Subject
     *  +-> Re: Subject */
    if (c->child) {
        display_msglist_thread(r, c->child, depth + 1, mode);
    }

    /* Display follow-ups :
     * Subject
     *  +-> ...
     *  | +-> ...
     *  +-> Re: Subject */
    if (depth && c->next) {
        display_msglist_thread(r, c->next, depth, mode);
    }
}

/* Display the XML index of the specified mbox file. */
apr_status_t mbox_xml_msglist(request_rec *r, apr_file_t *f, int sortFlags)
{
    apr_finfo_t fi;

    MBOX_LIST *head;
    Message *m;
    Container *threads = NULL, *c;

    int current_page = 0;       /* Current page number, starting at 0 */
    int pages;                  /* Number of pages */
    int count = 0;              /* Message count */
    int i = 0;

    /* Fetch page number if present. Otherwise, assume page #1 */
    if (r->args && strcmp(r->args, ""))
        current_page = atoi(r->args);

    /* Load the index of messages from the DB into the MBOX_LIST */
    head = mbox_load_index(r, f, &count);

    /* Compute the page count, depending on the sort flags */
    if (sortFlags != MBOX_SORT_THREAD) {
        pages = count / DEFAULT_MSGS_PER_PAGE;
        if (count > pages * DEFAULT_MSGS_PER_PAGE) {
            pages++;
        }
    }
    else {
        threads = calculate_threads(r->pool, head);
        c = threads;
        count = 0;

        while (c) {
            c = c->next;
            count++;
        }

        pages = count / DEFAULT_THREADS_PER_PAGE;
        if (count > pages * DEFAULT_THREADS_PER_PAGE) {
            pages++;
        }
    }

    /* This index only changes when the .mbox file changes. */
    apr_file_info_get(&fi, APR_FINFO_MTIME, f);
    r->mtime = fi.mtime;
    ap_set_last_modified(r);

    /* Send page header */
    ap_rputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", r);
    ap_rprintf(r, "<index page=\"%d\" pages=\"%d\">\n", current_page, pages);

    /* For date and author sorts */
    if (sortFlags != MBOX_SORT_THREAD) {
        /* Sort the list */
        head = mbox_sort_list(head, sortFlags);

        /* Pass useless messages */
        while (head && (i < current_page * DEFAULT_MSGS_PER_PAGE)) {
            head = head->next;
            i++;
        }

        /* Display current_page's messages */
        while (head && (i < (current_page + 1) * DEFAULT_MSGS_PER_PAGE)) {
            m = (Message *) head->value;
            display_xml_msglist_entry(r, m, 1, 0);

            head = head->next;
            i++;
        }
    }

    /* For threaded view */
    else {
        c = threads;

        /* Pass useless threads */
        while (c && (i < current_page * DEFAULT_THREADS_PER_PAGE)) {
            c = c->next;
            i++;
        }

        /* Display current_page's threads */
        while (c && (i < (current_page + 1) * DEFAULT_THREADS_PER_PAGE)) {
            display_msglist_thread(r, c, 0, MBOX_OUTPUT_AJAX);
            c = c->next;
            i++;
        }
    }

    ap_rputs("</index>", r);

    return OK;
}

static void send_link_if_not_active(request_rec *r,
                                    int is_active,
                                    const char* label,
                                    const char *prefix, const char *suffix,
                                    const char *part1, const char *part2,
                                    const char *part3, const char *part4)
{
    if (is_active) {
        ap_rprintf(r, "%s%s%s", prefix, label, suffix);
    } else {
        ap_rprintf(r, "%s<a href=\"%s%s%s%s\">%s</a>%s",
                      prefix, part1, part2, part3, part4, label, suffix);
    }
}

/* Display the page selector.
 *
 * FIXME: improve the algorithm in order to handle long pages list.
 */
static void mbox_static_msglist_page_selector(request_rec *r, const char *baseURI,
                                              int pages, int current_page)
{
    /* If we don't have more than one page, the page selector is useless. */
    if (pages == 1) {
        ap_rprintf(r, "<span class=\"num-pages\">Showing page %d of %d</span>\n",
                      current_page + 1, pages);
    } else {
        // TODO: Get rid of the calls to apr_psprintf().
        ap_rputs("<span class=\"page-selector\">", r);
        send_link_if_not_active(r, current_page == 0, "&laquo; Previous",
                                "", "", baseURI, r->path_info, "?",
                                apr_psprintf(r->pool, "%d", current_page - 1));

        for (int i = 0; i < pages; i++) {
            ap_rputs(" &middot; ", r);
            send_link_if_not_active(r, current_page == i,
                                    apr_psprintf(r->pool, "%d", i + 1),
                                    "", "", baseURI, r->path_info, "?",
                                    apr_psprintf(r->pool, "%d", i));
        }
        ap_rputs(" &middot; ", r);
        send_link_if_not_active(r, current_page + 1 >= pages, "Next &raquo;",
                                "", "", baseURI, r->path_info, "?",
                                apr_psprintf(r->pool, "%d", current_page + 1));
        ap_rputs("</span>\n", r);
    }
}

static void mbox_static_msglist_nav(request_rec *r, const char *baseURI,
                                    int pages, int current_page,
                                    int sortFlags)
{
    ap_rputs("   <tr>", r);
    send_link_if_not_active(r, sortFlags == MBOX_SORT_AUTHOR, "Author",
                            "<th>", "</th>", baseURI, "/", "author", "");
    send_link_if_not_active(r, sortFlags == MBOX_SORT_THREAD, "Thread",
                            "<th>", "</th>", baseURI, "/", "thread", "");
    send_link_if_not_active(r, sortFlags == MBOX_SORT_DATE, "Date",
                            "<th>", "</th>", baseURI, "/", "date", "");
    ap_rputs("</tr>\n\n", r);
}

/* Send page header */
static apr_status_t send_page_header(request_rec *r, const char *title,
                                     const char *h1)
{
    mbox_dir_cfg_t *conf = ap_get_module_config(r->per_dir_config,
                                                &mbox_module);
    ap_set_content_type(r, "text/html; charset=utf-8");
    if (!h1)
        h1 = title;
    ap_rprintf(r,
               "<!DOCTYPE html>\n"
               "<html>\n"
               " <head>\n"
               "  <meta http-equiv=\"Content-Type\" "
                   "content=\"text/html; charset=utf-8\" />\n"
               "  <title>%s</title>\n",
               title);

    DECLINE_NOT_SUCCESS(mbox_send_header_includes(r, conf));

    ap_rputs(" </head>\n\n"
             " <body id=\"archives\"", r);
    ap_rputs(">\n", r);

    ap_rprintf(r, "  <h1>%s</h1>\n\n", h1);
    return APR_SUCCESS;
}


/* Display the XHTML index of the specified mbox file. */
apr_status_t mbox_static_msglist(request_rec *r, apr_file_t *f,
                                 int sortFlags)
{
    apr_finfo_t fi;

    mbox_dir_cfg_t *conf;
    MBOX_LIST *head;
    Message *m;
    Container *threads = NULL, *c;

    int current_page = 0;       /* Current page number, starting at 0 */
    int pages;                  /* Total number of pages */
    int count = 0;              /* Message count */
    int i = 0;

    const char *baseURI;
    char *filename;
    const char *month;
    const char *year;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);
    baseURI = get_base_uri(r);

    /* Fetch page number if present. Otherwise, assume page #1 */
    if (r->args && strcmp(r->args, ""))
        current_page = atoi(r->args);

    /* Load the index of messages from the DB into the MBOX_LIST */
    head = mbox_load_index(r, f, &count);

    /* Compute the page count, depending on the sort flags */
    if (sortFlags != MBOX_SORT_THREAD) {
        pages = count / DEFAULT_MSGS_PER_PAGE;
        if (count > pages * DEFAULT_MSGS_PER_PAGE) {
            pages++;
        }
    }
    else {
        threads = calculate_threads(r->pool, head);
        c = threads;
        count = 0;

        while (c) {
            c = c->next;
            count++;
        }

        pages = count / DEFAULT_THREADS_PER_PAGE;
        if (count > pages * DEFAULT_THREADS_PER_PAGE) {
            pages++;
        }
    }

    /* This index only changes when the .mbox file changes. */
    apr_file_info_get(&fi, APR_FINFO_MTIME, f);
    r->mtime = fi.mtime;
    ap_set_last_modified(r);

    /* Determine the month and year of the list, if we can. */
    filename = strrchr(r->filename, '/');
    if (filename &&
        apr_fnmatch("[0-9][0-9][0-9][0-9][0-9][0-9].mbox", filename + 1, 0)
        == APR_SUCCESS) {
        month = mbox_months[atoi(apr_pstrndup(r->pool, baseURI +
                                              (strlen(baseURI) -
                                               strlen(".mbox") - 2),
                                              2)) - 1][1];
        year = baseURI + (strlen(baseURI) - strlen(".mbox") - 6);
    }
    else {
        month = "";
        year = "";
    }

    DECLINE_NOT_SUCCESS(send_page_header(r,
                apr_psprintf(r->pool, "%s mailing list archives: %s %.4s",
                             get_base_name(r), month, year),
                NULL));

    ap_rputs("  <div id=\"cont\">\n", r);

    /* Display box list */
    mbox_static_boxlist(r);

    ap_rputs("  <div id=\"msglist-cont\">\n", r);
    ap_rputs("<h5>", r);
    mbox_static_msglist_page_selector(r, baseURI, pages, current_page);
    ap_rputs("</h5>", r);
    ap_rputs("  <table id=\"msglist\">\n", r);
    ap_rputs("  <thead>\n", r);
    mbox_static_msglist_nav(r, baseURI, pages, current_page, sortFlags);
    ap_rputs("  </thead>\n", r);

    ap_rputs("   <tbody>\n", r);

    /* For date or author sorts */
    if (sortFlags != MBOX_SORT_THREAD) {
        /* Sort the list */
        head = mbox_sort_list(head, sortFlags);

        /* Pass useless messages */
        while (head && (i < current_page * DEFAULT_MSGS_PER_PAGE)) {
            head = head->next;
            i++;
        }

        /* Display current_page's messages */
        while (head && (i < (current_page + 1) * DEFAULT_MSGS_PER_PAGE)) {
            m = (Message *) head->value;
            display_static_msglist_entry(r, m, 1, 0);

            head = head->next;
            i++;
        }
    }

    /* For threaded view */
    else {
        c = threads;

        /* Pass useless threads */
        while (c && (i < current_page * DEFAULT_THREADS_PER_PAGE)) {
            c = c->next;
            i++;
        }

        /* Display current_page's threads */
        while (c && (i < (current_page + 1) * DEFAULT_THREADS_PER_PAGE)) {
            display_msglist_thread(r, c, 0, MBOX_OUTPUT_STATIC);
            c = c->next;
            i++;
        }
    }

    ap_rputs("   </tbody>\n", r);
    ap_rputs("  <tfoot>\n", r);
    mbox_static_msglist_nav(r, baseURI, pages, current_page, sortFlags);
    ap_rputs("  </tfoot>\n", r);
    ap_rputs("  </table>\n", r);
    ap_rputs("  </div><!-- /#msglist-cont -->\n", r);

    ap_rputs(" <div id=\"shim\"></div>\n", r);
    ap_rputs(" </div><!-- /#cont -->\n", r);

    DECLINE_NOT_SUCCESS(mbox_send_footer_includes(r, conf));

    ap_rputs(" </body>\n", r);
    ap_rputs("</html>", r);
    return OK;
}

/* Outputs the AJAX browser XHTML stub. */
apr_status_t mbox_ajax_browser(request_rec *r)
{
    mbox_dir_cfg_t *conf = ap_get_module_config(r->per_dir_config,
                                                &mbox_module);

    send_page_header(r,
                     apr_psprintf(r->pool, "%s mailing list archives",
                                  get_base_name(r)),
                     NULL);

    ap_rputs("  <h5>\n", r);

    if (conf->root_path) {
        ap_rprintf(r, "<a href=\"%s\" title=\"Back to the archives depot\">"
                   "Site index</a> &middot; ", conf->root_path);
    }

    ap_rprintf(r, "<a href=\"%s\" title=\"Back to the list index\">"
               "List index</a></h5>", get_base_path(r));

    /* Output a small notice if no MboxScriptPath configuration
       directive was specified. */
    if (!conf->script_path) {
        ap_rputs("  <p>You did not specified a script path, and the dynamic "
                 "browser won't run without it. Check your server configuration.\n",
                 r);
    }

    ap_rputs(" </body>\n</html>\n", r);

    return OK;
}

/* Display a raw mail from cache. No processing is done here. */
int mbox_raw_message(request_rec *r, apr_file_t *f)
{
    int errstatus;
    mbox_mime_message_t *mime_part;
    Message *m;

    char *msgID, *part, *end;

    /* Fetch message ID (offset is 5 : '/raw') and eventual MIME part
       number. */
    msgID = r->path_info + 5;

    part = strchr(msgID, '/');
    if (part) {
        *part = 0;
        part++;
    }

    /* Fetch message */
    m = fetch_message(r, f, msgID);
    if (!m) {
        return HTTP_NOT_FOUND;
    }

    if ((errstatus = ap_meets_conditions(r)) != OK) {
        r->status = errstatus;
        return r->status;
    }

    if (!m->raw_msg) {
        ap_set_content_type(r, "text/plain");
        ap_rprintf(r, "%s", MBOX_FETCH_ERROR_STR);
    }

    /* No MIME part specified : output whole message and return. */
    if (!part) {
        ap_set_content_type(r, "text/plain");
        ap_rprintf(r, "%s", m->raw_msg);

        return OK;
    }

    /* Empty MIME part : we want only mail's body */
    if (!*part) {
        apr_size_t len = m->body_end - m->body_start;

        ap_set_content_type(r, "text/plain");
        ap_rprintf(r, "%s", mbox_mime_decode_body(r->pool, m->cte,
                                                  m->raw_body, len, NULL));
        return OK;
    }

    /* First, parse the MIME structure, and look for the correct
       subpart */
    m->mime_msg = mbox_mime_decode_multipart(r, r->pool, m->raw_body,
                                             m->content_type,
                                             m->charset,
                                             m->cte, m->boundary);

    mime_part = m->mime_msg;

    do {
        int num;

        end = strchr(part, '/');
        if (end) {
            *end = 0;
            num = atoi(part);
            *end = '/';

            part = end + 1;
        }
        else {
            num = atoi(part);
        }

        if (mime_part && num > 0 &&
            (num <= mime_part->sub_count) &&
            mime_part->sub[num - 1] &&
            mime_part->sub[num - 1]->body != NULL) {
            mime_part = mime_part->sub[num - 1];
        }
        else {
            return HTTP_NOT_FOUND;
        }
    } while (*part && end);

    if (strncmp(mime_part->content_type, "multipart/", 10) == 0) {
        ap_set_content_type(r, "text/plain");
    }
    else {
        ap_set_content_type(r, mime_part->content_type);
    }

    if (mime_part->body_len > 0) {
        const char *pdata;
        apr_size_t ret_len;

        mime_part->body[mime_part->body_len] = 0;

        pdata = mbox_mime_decode_body(r->pool,
                                      mime_part->cte,
                                      mime_part->body,
                                      mime_part->body_len, &ret_len);
        if (pdata != NULL && ret_len) {
            ap_rwrite(pdata, ret_len, r);
        }
    }

    return OK;
}

static void mbox_static_message_nav(request_rec *r, char **context,
                                    const char *baseURI, char *msgID)
{
    ap_rputs("    <th class=\"nav\">", r);

    /* Date navigation */
    if (context[0]) {
        ap_rprintf(r, "<a href=\"%s/%s\" "
                   "title=\"Previous by date\">&laquo;</a>",
                   baseURI, MSG_ID_ESCAPE_OR_BLANK(r->pool, context[0]));
    }
    else {
        ap_rputs("&laquo;", r);
    }

    ap_rprintf(r, " <a href=\"%s/date\" "
               "title=\"View messages sorted by date\">Date</a> ", baseURI);

    if (context[1]) {
        ap_rprintf(r, "<a href=\"%s/%s\" "
                   "title=\"Next by date\">&raquo;</a>",
                   baseURI, MSG_ID_ESCAPE_OR_BLANK(r->pool, context[1]));
    }
    else {
        ap_rputs("&raquo;", r);
    }

    ap_rputs(" &middot; ", r);

    /* Thread navigation */
    if (context[2]) {
        ap_rprintf(r, "<a href=\"%s/%s\" "
                   "title=\"Previous by thread\">&laquo;</a>",
                   baseURI, MSG_ID_ESCAPE_OR_BLANK(r->pool, context[2]));
    }
    else {
        ap_rputs("&laquo;", r);
    }

    ap_rprintf(r, " <a href=\"%s/thread\" "
               "title=\"View messages sorted by thread\">Thread</a> ",
               baseURI);

    if (context[3]) {
        ap_rprintf(r, "<a href=\"%s/%s\" "
                   "title=\"Next by thread\">&raquo;</a>",
                   baseURI, MSG_ID_ESCAPE_OR_BLANK(r->pool, context[3]));
    }
    else {
        ap_rputs("&raquo;", r);
    }

    ap_rputs("</th>\n", r);
}

/* Display a static XHTML mail */
int mbox_static_message(request_rec *r, apr_file_t *f)
{
    int errstatus;
    mbox_dir_cfg_t *conf;
    Message *m;

    const char *baseURI;
    char *from, **context, *msgID, *escaped_msgID, *subject;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);
    baseURI = get_base_uri(r);

    msgID = r->path_info + 1;

    /* msgID should be the part of the URI that Apache could not resolve
     * on its own.  Grab it and skip over the expected /. */
    m = fetch_message(r, f, msgID);
    if (!m) {
        return HTTP_NOT_FOUND;
    }

    if ((errstatus = ap_meets_conditions(r)) != OK) {
        r->status = errstatus;
        return r->status;
    }

    /* Parse multipart information */
    m->mime_msg = mbox_mime_decode_multipart(r, r->pool, m->raw_body,
                                             m->content_type,
                                             m->charset,
                                             m->cte, m->boundary);

    subject = ESCAPE_AND_CONV_HDR(r->pool, m->subject);
    send_page_header(r, subject,
                     apr_psprintf(r->pool, "%s mailing list archives",
                                  get_base_name(r)));

    ap_rputs("  <h5>\n", r);

    if (conf->root_path) {
        ap_rprintf(r, "<a href=\"%s\" title=\"Back to the archives depot\">"
                   "Site index</a> &middot; ", conf->root_path);
    }

    ap_rprintf(r, "<a href=\"%s\" title=\"Back to the list index\">"
               "List index</a></h5>", get_base_path(r));

    /* Display context message list */
    from = mbox_cte_decode_header(r->pool, m->from);
    if (conf->antispam) {
        from = email_antispam(from);
    }
    from = ESCAPE_OR_BLANK(r->pool, from);

    ap_rputs("  <table class=\"static\" id=\"msgview\">\n", r);

    context = fetch_context_msgids(r, f, m->msgID);

    /* Top navigation */
    ap_rputs("   <thead>\n"
             "    <tr>\n" "    <th class=\"title\">Message view</th>\n", r);
    mbox_static_message_nav(r, context, baseURI, m->msgID);
    ap_rputs("   </tr>\n" "   </thead>\n\n", r);

    /* Bottom navigation */
    ap_rputs("   <tfoot>\n"
             "    <tr>\n"
             "    <th class=\"title\"><a href=\"#archives\">Top</a></th>\n",
             r);
    mbox_static_message_nav(r, context, baseURI, m->msgID);
    ap_rputs("   </tr>\n" "   </tfoot>\n\n", r);

    /* Headers */
    ap_rputs("   <tbody>\n", r);
    ap_rprintf(r, "   <tr class=\"from\">\n"
               "    <td class=\"left\">From</td>\n"
               "    <td class=\"right\">%s</td>\n" "   </tr>\n", from);

    ap_rprintf(r, "   <tr class=\"subject\">\n"
               "    <td class=\"left\">Subject</td>\n"
               "    <td class=\"right\">%s</td>\n"
               "   </tr>\n", subject);

    ap_rprintf(r, "   <tr class=\"date\">\n"
               "    <td class=\"left\">Date</td>\n"
               "    <td class=\"right\">%s</td>\n"
               "   </tr>\n", ESCAPE_OR_BLANK(r->pool, m->rfc822_date));

    /* Message body */
    ap_rputs("   <tr class=\"contents\"><td colspan=\"2\"><pre>\n", r);
    ap_rprintf(r, "%s",
               mbox_wrap_text(mbox_mime_get_body(r, r->pool, m->mime_msg)));
    ap_rputs("</pre></td></tr>\n", r);

    /* MIME structure */
    ap_rputs("   <tr class=\"mime\">\n"
             "    <td class=\"left\">Mime</td>\n"
             "    <td class=\"right\">\n<ul>\n", r);
    escaped_msgID = MSG_ID_ESCAPE_OR_BLANK(r->pool, m->msgID);
    mbox_mime_display_static_structure(r, m->mime_msg,
                                       apr_psprintf(r->pool, "%s/raw/%s/",
                                                    baseURI, escaped_msgID));
    ap_rputs("</ul>\n</td>\n</tr>\n", r);

    ap_rprintf(r, "   <tr class=\"raw\">\n"
               "    <td class=\"left\"></td>\n"
               "    <td class=\"right\"><a href=\"%s/raw/%s\" rel=\"nofollow\">View raw message</a></td>\n"
               "   </tr>\n", baseURI, escaped_msgID);

    ap_rputs("   </tbody>\n", r);
    ap_rputs("  </table>\n", r);

    ap_rputs(" </body>\n", r);
    ap_rputs("</html>\n", r);

    return OK;
}

/* Display an XML formatted mail */
apr_status_t mbox_xml_message(request_rec *r, apr_file_t *f)
{
    mbox_dir_cfg_t *conf;
    Message *m;
    char *from, *subj, *msgID;

    conf = ap_get_module_config(r->per_dir_config, &mbox_module);

    /* Here, we skip 6 chars (/ajax/). */
    msgID = r->path_info + 6;

    m = fetch_message(r, f, msgID);
    if (!m) {
        return HTTP_NOT_FOUND;
    }

    /* Parse multipart information */
    m->mime_msg = mbox_mime_decode_multipart(r, r->pool, m->raw_body,
                                             m->content_type,
                                             m->charset,
                                             m->cte, m->boundary);

    ap_rputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", r);

    from = mbox_cte_decode_header(r->pool, m->from);
    if (conf->antispam) {
        from = email_antispam(from);
    }
    from = ESCAPE_OR_BLANK(r->pool, from);
    subj = ESCAPE_AND_CONV_HDR(r->pool, m->subject);

    ap_rprintf(r, "<mail id=\"%s\">\n"
               " <from><![CDATA[%s]]></from>\n"
               " <subject><![CDATA[%s]]></subject>\n"
               " <date><![CDATA[%s]]></date>\n"
               " <contents><![CDATA[",
               MSG_ID_ESCAPE_OR_BLANK(r->pool, m->msgID),
               from, subj,
               ESCAPE_OR_BLANK(r->pool, m->rfc822_date));

    ap_rprintf(r, "%s",
               mbox_cntrl_escape(r->pool, mbox_wrap_text(mbox_mime_get_body(r, r->pool, m->mime_msg))));
    ap_rputs("]]></contents>\n", r);
    ap_rputs(" <mime>\n", r);
    mbox_mime_display_xml_structure(r, m->mime_msg,
                                    apr_psprintf(r->pool, "/"));
    ap_rputs(" </mime>\n", r);
    ap_rputs("</mail>\n", r);

    return OK;
}
