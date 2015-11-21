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

/* MIME parsing and structure management functions.
 */

#include "mod_mbox.h"
#include <apr_lib.h>

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

/**
 * find certain header line, return copy of first part (up to first ";")
 * @param p pool to allocate from
 * @param name name of the header
 * @param string input: pointer to pointer string where to find the header;
 *        output: pointer to the ";" or "\n" after the copied value
 * @param end pointer where to stop searching
 * @note string must be NUL-terminated (but the NUL may be after *end)
 * @return copy of the header value or NULL if not found
 */
static char *mbox_mime_get_header(apr_pool_t *p, const char *name,
                                  char **string, const char *end)
{
    char *ptr;
    int namelen = strlen(name);
    for (ptr = *string;
         ptr && *ptr && ptr < end ;
         ptr = ap_strchr(ptr + 1, '\n') + 1)
    {
        int l;
        if (strncasecmp(ptr, name, namelen) != 0)
            continue;
        ptr += namelen;
        if (*ptr != ':')
            continue;
        ptr++;
        while (*ptr == ' ')
            ptr++;
        if (ptr >= end)
            break;
        l = strcspn(ptr, ";\n");
        *string = ptr + l;
        while (apr_isspace(ptr[l]) && l > 0)
            l--;
        return apr_pstrndup(p, ptr, l);
    }
    return NULL;
}

/**
 * find value for parameter with certain name
 * @param p pool to allocate from
 * @param name name of the attribute
 * @param string string with name=value pairs separated by ";",
 *        value may be a quoted string delimited by double quotes
 * @param end pointer where to stop searching
 * @note string must be NUL-terminated (but the NUL may be after *end)
 * @return copy of the value, NULL if not found
 */
static char *mbox_mime_get_parameter(apr_pool_t *p, const char *name,
                                     const char *string, const char *end)
{
    const char *ptr = string;
    int namelen = strlen(name);
    while (ptr && *ptr && ptr < end) {
        int have_match = 0;
        const char *val_end;
        while (*ptr && (apr_isspace(*ptr) || *ptr == ';'))
            ptr++;
        if (strncasecmp(ptr, name, namelen) == 0) {
            ptr += strlen(name);
            while (*ptr && apr_isspace(*ptr) && ptr < end)
                ptr++;
            if (*ptr == '=') {
                have_match = 1;
                ptr++;
                if (ptr >= end)
                    break;
                while (*ptr && apr_isspace(*ptr) && ptr < end)
                    ptr++;
            }
        }
        if (!have_match)
            ptr += strcspn(ptr, ";= \t");
        if (*ptr == '"')
            val_end = ap_strchr_c(++ptr, '"');
        else
            val_end = ptr + strcspn(ptr, ";\n ");
        if (!val_end || val_end > end)
            val_end = end;
        if (have_match)
            return apr_pstrmemdup(p, ptr, val_end - ptr);
        ptr = val_end + 1;
    }
    return NULL;
}

static apr_status_t cleanup_mime_msg(void *data)
{
    mbox_mime_message_t *mail = data;
    free(mail->sub);
    return APR_SUCCESS;
}

/* Decode a multipart (or not) email. In order to support multiple
 * levels of MIME parts, this function is recursive.
 */
