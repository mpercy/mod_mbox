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

/* Decoding common Content-Encodings of E-Mail functions.
 *
 * These decoding functions do not copy data.
 */

#include "mod_mbox.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

/*
 * The char64 macro and `mime_decode_b64' routine are taken from
 * metamail 2.7, which is copyright (c) 1991 Bell Communications
 * Research, Inc. (Bellcore).  The following license applies to all
 * code below this point:
 *
 * Permission to use, copy, modify, and distribute this material
 * for any purpose and without fee is hereby granted, provided
 * that the above copyright notice and this permission notice
 * appear in all copies, and that the name of Bellcore not be
 * used in advertising or publicity pertaining to this
 * material without the specific, prior written permission
 * of an authorized representative of Bellcore.  BELLCORE
 * MAKES NO REPRESENTATIONS ABOUT THE ACCURACY OR SUITABILITY
 * OF THIS MATERIAL FOR ANY PURPOSE.  IT IS PROVIDED "AS IS",
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES.
 */

static char index_64[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1
};

#define char64(c)  (((c) < 0 || (c) > 127) ? -1 : index_64[(c)])

const char *mbox_cte_to_char(mbox_cte_e cte)
{
    switch (cte) {
    case CTE_NONE:
        return "None";
    case CTE_7BIT:
        return "7-Bit";
    case CTE_8BIT:
        return "8-Bit";
    case CTE_UUENCODE:
        return "uuencode";
    case CTE_BINARY:
        return "Binary";
    case CTE_QP:
        return "Quoted Printable";
    case CTE_BASE64:
        return "Base64";
    default:
        return "Unknown CTE";
    }
}

/* Unlike the original ap_escape_html, this one is also binary
 * safe.
 * The result is always NUL-terminated
 */
apr_size_t mbox_cte_escape_html(apr_pool_t *p, const char *s,
                                apr_size_t len, char **body)
{
    char *x;
    int i, j;

    /* First, count the number of extra characters */
    for (i = 0, j = 0; i < len; i++) {
        if ((s[i] == '<') || (s[i] == '>')) {
            j += 3;
        }
        else if (s[i] == '&') {
            j += 4;
        }
    }

    /* If there is nothing to escape, just copy the body to the new
       string */
    if (j == 0) {
        j = len;
        x = apr_pstrmemdup(p, s, len);
    }

    /* Otherwise, we have some extra characters to insert : allocate
       enough space for them, and process the data. */
    else {
        x = apr_palloc(p, i + j + 1);

        for (i = 0, j = 0; i < len; i++, j++) {
            if (s[i] == '<') {
                memcpy(&x[j], "&lt;", 4);
                j += 3;
            }
            else if (s[i] == '>') {
                memcpy(&x[j], "&gt;", 4);
                j += 3;
            }
            else if (s[i] == '&') {
                memcpy(&x[j], "&amp;", 5);
                j += 4;
            }
            else {
                x[j] = s[i];
            }
        }
	x[j] = '\0';
    }

    *body = x;
    return j;
}

/* Decode BASE64 encoded data */
apr_size_t mbox_cte_decode_b64(char *src)
{
    apr_size_t len = 0;

    int data_done = 0;
    int c1, c2, c3, c4;
    char *dst;

    dst = src;

    while ((c1 = *src++) != '\0') {
        if (isspace(c1))
            continue;

        if (data_done)
            break;

        do {
            c2 = *src++;
        } while (c2 != '\0' && isspace(c2));

        do {
            c3 = *src++;
        } while (c3 != '\0' && isspace(c3));

        do {
            c4 = *src++;
        } while (c4 != '\0' && isspace(c4));

        /* Premature EOF. Should return an Error? */
        if ((c2 == '\0') || (c3 == '\0') || (c4 == '\0')) {
            return len;
        }

        if (c1 == '=' || c2 == '=') {
            data_done = 1;
            continue;
        }

        c1 = char64(c1);
        c2 = char64(c2);
        *dst++ = (c1 << 2) | ((c2 & 0x30) >> 4);
        len++;

        if (c3 == '=') {
            data_done = 1;
        }
        else {
            c3 = char64(c3);
            *dst++ = ((c2 & 0XF) << 4) | ((c3 & 0x3C) >> 2);
            len++;

            if (c4 == '=') {
                data_done = 1;
            }
            else {
                c4 = char64(c4);
                *dst++ = ((c3 & 0x03) << 6) | c4;
                len++;
            }
        }
    }

    *dst = '\0';
    return len;
}

static int hex2dec_char(char ch)
{
    if (isdigit(ch)) {
        return ch - '0';
    }
    else if (isupper(ch)) {
        return ch - 'A' + 10;
    }
    else {
        return ch - 'a' + 10;
    }
}

/* Decode quoted-printable to raw text. */
apr_size_t mbox_cte_decode_qp(char *p)
{
    apr_size_t len = 0;
    char *src, *dst;

    dst = src = p;
    while (*src != '\0') {
        if (*src == '=') {
            if (*++src == '\n') {
                ++src;
                continue;
            }
            else {
                int hi, lo;
                hi = hex2dec_char(*src++);
                lo = hex2dec_char(*src);
                *dst = (hi * 16) + lo;
            }
        }
        else {
            *dst = *src;
        }

        ++dst, ++src;
        len++;
    }

    return len;
}

