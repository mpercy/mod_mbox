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

/* DEFAULT_LIMIT_REQUEST_FIELDSIZE is now CORE_PRIVATE */
#define CORE_PRIVATE
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "util_varbuf.h"

#include "apr_date.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_dbm.h"
#include "apr_hash.h"
#include "apr_fnmatch.h"
#include "apr_xlate.h"

#include <stdio.h>
#include <ctype.h>

#include "mbox_cache.h"
#include "mbox_parse.h"
#include "mbox_thread.h"

#ifndef MOD_MBOX_H
#define MOD_MBOX_H

#ifdef __cplusplus
extern "C"
{
#endif

#define MBOX_HANDLER "mbox-handler"
#define MBOX_MAGIC_TYPE "mbox-file"

#define DEFAULT_MSGS_PER_PAGE 100
#define DEFAULT_THREADS_PER_PAGE 40

#define MBOX_PREV 0
#define MBOX_NEXT 1
#define MBOX_PREV_THREAD 2
#define MBOX_NEXT_THREAD 3

#define MBOX_OUTPUT_STATIC 0
#define MBOX_OUTPUT_AJAX   1

#define MBOX_WRAP_TO 90
#define MBOX_MAX_WRAP 50000

#define MBOX_ATOM_NUM_ENTRIES 40

#define MBOX_FETCH_ERROR_STR "An error occured while fetching this message, sorry !"

typedef struct mbox_dir_cfg
{
    int enabled;
    int antispam;
    int hide_empty;
    const char *root_path;
    const char *style_path;
    const char *script_path;
} mbox_dir_cfg_t;

typedef struct mbox_file
{
    char *filename;
    int count;
} mbox_file_t;

/* Declare ourselves so the configuration routines can find and know us.
* We'll fill it in at the end of the module.
*/
extern module mbox_module;

/* Handlers */
int mbox_atom_handler(request_rec *r, mbox_cache_info *mli);
int mbox_sitemap_handler(request_rec *r, mbox_cache_info *mli);
int mbox_file_handler(request_rec *r);
int mbox_index_handler(request_rec *r);

/* Output functions */
apr_status_t mbox_xml_msglist(request_rec *r, apr_file_t *f,
                              int sortFlags);
apr_status_t mbox_static_msglist(request_rec *r, apr_file_t *f,
                                 int sortFlags);
apr_status_t mbox_xml_boxlist(request_rec *r);
apr_status_t mbox_static_boxlist(request_rec *r);
apr_status_t mbox_static_index_boxlist(request_rec *r,
                                       mbox_dir_cfg_t *conf,
                                       mbox_cache_info *mli);

void mbox_atom_entries(request_rec *r, mbox_cache_info *mli);

apr_status_t mbox_ajax_browser(request_rec *r);

int mbox_raw_message(request_rec *r, apr_file_t *f);
int mbox_static_message(request_rec *r, apr_file_t *f);
apr_status_t mbox_xml_message(request_rec *r, apr_file_t *f);

/* CTE decoding functions */
const char *mbox_cte_to_char(mbox_cte_e cte);
apr_size_t mbox_cte_decode_qp(char *p);
apr_size_t mbox_cte_decode_b64(char *src);
apr_size_t mbox_cte_escape_html(apr_pool_t *p, const char *s,
                                apr_size_t len, char **body);
char *mbox_cte_decode_header(apr_pool_t *p, char *src);
apr_status_t mbox_cte_convert_to_utf8(apr_pool_t *p, const char *charset,
                                      const char *src, apr_size_t len,
                                      struct ap_varbuf *vb);

/* MIME decoding functions */
mbox_mime_message_t *mbox_mime_decode_multipart(request_rec *r, apr_pool_t *p,
                                                char *body, char *ct,
                                                char *charset,
                                                mbox_cte_e cte,
                                                char *boundary);
char *mbox_mime_decode_body(apr_pool_t *p, mbox_cte_e cte, char *body,
                            apr_size_t len, apr_size_t *ret_len);
char *mbox_mime_get_body(request_rec *r, apr_pool_t *p, mbox_mime_message_t *m);
void mbox_mime_display_static_structure(request_rec *r,
                                        mbox_mime_message_t *m,
                                        char *link);
void mbox_mime_display_xml_structure(request_rec *r,
                                     mbox_mime_message_t *m, char *link);

/* Utility functions */
char *mbox_wrap_text(char *str);
char *mbox_cntrl_escape(apr_pool_t *p, char *s);
char *mbox_msg_id_escape(apr_pool_t *p, char *s);
const char *get_base_path(request_rec *r);
const char *get_base_uri(request_rec *r);
const char *get_base_name(request_rec *r);

/* XXX This should enforce that the result is valid UTF-8 */
#define ESCAPE_OR_BLANK(pool, s) \
(s ? mbox_cntrl_escape(pool, ap_escape_html(pool, s)) : "")

/* XXX This should enforce that the result is valid UTF-8 */
#define ESCAPE_AND_CONV_HDR(pool, s) \
(s ? mbox_cntrl_escape(pool, ap_escape_html(pool, mbox_cte_decode_header(pool, s))) : "")

#define MSG_ID_ESCAPE_OR_BLANK(pool, s) \
(s ? mbox_msg_id_escape(pool, ap_escape_uri(pool, s)) : "")

/* Backend functions */
apr_array_header_t *mbox_fetch_boxes_list(request_rec *r,
                                          mbox_cache_info *mli,
                                          char *path);
Message *fetch_message(request_rec *r, apr_file_t *f, char *msgID);
char **fetch_context_msgids(request_rec *r, apr_file_t *f, char *msgID);

void load_message(apr_pool_t *p, apr_file_t *f, Message *m);

#ifdef __cplusplus
}
#endif

#endif