mbox_mime_message_t *mbox_mime_decode_multipart(request_rec *r, apr_pool_t *p, char *body,
                                                char *ct, char *charset,
                                                mbox_cte_e cte, char *boundary)
{
    mbox_mime_message_t *mail;
    char *tmp = NULL, *end_bound = NULL;
    char *headers_bound = NULL;

    if (!body) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mbox_mime_decode_multipart: no body");
        return NULL;
    }

    /* Locate the end of part headers */
    if (!ct) {
        headers_bound = ap_strstr(body, "\n\n");
        if (!headers_bound) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                          "no '\\n\\n' header separator found");
            return NULL;
        }
    }
    else {
        headers_bound = body;
    }

    mail = apr_pcalloc(p, sizeof(mbox_mime_message_t));
    /* make sure the memory allocated by realloc() below is cleaned up */
    apr_pool_cleanup_register(p, mail, cleanup_mime_msg, apr_pool_cleanup_null);

    if (!ct) {
        /* If no Content-Type is provided, it means that we are parsing a
         * sub-part of the multipart message. The Content-Type header
         * should then be the first line of the part. If not, use
         * text/plain as default for the sub-part.
         */
        tmp = body;
        ct = mbox_mime_get_header(p, "Content-Type", &tmp, headers_bound);
        if (!ct) {
            ct = "text/plain";
        }
        else {
            if (!charset)
                charset = mbox_mime_get_parameter(p, "charset", tmp, headers_bound);
            mail->content_name = mbox_mime_get_parameter(p, "name", tmp, headers_bound);
        }
        mail->content_type = ct;
    }
    else {
        mail->content_type = ct;
        if (!charset)
            charset = mbox_mime_get_parameter(p, "charset", ct, ct + strlen(ct));
    }
    mail->charset = charset;

    /* Now we have a Content-Type. Look for other useful header information */

    /* Check Content-Disposition if the match is within the headers */
    tmp = body;
    mail->content_disposition = mbox_mime_get_header(p, "Content-Disposition", &tmp, headers_bound);
    if (!mail->content_disposition)
        mail->content_disposition = "inline";

    /* Check Content-Transfer-Encoding, if needed */
    if (cte == CTE_NONE) {
        tmp = body;
        tmp = mbox_mime_get_header(p, "Content-Transfer-Encoding", &tmp, headers_bound);
        if (tmp)
            mail->cte = mbox_parse_cte_header(tmp);
    }
    else {
        mail->cte = cte;
    }

    /* Now we have all the headers we need. Start processing the body */
    if (headers_bound == body)
        mail->body = body;
    else
        mail->body = headers_bound + 2; /* skip double new line */

    /* If the mail is a multipart message, search for the boundary,
       and process its sub parts by recursive calls. */
    if (strncmp(mail->content_type, "multipart/", strlen("multipart/")) == 0) {
        int end = 0, count = 0;
        char *search, *bound, *boundary_line;

        /* If the boundary was not given, we must look for it in the headers */
        if (!boundary) {
            boundary = mbox_mime_get_parameter(p, "boundary", body, headers_bound);
            if (!boundary) {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                              "invalid multipart message: no boundary defined");
                return NULL;
            }
        }
        mail->boundary = boundary;
        ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r,
                      "decoding multipart message: boundary %s", boundary);

        /* Now we have our boundary string. We must : look for it once
           (begining of MIME part) and then look for the end boundary :
           --boundary-- to mark the end of the MIME part.

           In order to handle empty boundaries, we'll look for the
           boundary plus the \n. */

        boundary_line = apr_pstrcat(p, "--", mail->boundary, NULL);

        /* The start boundary */
        bound = ap_strstr(mail->body, boundary_line);
        if (!bound) {
            return NULL;
        }

        /* The end boudary */
        end_bound = apr_psprintf(p, "--%s--", mail->boundary);
        tmp = ap_strstr(mail->body, end_bound);
        if (!tmp) {
            return NULL;
        }
        *tmp = 0;

        /* Set the search begining to the line after the start boundary. */
        search = bound + strlen(boundary_line) + 1;

        /* While the MIME part is not finished, go through all sub parts */
        while (!end) {
            char *inbound;

            inbound = ap_strstr(search, boundary_line);
            if (inbound) {
                *inbound = 0;
            }

            /* Allocate a new pointer for the sub part, and parse it. */
            mail->sub =
                realloc(mail->sub, ++count * sizeof(struct mimemsg *));
            ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r,
                          "decoding part %d", count);
            mail->sub[count - 1] =
                mbox_mime_decode_multipart(r, p, search, NULL, NULL, CTE_NONE, NULL);

            /* If the boudary is found again, it means we have another
               MIME part in the same multipart message. Set the new
               search begining to the line after this new start
               boundary */
            if (inbound) {
                *inbound = '-';

                search = inbound + strlen(boundary_line) + 1;
            }

            /* Otherwise, the MIME part is finished. */
            else {
                mail->sub_count = count;
                end = 1;
            }
        }

        /* Finally reset the end-body pointer. */
        //      *tmp = '-';
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "done decoding multipart message (boundary %s)",
                      boundary);
    }

    /* If the parsed body is not multipart or is a MIME part, the body
       length is the length of the body string (no surprise here). If
       it's a MIME part, its correct length will be set after the call
       to mbox_mime_decode_multipart just a dozen lines above. */
    else {
        if (mail->body != NULL) {
            mail->body_len = strlen(mail->body);
        }
        else {
            mail->body_len = 0;
        }
    }

    return mail;
}

/* Decode a MIME part body, according to its CTE. */
char *mbox_mime_decode_body(apr_pool_t *p, mbox_cte_e cte, char *body,
                            apr_size_t len, apr_size_t *ret_len)
{
    char *new_body;

    /* Failsafe : in case of body == NULL or len == 0, apr_pstrndup
       will not allocate anything, not even one byte for the '\0' */
    if (!body || !len) {
        return NULL;
    }

    new_body = apr_pstrndup(p, body, len);

    if (cte == CTE_BASE64)
        len = mbox_cte_decode_b64(new_body);
    else if (cte == CTE_QP)
        len = mbox_cte_decode_qp(new_body);

    if (ret_len)
        *ret_len = len;

    new_body[len] = 0;
    return new_body;
}

/* This function returns the relevant MIME part from a message. For
 * the moment, it just returns the first text/ MIME part available.
 */
