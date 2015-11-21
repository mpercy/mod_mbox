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


/**
 * This file contains functions for generating a Sitemap, to make indexing
 * by search engines easier on both them, and us:
 *  <http://www.sitemaps.org/protocol.php>
 */

#include "mod_mbox.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

static void mbox2sitemap(request_rec *r, const char *mboxfile,
                         mbox_cache_info *mli, 
                         int partition,
                         int partmax)
{
    char *filename;
    char *origfilename;
    MBOX_LIST *head;
    Message *m;
    apr_pool_t *tpool;

    filename = apr_pstrcat(r->pool, r->filename, mboxfile, NULL);
    origfilename = r->filename;
    
    r->filename = filename;
    
    head = mbox_load_index(r, NULL, NULL);

    r->filename = origfilename;

    apr_pool_create(&tpool, r->pool);
    while (head != NULL) {
        char dstr[100];
        apr_size_t dlen;
        apr_time_exp_t extime;
        apr_ssize_t hlen;
        unsigned int hrv;

        m = (Message *) head->value;

        if (!m || !m->msgID) {
            head = head->next;
            apr_pool_clear(tpool);
            continue;
        }

        hlen = APR_HASH_KEY_STRING;

        hrv = apr_hashfunc_default(m->msgID, &hlen);

        if (partmax) {
            int v = hrv % partmax;
            if (v != partition) {
                head = head->next;
                apr_pool_clear(tpool);
                continue;
            }
        }

        ap_rputs("<url>\n", r);

        ap_rprintf(r, "<loc><![CDATA[%s%s/%s]]></loc>\n",
                   ap_construct_url(tpool, r->uri, r),
                   mboxfile, MSG_ID_ESCAPE_OR_BLANK(tpool, m->msgID));

        apr_time_exp_gmt(&extime, m->date);
        apr_strftime(dstr, &dlen, sizeof(dstr), "%G-%m-%d", &extime);
        
        ap_rprintf(r, "<lastmod>%s</lastmod>\n", dstr);
        ap_rputs("<changefreq>never</changefreq>\n", r);
        ap_rputs("</url>\n", r);
        head = head->next;
        apr_pool_clear(tpool);
    }
}

static void mbox_sitemap_entries(request_rec *r, mbox_cache_info *mli)
{
    mbox_file_t *fi;
    apr_array_header_t *files;
    int i;
    int partition = 0;
    int partmax = 0;
    const char *p;

    files = mbox_fetch_boxes_list(r, mli, r->filename);
    if (!files) {
        return;
    }

    if (r->args) {
        p = strstr(r->args, "part=");
        if (p) {
            partition = atoi(p + 5);
        }

        p = strstr(r->args, "pmax=");
        if (p) {
            partmax = atoi(p + 5);
        }
    }

    if (partition == 0) {
        ap_rputs("<url>\n", r);
        ap_rprintf(r, "<loc>%s?format=atom</loc>\n",
                   ap_construct_url(r->pool, r->uri, r));
        ap_rputs("</url>\n", r);
    }

    fi = (mbox_file_t *) files->elts;
    for (i = 0; i < files->nelts; i++) {
        if (!fi[i].count) {
            continue;
        }
        mbox2sitemap(r, fi[i].filename, mli, partition, partmax);
    }
}

int mbox_sitemap_handler(request_rec  *r, mbox_cache_info *mli)
{
    int errstatus;
    char *etag;
    
    /* Only allow GETs */
    r->allowed |= (AP_METHOD_BIT << M_GET);
    if (r->method_number != M_GET) {
        return HTTP_METHOD_NOT_ALLOWED;
    }
    
    ap_set_content_type(r, "text/xml; charset=utf-8");
    
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
    ap_rputs("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n", r);
    mbox_sitemap_entries(r, mli);
    ap_rputs("</urlset>\n", r);

    return OK;
}