apr_status_t mbox_cte_convert_to_utf8(apr_pool_t *p, const char *charset,
                                      const char *src, apr_size_t len,
                                      struct ap_varbuf *vb)
{
    apr_xlate_t *convset;
    apr_status_t rv;
    apr_size_t outbytes_left, inbytes_left = len;
    char *dst;
    if (len <= 0)
        return APR_SUCCESS;
    /* Special case "utf8": it is often unknown (no alias) */
    if (!strcmp(charset, "utf8") || !strcmp(charset, "UTF8")) {
        ap_log_error(APLOG_MARK, APLOG_TRACE7, 0, ap_server_conf,
                     "Identity shortcut for convset '%s'", charset);
        ap_varbuf_strmemcat(vb, src, len);
        return APR_SUCCESS;
    }
    rv = apr_xlate_open(&convset, "UTF-8", charset, p);
    if (rv != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, ap_server_conf,
                     "could not open convset '%s'", charset);
        return rv;
    }
    ap_log_error(APLOG_MARK, APLOG_TRACE6, rv, ap_server_conf,
                 "using convset %s", charset);

    while (inbytes_left > 0) {
        ap_varbuf_grow(vb, vb->strlen + inbytes_left + 8);
        dst = vb->buf + vb->strlen;
        outbytes_left = vb->avail - vb->strlen;
        rv = apr_xlate_conv_buffer(convset, src + len - inbytes_left, &inbytes_left,
                                   dst, &outbytes_left);
        if (rv != APR_SUCCESS) {
            *dst = '\0';
            goto out;
        }
        vb->strlen = vb->avail - outbytes_left;
    }
    ap_varbuf_grow(vb, vb->strlen + 8);
    outbytes_left = vb->avail - vb->strlen;
    dst = vb->buf + vb->strlen;
    rv = apr_xlate_conv_buffer(convset, NULL, NULL, dst, &outbytes_left);
    if (rv != APR_SUCCESS) {
        *dst = '\0';
        goto out;
    }
    vb->strlen = vb->avail - outbytes_left;
    vb->buf[vb->strlen] = '\0';

out:
    apr_xlate_close(convset);
    return rv;
}

/* This function performs the decoding of strings like :
 * =?UTF-8?B?QnJhbmtvIMSMaWJlag==?=
 *
 * These strings complies to the following syntax :
 * =?charset?mode?data?= rest
 *
 * Appends decoded string to vb, resturns
 * position where to continue parsing.
 */
static char *mbox_cte_decode_rfc2047(apr_pool_t *p, char *src, struct ap_varbuf *vb)
{
    char *charset, *mode, *data, *rest;
    int i;
    apr_status_t rv;
    apr_size_t data_len;

    if (strncmp(src, "=?", 2) != 0)
        return src;
    charset = src + strlen("=?");

    /* Encoding mode (first '?' after charset) */
    mode = strstr(charset, "?");
    if (!mode) {
        return src;
    }
    mode++;

    /* Fetch data */
    data = strstr(mode, "?");
    if (!data || data != mode + 1)
        return src;
    data++;

    /* Look for the end bound */
    rest = strstr(data, "?=");
    if (!rest)
        return src;
    data = apr_pstrmemdup(p, data, rest - data);

    /* Quoted-Printable decoding : mode 'q' */
    if ((*mode == 'q') || (*mode == 'Q')) {
        int i;

        /* In QP header encoding, spaces are encoded either in =20 (as
           in all QP encoding) or in underscores '_' (for header
           encoding). The first case will be handle by the QP
           decoding, so we must handle the other one */
        for (i = 0; i < strlen(data); i++) {
            if (data[i] == '_') {
                data[i] = ' ';
            }
        }

        data_len = mbox_cte_decode_qp(data);
    }
    else if ((*mode == 'b') || (*mode == 'B')) {
        data_len = mbox_cte_decode_b64(data);
    }
    else {
        return src;
    }

    /* Convert charset to uppercase */
    charset = apr_pstrmemdup(p, charset, mode - charset - 1);
    for (i = 0; i < strlen(charset); i++) {
        charset[i] = toupper(charset[i]);
    }

    /* Charset conversion */
    rv = mbox_cte_convert_to_utf8(p, charset, data, data_len, vb);
    if (rv != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, ap_server_conf,
                     "conversion from %s to utf-8 failed", charset);
        *rest = '?';
        return src;
    }
    return rest + strlen("?=");;
}

/* MIME header decoding (see RFC 2047). */
char *mbox_cte_decode_header(apr_pool_t *p, char *src)
{
    char *start, *cont;
    struct ap_varbuf vb;
    int seen_encoded_word = 0;
    if (src == NULL || *src == '\0')
        return "";
    ap_varbuf_init(p, &vb, 0);
    vb.strlen = 0;

    do {
        start = strstr(src, "=?");
        if (!start) {
            if (vb.strlen == 0)
                return src;
            return apr_pstrcat(p, vb.buf, src, NULL);
        }

        if (start != src) {
            if (seen_encoded_word) {
                /* space between consecutive encoded words must be discarded */
                char *p = src;
                while (p < start && apr_isspace(*p))
                    p++;
                if (p == start)
                    src = start;
                /* XXX: this is wrong if the next encoded word fails to decode */
            }
            if (start != src) {
                ap_varbuf_strmemcat(&vb, src, start - src);
                seen_encoded_word = 0;
            }
        }

        cont = mbox_cte_decode_rfc2047(p, start, &vb);
        if (cont == start) {
            /* decoding failed, copy start delimiter and continue */
            ap_varbuf_strmemcat(&vb, start, 2);
            src = start + 2;
        }
        else {
            src = cont;
            seen_encoded_word = 1;
        }
    } while (src && *src);

    /* vb.buf is pool memory */
    return vb.buf;
}