char *mbox_mime_get_body(request_rec *r, apr_pool_t *p, mbox_mime_message_t *m)
{
    int i;

    /* If the message structure or the message body is empty, just
       return NULL */
    if (!m || !m->body) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mbox_mime_get_body: %s",
                      m == NULL ? "no message???" : "no body");
        return MBOX_FETCH_ERROR_STR;
    }

    if (strncasecmp(m->content_type, "text/", strlen("text/")) == 0) {
        char *new_body;
        apr_size_t new_len;
        new_body = mbox_mime_decode_body(p, m->cte, m->body, m->body_len,
                                         &new_len);
        if (!new_body) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                          "mbox_mime_get_body: could not decode body");
            return MBOX_FETCH_ERROR_STR;
        }

        if (m->charset) {
            struct ap_varbuf vb;
            apr_status_t rv;
            ap_varbuf_init(p, &vb, 0);
            vb.strlen = 0;
            ap_log_rerror(APLOG_MARK, APLOG_TRACE3, 0, r,
                          "mbox_mime_get_body: converting %" APR_SIZE_T_FMT " bytes from %s",
                          new_len, m->charset);
            if ((rv = mbox_cte_convert_to_utf8(p, m->charset, new_body, new_len, &vb))
                == APR_SUCCESS) {
                new_body = vb.buf;
                new_len = vb.strlen + 1;
            }
            else {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                              "conversion from '%s' to utf-8 failed", m->charset);
            }
            ap_log_rerror(APLOG_MARK, APLOG_TRACE4, 0, r,
                          "mbox_mime_get_body: conversion done");
        }

        mbox_cte_escape_html(p, new_body, new_len, &new_body);
        return new_body;
    }

    if (!m->sub) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mbox_mime_get_body: message not text/* and no sub parts");
        return MBOX_FETCH_ERROR_STR;
    }

    for (i = 0; i < m->sub_count; i++) {
        /* XXX this loop is bullshit, should check result of mbox_mime_get_body()  */
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mbox_mime_get_body: choosing m->sub[%d]", i);
        return mbox_mime_get_body(r, p, m->sub[i]);
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "m->sub != NULL but m->subcount == 0 ???");

    return MBOX_FETCH_ERROR_STR;
}

/*
 * Display an XHTML MIME structure
 * 'link' must already be properly URI-escaped
 */
void mbox_mime_display_static_structure(request_rec *r,
                                        mbox_mime_message_t *m, char *link)
{
    int i;

    if (!m) {
        return;
    }

    ap_rputs("<li>", r);

    if (m->body_len) {
        ap_rprintf(r, "<a rel=\"nofollow\" href=\"%s\">", link);
    }

    if (m->content_name) {
        ap_rprintf(r, "%s (%s)",
                   ESCAPE_OR_BLANK(r->pool, m->content_name),
                   ESCAPE_OR_BLANK(r->pool, m->content_type));
    }
    else {
        ap_rprintf(r, "Unnamed %s", ESCAPE_OR_BLANK(r->pool, m->content_type));
    }

    if (m->body_len) {
        ap_rputs("</a>", r);
    }

    ap_rprintf(r, " (%s, %s, %" APR_SIZE_T_FMT " bytes)</li>\n",
               m->content_disposition, mbox_cte_to_char(m->cte), m->body_len);

    if (!m->sub) {
        return;
    }

    for (i = 0; i < m->sub_count; i++) {
        ap_rputs("<ul>\n", r);

        if (link[strlen(link) - 1] == '/') {
            link[strlen(link) - 1] = 0;
        }

        mbox_mime_display_static_structure(r, m->sub[i],
                                           apr_psprintf(r->pool, "%s/%d",
                                                        link, i + 1));
        ap_rputs("</ul>\n", r);
    }
}

/* Display an XML MIME structure */
void mbox_mime_display_xml_structure(request_rec *r, mbox_mime_message_t *m,
                                     char *link)
{
    int i;

    if (!m) {
        return;
    }

    if (m->content_name) {
        ap_rprintf(r, "<part name=\"%s\" cd=\"%s\" cte=\"%s\" "
                   "length=\"%" APR_SIZE_T_FMT "\" link=\"%s\" />\n",
                   m->content_name, m->content_disposition,
                   mbox_cte_to_char(m->cte), m->body_len, link);
    }
    else {
        ap_rprintf(r, "<part ct=\"%s\" cd=\"%s\" cte=\"%s\" "
                   "length=\"%" APR_SIZE_T_FMT "\" link=\"%s\" />\n",
                   m->content_type, m->content_disposition,
                   mbox_cte_to_char(m->cte), m->body_len, link);
    }

    if (!m->sub) {
        return;
    }

    ap_rputs("<mime>\n", r);
    for (i = 0; i < m->sub_count; i++) {
        if (link[strlen(link) - 1] == '/') {
            link[strlen(link) - 1] = 0;
        }

        mbox_mime_display_xml_structure(r, m->sub[i],
                                        apr_psprintf(r->pool, "%s/%d", link,
                                                     i + 1));
    }
    ap_rputs("</mime>\n", r);
}
