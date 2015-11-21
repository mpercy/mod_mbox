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

/* mbox_parse.c
 *
 * This file contains a variety of functions to enable "easy" parsing
 * of an mbox file.
 *
 * The key point is that we will create a series of DBMs that represent
 * the fields that we wish to quickly retrieve.  This will enable us to
 * quickly return a list of all messages or a particular message.  The
 * offset to a particular message should also be included in the DBM
 * so that we can quickly seek to the appropriate location in the original
 * mbox file.
 *
 * Currently, this version has only been tested with mbox files generated
 * by qmail for the apache.org lists.  It should be trivial to enable parsing
 * of slightly different mbox formats (of which there are a few).
 */

#include "mbox_parse.h"
#include "mbox_sort.h"
#include "mbox_dbm.h"

/* FIXME: Remove this when apr_date_parse_rfc() and ap_strcasestr() are fixed ! */
#include "mbox_externals.h"

#include "apr_dbm.h"
#include "apr_hash.h"
#include "apr_date.h"
#include "apr_lib.h"

/* for dirname() */
#include <libgen.h>

#define OPEN_DBM(r, db, flags, suffix, temp, status) \
    temp = apr_pstrcat(r->pool, r->filename, suffix, NULL); \
    status = apr_dbm_open_ex(&db, APR_STRINGIFY(DBM_TYPE), temp, flags, APR_OS_DEFAULT, r->pool);

#define MSGID_DBM_SUFFIX ".msgsum"

typedef struct mb_dbm_data
{
    apr_off_t msg_start;
    apr_off_t body_start;
    apr_off_t body_end;
    mbox_cte_e cte;
    apr_time_t date;
    const char *from;
    const char *subject;
    const char *references;
    const char *content_type;
    const char *charset;
    const char *boundary;
} mb_dbm_data;

/* Fills the MBOX_BUFF structure with data from the backing file descriptor.
 * Will read up to MBOX_BUFF->maxlen-(remaining in buffer) bytes.
 */
void mbox_fillbuf(MBOX_BUFF *fb)
{
    if (fb->fd) {
        int len = strlen(fb->b);

        /* We are backed by a file descriptor.
         * Read a new set of characters in.
         */
        do {
            if (len > 0) {
                memmove(fb->sb, fb->b, len);
                fb->rb = fb->sb + len;
            }
            else
                fb->rb = fb->sb;

            fb->b = fb->sb;
            fb->len = fb->maxlen - len;

            /* Input: fb->len contains how many bytes I want.
             * Output: fb->len contains how many bytes I got.
             *
             * If we reached the EOF, we don't want to read anymore.
             */
            if (APR_EOF == apr_file_read(fb->fd, fb->rb, &fb->len))
                fb->fd = 0;

            fb->rb[fb->len] = '\0';
            fb->totalread += fb->len;
        }
        while (fb->fd && fb->b && !strchr(fb->b, LF));
    }
}

/* Looks at the next character in the buffer, but does not advance the
 * pointer.
 *
 * Only array element 0 of the input char* is guaranteed to be valid.
 */
static int mbox_blookc(char *buff, MBOX_BUFF *fb)
{
    *buff = '\0';

    if (!fb || !fb->b || !fb->b[0])
        return 0;

    buff[0] = fb->b[0];

    return 1;
}

/* Reads a string.
 *
 * If we are a restartable buffer (MBOX_BUFF->sb != NULL) and we don't
 * find a LF, returns -1.  Otherwise we will return the remainder of the
 * char * not read yet.
 *
 * Note: Per the original ap_bgets(), this function strips CRLFs into
 * LFs.
 *
 * Attempts to mimic buff.c - ap_bgets()
 */
static apr_size_t mbox_bgets(char *buff, int n, MBOX_BUFF *fb)
{
    char *tmp;
    apr_size_t len;

    if (!fb->b) {
        return -1;
    }

    tmp = strchr(fb->b, LF);

    if (!tmp) {
        if (fb->fd) {
            mbox_fillbuf(fb);

            tmp = strchr(fb->b, LF);
            if (tmp)
                len = tmp - fb->b;
            else {
                fb->b = 0;
                return -1;
            }
        }
        else {                  /* if (fb->sb) */

            fb->b = 0;
            return -1;
        }
    }
    else
        len = tmp - fb->b;

    /* Ensure that we can not overflow */
    if (len + 2 < n) {
        /* Don't copy the linefeed as we will play tricks with it. */
        memcpy(buff, fb->b, len);

        /* Advance our pointer past the linefeed. */
        fb->b += len + 1;
    }
    else {
        /* Leave space for EOL and term NULL. */
        len = n - 2;

        memcpy(buff, fb->b, len);

        fb->b += len;
    }

    if (!len || buff[len - 1] != CR)    /* Blank line or no previous CR */
        buff[len++] = '\n';
    else                        /* replace CR with LF */
        buff[--len] = '\n';

    /* Add terminating null. */
    buff[len] = '\0';

    return len;
}

/* Get a line of protocol input, including any continuation lines
 * caused by MIME folding (or broken clients) if fold != 0, and place it
 * in the buffer s, of size n bytes, without the ending newline.
 *
 * Returns -1 on error, or the length of s.
 *
 * Note: Because bgets uses 1 char for newline and 1 char for NUL,
 *       the most we can get is (n - 2) actual characters if it
 *       was ended by a newline, or (n - 1) characters if the line
 *       length exceeded (n - 1).  So, if the result == (n - 1),
 *       then the actual input line exceeded the buffer length,
 *       and it would be a good idea for the caller to puke 400 or 414.
 *
 * Adapted from http_protocol.c - getline()
 *
 * FIXME: MAY NOT WORK WITH CHARSET_EBCDIC DEFINED
 */
int mbox_getline(char *s, int n, MBOX_BUFF *in, int fold)
{
    char *pos, next;
    int retval;
    int total = 0;

    pos = s;

    do {
        retval = mbox_bgets(pos, n, in);        /* retval == -1 if error, 0 if EOF */
        if (retval <= 0) {
            total = ((retval < 0) && (total == 0)) ? -1 : total;
            break;
        }

        /* retval is the number of characters read, not including NUL      */

        n -= retval;            /* Keep track of how much of s is full     */
        pos += (retval - 1);    /* and where s ends                        */
        total += retval;        /* and how long s has become               */

        if (*pos == '\n') {     /* Did we get a full line of input?        */
            /*
             * Trim any extra trailing spaces or tabs except for the first
             * space or tab at the beginning of a blank string.  This makes
             * it much easier to check field values for exact matches, and
             * saves memory as well.  Terminate string at end of line.
             */
            while (pos > (s + 1) && (*(pos - 1) == ' ' || *(pos - 1) == '\t')) {
                --pos;          /* trim extra trailing spaces or tabs      */
                --total;        /* but not one at the beginning of line    */
                ++n;
            }
            *pos = '\0';
            --total;
            ++n;
        }
        else
            break;              /* if not, input line exceeded buffer size */

        /* Continue appending if line folding is desired and
         * the last line was not empty and we have room in the buffer and
         * the next line begins with a continuation character.
         */

    } while (fold && (retval != 1) && (n > 1)
             && (mbox_blookc(&next, in) == 1)
             && ((next == ' ') || (next == '\t')));

    return total;
}

/* This function will parse the BUFF * into a table*
 *
 * Adapted from http_protocol.c - get_mime_headers()
 */
static apr_table_t *load_mbox_mime_tables(request_rec *r, MBOX_BUFF *b)
{
    char field[DEFAULT_LIMIT_REQUEST_FIELDSIZE + 2];    /* getline's two extra */
    char *value;
    char *copy;
    int len;
    unsigned int fields_read = 0;
    apr_table_t *tmp_headers;

    /* We'll use ap_overlap_tables later to merge these into r->headers_in. */
    tmp_headers = apr_table_make(r->pool, 50);

    /*
     * Read header lines until we get the empty separator line, a read error,
     * the connection closes (EOF), reach the server limit, or we timeout.
     */
    while ((len = mbox_getline(field, sizeof(field), b, 1)) > 0) {

        if (r->server->limit_req_fields &&
            (++fields_read > r->server->limit_req_fields))
            continue;

        /* getline returns (size of max buffer - 1) if it fills up the
         * buffer before finding the end-of-line.  This is only going to
         * happen if it exceeds the configured limit for a field size.
         */
        if (len > r->server->limit_req_fieldsize)
            continue;

        copy = apr_palloc(r->pool, len + 1);
        memcpy(copy, field, len + 1);

        if (!(value = strchr(copy, ':')))       /* Find the colon separator */
            continue;           /* or skip the bad line     */

        *value = '\0';
        ++value;
        while (*value == ' ' || *value == '\t')
            ++value;            /* Skip to start of value   */

        apr_table_addn(tmp_headers, copy, value);
    }

    return tmp_headers;
}

/* Skips a line in the character array
 */
static void skipLine(MBOX_BUFF *b)
{
    char *tmp = memchr(b->b, LF, b->len - (b->b - b->sb));

    if (!tmp) {
        if (b->fd) {
            mbox_fillbuf(b);
            skipLine(b);
        }
        else
            b->b = 0;
    }
    else
        b->b = ++tmp;
}

/* Inserts the key and value into the MBOX_LIST arranged by the key value. */
static apr_status_t put_entry(request_rec *r, MBOX_LIST ** head,
                              apr_time_t t, void *value)
{
    MBOX_LIST *new = (MBOX_LIST *) apr_pcalloc(r->pool, sizeof(MBOX_LIST));

    new->next = *head;
    *head = new;

    new->key = t;

    /* Don't copy the value */
    new->value = value;

    return APR_SUCCESS;
}

/*
 * Comparison function called by sort_linked_list to sort MBOX_LIST items.
 */
static int mbox_compare_list(void *p, void *q, void *pointer)
{
    MBOX_LIST *a = (MBOX_LIST *) p;
    MBOX_LIST *b = (MBOX_LIST *) q;

    return ((a->key > b->key) ? 1 : -1);
}

static int mbox_reverse_list(void *p, void *q, void *pointer)
{
    MBOX_LIST *a = (MBOX_LIST *) p;
    MBOX_LIST *b = (MBOX_LIST *) q;

    return ((a->key > b->key) ? -1 : 1);
}

/*
 * Comparison function called by sort_linked_list to sort MBOX_LIST items
 * by author (then by date).
 */
static int mbox_compare_list_author(void *p, void *q, void *pointer)
{
    MBOX_LIST *a = (MBOX_LIST *) p;
    MBOX_LIST *b = (MBOX_LIST *) q;
    Message *c = (Message *) a->value;
    Message *d = (Message *) b->value;
    int cmp;

    if (!c->str_from) {
        return -1;
    }

    if (!d->str_from) {
        return 1;
    }

    cmp = strcmp(c->str_from, d->str_from);

    /* Match, sort by normal value */
    if (!cmp)
        return mbox_compare_list(p, q, pointer);

    return cmp;
}

/*
 * Uses the sort_linked_list function from mbox_sort (merge-sort).
 */
MBOX_LIST *mbox_sort_list(MBOX_LIST *l, int flags)
{
    switch (flags) {
    case MBOX_SORT_DATE:
        l = (MBOX_LIST *) mbox_sort_linked_list(l, 0, mbox_compare_list,
                                                NULL, NULL);
        break;
    case MBOX_SORT_REVERSE_DATE:
        l = (MBOX_LIST *) mbox_sort_linked_list(l, 0, mbox_reverse_list,
                                                NULL, NULL);
        break;
    case MBOX_SORT_AUTHOR:
        l = (MBOX_LIST *) mbox_sort_linked_list(l, 0,
                                                mbox_compare_list_author,
                                                NULL, NULL);
        break;
    }

    return l;
}

/*
 * Normalize the from header in the message to something we like.
 */
static void parse_from(request_rec *r, Message *m)
{
    char *startFrom, *endFrom;
    if (m->from) {
        /* Froms come in many shapes and forms.
         * Notably, we want to try and handle:
         * 1) "Bob Smith" <bsmith@example.com>
         * 2) Bob Smith <bsmith@example.com>
         * 3) <bsmith@example.com>
         * 4) bsmith@example.com
         * 5) bsmith@example.com (Bob Smith)
         */

        /* FIXME: Optimize string matching */
        startFrom = apr_pstrdup(r->pool, m->from);
        endFrom = strchr(startFrom, '"');
        if (endFrom) {          /* Case 1 */
            startFrom = ++endFrom;
            endFrom = strchr(startFrom, '"');
            if (endFrom)
                *endFrom = '\0';
        }
        else {
            endFrom = strchr(startFrom, '<');
            if (endFrom) {
                if (endFrom == startFrom) {     /* Case 3 */
                    endFrom = strchr(++startFrom, '>');
                    if (endFrom)
                        *endFrom = '\0';
                }
                else {          /* Case 2 */

                    /* FIXME: What about Bob Smith<bsmith@example.com>? */
                    *endFrom = '\0';
                    endFrom = strrchr(startFrom, ' ');
                    if (endFrom)
                        *endFrom = '\0';
                }
            }
            else {
                endFrom = strchr(startFrom, '(');
                if (endFrom) {  /* Case 5 */
                    startFrom = ++endFrom;
                    endFrom = strchr(startFrom, ')');
                    if (endFrom)
                        *endFrom = '\0';
                }

            }
        }
        m->str_from = startFrom;
    }
}

static void parse_references(request_rec *r, Message *m)
{
    char *startRef, *endRef;
    /* FIXME: Is table the right data structure here? */
    if (m->raw_ref) {
        m->references = apr_table_make(r->pool, 50);
        startRef = m->raw_ref;
        endRef = strchr(startRef, '>');
        while (endRef != NULL) {
            startRef = apr_pstrndup(r->pool, startRef, endRef - startRef + 1);
            apr_collapse_spaces(startRef, startRef);
            apr_table_setn(m->references, startRef, m->msgID);
            startRef = ++endRef;
            endRef = strchr(startRef, '>');
        }
    }
}

/* Normalizes all fields on the message after it has been loaded.
 */
static void normalize_message(request_rec *r, Message *m)
{
    apr_time_exp_t time_exp;
    apr_size_t len = 0;

    /* Clean up the from to hide email addresses if possible. */
    parse_from(r, m);

    /* Some morons don't provide subjects. */
    if (!m->subject || !*m->subject)
        m->subject = "[No Subject]";

    if (!m->content_type || !*m->content_type)
        m->content_type = "text/plain";

    if (m->charset && !*m->charset)
        m->charset = NULL;

    apr_time_exp_gmt(&time_exp, m->date);

    m->str_date = (char *) apr_pcalloc(r->pool, APR_RFC822_DATE_LEN);
    m->rfc822_date = (char *) apr_pcalloc(r->pool, APR_RFC822_DATE_LEN);

    apr_strftime(m->str_date, &len, APR_RFC822_DATE_LEN,
                 "%a, %d %b, %H:%M", &time_exp);
    apr_rfc822_date(m->rfc822_date, m->date);

    /* Parse the references into a table. */
    parse_references(r, m);
}


#define fetch_cstring(pool, dst, source, pos, xlen) \
    do { \
        memcpy(&xlen, source+pos, sizeof(xlen)); \
        pos += sizeof(xlen); \
        if (xlen == 0) { \
            dst = NULL; \
        } \
        else { \
            dst = apr_pmemdup(pool, source+pos, xlen); \
            pos += xlen; \
        } \
    } while(0);

#define sstrlen(str) (str ? strlen(str) + 1 : 0)

static apr_status_t fetch_msgc(apr_pool_t *pool, apr_dbm_t *database,
                               const char *key, mb_dbm_data *msgc)
{
    apr_datum_t msgKey, msgValue;
    int status;
    int pos = 0;
    apr_uint16_t tlen = 0;

    msgKey.dptr = (char *) key;
    msgKey.dsize = strlen(key) + 1;

    status = apr_dbm_fetch(database, msgKey, &msgValue);

    if (status != APR_SUCCESS || !msgValue.dptr || !msgValue.dsize) {
        /* TODO: Error out. */
        return APR_EGENERAL;
    }

    memcpy(&msgc->msg_start, msgValue.dptr + pos, sizeof(msgc->msg_start));
    pos += sizeof(msgc->msg_start);
    memcpy(&msgc->body_start, msgValue.dptr + pos, sizeof(msgc->body_start));
    pos += sizeof(msgc->body_start);
    memcpy(&msgc->body_end, msgValue.dptr + pos, sizeof(msgc->body_end));
    pos += sizeof(msgc->body_end);

    memcpy(&msgc->date, msgValue.dptr + pos, sizeof(msgc->date));
    pos += sizeof(msgc->date);

    memcpy(&msgc->cte, msgValue.dptr + pos, sizeof(msgc->cte));
    pos += sizeof(msgc->cte);

    fetch_cstring(pool, msgc->from, msgValue.dptr, pos, tlen);
    fetch_cstring(pool, msgc->subject, msgValue.dptr, pos, tlen);
    fetch_cstring(pool, msgc->references, msgValue.dptr, pos, tlen);
    fetch_cstring(pool, msgc->content_type, msgValue.dptr, pos, tlen);
    fetch_cstring(pool, msgc->charset, msgValue.dptr, pos, tlen);
    fetch_cstring(pool, msgc->boundary, msgValue.dptr, pos, tlen);

    return APR_SUCCESS;
}

#define store_cstring(source, dest, pos, xlen) \
    do { \
        xlen = sstrlen(source); \
        memcpy(dest+pos, &xlen, sizeof(xlen)); \
        pos += sizeof(xlen); \
        if (source) { \
            memcpy(dest+pos, source, xlen); \
            pos += xlen; \
        } \
    } while (0);

static apr_status_t store_msgc(apr_pool_t *pool, apr_dbm_t *database,
                               const char *key, mb_dbm_data *msgc,
                               const char *list, const char *domain)
{
    apr_datum_t msgKey, msgValue;
    int vlen;
    int pos = 0;
    apr_uint16_t tlen = 0;
    char *value;

    if (!database || !key || !msgc)
        return APR_EGENERAL;

    /**
    printf("Message Start: %"APR_OFF_T_FMT
           "\n\t Body Start: %"APR_OFF_T_FMT
           "\n\t Body End: %"APR_OFF_T_FMT
           "\n\t Header Size: %"APR_OFF_T_FMT
           "\n\t Body Size: %"APR_OFF_T_FMT
           "\n\t Message Size: %"APR_OFF_T_FMT"\n",
           msgc->msg_start, msgc->body_start, msgc->body_end,
           msgc->body_start-msgc->msg_start,
           msgc->body_end - msgc->body_start,
           msgc->body_end - msgc->msg_start);
    **/

    msgKey.dptr = (char *) key;
    /* We add one to the strlen to encompass the term null */
    msgKey.dsize = strlen(key) + 1;

    /* We store the entire structure in a single entry. */
    vlen = sizeof(msgc->msg_start) +
        sizeof(msgc->body_start) +
        sizeof(msgc->body_end) +
        sizeof(msgc->date) +
        sizeof(msgc->cte) +
        sstrlen(msgc->from) + sizeof(tlen) +
        sstrlen(msgc->subject) + sizeof(tlen) +
        sstrlen(msgc->references) + sizeof(tlen) +
        sstrlen(msgc->content_type) + sizeof(tlen) +
        sstrlen(msgc->charset) + sizeof(tlen) +
        sstrlen(msgc->boundary) + sizeof(tlen);

    value = apr_palloc(pool, vlen);

    memcpy(value + pos, &msgc->msg_start, sizeof(msgc->msg_start));
    pos += sizeof(msgc->msg_start);
    memcpy(value + pos, &msgc->body_start, sizeof(msgc->body_start));
    pos += sizeof(msgc->body_start);
    memcpy(value + pos, &msgc->body_end, sizeof(msgc->body_end));
    pos += sizeof(msgc->body_end);

    memcpy(value + pos, &msgc->date, sizeof(msgc->date));
    pos += sizeof(msgc->date);

    memcpy(value + pos, &msgc->cte, sizeof(msgc->cte));
    pos += sizeof(msgc->cte);

    store_cstring(msgc->from, value, pos, tlen);
    store_cstring(msgc->subject, value, pos, tlen);
    store_cstring(msgc->references, value, pos, tlen);
    store_cstring(msgc->content_type, value, pos, tlen);
    store_cstring(msgc->charset, value, pos, tlen);
    store_cstring(msgc->boundary, value, pos, tlen);

    msgValue.dptr = (char *) value;
    msgValue.dsize = pos;

    return apr_dbm_store(database, msgKey, msgValue);
}

/* This function is stolen from server/util.c, since we need to be able to run
 * standalone, without the httpd core... sigh. */
static void ex_ap_str_tolower(char *str)
{
    while (*str) {
        *str = apr_tolower(*str);
        ++str;
    }
}


/**
 * This function will generate the appropriate DBM for a given mbox file.
 */
apr_status_t mbox_generate_index(request_rec *r, apr_file_t *f,
                                 const char *list, const char *domain)
{
    apr_status_t status;
    apr_table_t *table;
    apr_dbm_t *msgDB;
    apr_pool_t *tpool;
#ifdef APR_HAS_MMAP
    apr_finfo_t fi;
#else
    char buf[HUGE_STRING_LEN + 1];
#endif
    MBOX_BUFF b;
    const char *temp, *msgID;
    mb_dbm_data msgc;

#ifdef APR_HAS_MMAP
    status = apr_file_name_get(&temp, f);

    if (status != APR_SUCCESS)
        return status;

    status = apr_stat(&fi, temp, APR_FINFO_SIZE, r->pool);

    if (status != APR_SUCCESS)
        return status;

    if (fi.size != (apr_size_t) fi.size)
        return APR_EGENERAL;

    if (fi.size == 0) {
        OPEN_DBM(r, msgDB, APR_DBM_RWCREATE, MSGID_DBM_SUFFIX, temp, status);
        apr_dbm_close(msgDB);
        return OK;
    }

    status =
        apr_mmap_create(&b.mm, f, 0, (apr_size_t) fi.size, APR_MMAP_READ,
                        r->pool);

    if (status != APR_SUCCESS)
        return status;
    b.sb = b.rb = b.b = b.mm->mm;
    b.len = b.mm->size;
    b.maxlen = b.mm->size;
    b.fd = 0;
#else
    buf[0] = '\0';
    b.sb = b.rb = b.b = buf;
    b.fd = f;
    b.maxlen = HUGE_STRING_LEN;
    b.len = 0;
#endif
    b.totalread = 0;

    OPEN_DBM(r, msgDB, APR_DBM_RWCREATE, MSGID_DBM_SUFFIX, temp, status);

    mbox_fillbuf(&b);

    msgID = NULL;
    apr_pool_create(&tpool, r->pool);

    /* When we reach the end of the file, b is NULL.  */
    while (b.b) {
#ifdef APR_HAS_MMAP
        msgc.body_end = b.b - b.sb;
        /* With mmap, we can hit a file that brings the From check to the very
         * end of the mmap region - hence a dangling pointer (likely SEGV).
         * Therefore, break out of the loop first.
         */
        if (msgc.body_end == b.maxlen) {
            break;
        }
#else
        msgc.body_end = b.totalread - b.len + b.b - b.rb;
#endif
        if (b.b[0] == 'F' && b.b[1] == 'r' &&
            b.b[2] == 'o' && b.b[3] == 'm' && b.b[4] == ' ') {
            /**
             * The updating of the index is delayed, until we have found
             * the next message.  This allows the 'current' message to konw
             * where it's body ended, without having to deal with 'peeking',
             * and then seeking backwards inside the file.
             */

            if (msgID) {
                store_msgc(tpool, msgDB, msgID, &msgc, list, domain);
                msgID = NULL;
            }
            apr_pool_clear(tpool);

#ifdef APR_HAS_MMAP
            msgc.msg_start = b.b - b.sb;
#else
            msgc.msg_start = b.totalread - b.len + b.b - b.rb;
#endif
            skipLine(&b);

            table = load_mbox_mime_tables(r, &b);

            /* Location is how much read total minus how much read this pass
             * plus the offset of our current position from the last place
             * we read from. */
            msgID = apr_table_get(table, "Message-ID");
            if (msgID) {
#ifdef APR_HAS_MMAP
                msgc.body_start = b.b - b.sb;
#else
                msgc.body_start = b.totalread - b.len + b.b - b.rb;
#endif
                /* TODO: Seek to the Body End */

                msgc.from = apr_table_get(table, "From");
                msgc.subject = apr_table_get(table, "Subject");
                temp = apr_table_get(table, "Date");
                if (temp) {
                    /* FIXME: Change this back to apr_date_parse_rfc()
                       as soon as it is fixed ! */
                    msgc.date = mbox_date_parse_rfc(temp);
                }
                else {
                    msgc.date = 0;
                }

                msgc.references = apr_table_get(table, "References");

                temp = apr_table_get(table, "Content-Transfer-Encoding");
                if (temp) {
                    char *p = apr_pstrdup(tpool, temp);
                    msgc.cte = mbox_parse_cte_header(p);
                }
                else {
                    msgc.cte = CTE_NONE;
                }

                temp = apr_table_get(table, "Content-Type");
                if (temp) {
                    char *p, *boundary, *dup, *charset;
                    dup = apr_pstrdup(tpool, temp);
                    boundary = mbox_strcasestr(dup, "boundary=");
                    charset = mbox_strcasestr(dup, "charset=");
                    if (boundary) {
                        boundary += strlen("boundary=");
                        if (boundary[0] == '"') {
                            ++boundary;
                            if ((p = strstr(boundary, "\""))) {
                                *p = '\0';
                            }
                        }
                        else {
                            if ((p = strstr(boundary, ";"))) {
                                *p = '\0';
                            }
                        }
                    }
                    if (charset) {
                        charset += strlen("charset=");
                        if (charset[0] == '"') {
                            ++charset;
                            if ((p = strstr(charset, "\""))) {
                                *p = '\0';
                            }
                        }
                        else {
                            if ((p = strstr(charset, ";"))) {
                                *p = '\0';
                            }
                        }
                    }
                    msgc.boundary = boundary;
                    msgc.charset = charset;
                    p = strstr(dup, ";");
                    if (p) {
                        *p = '\0';
                    }
                    /* Some old clients only sent 'text',
                     * instead of 'text/plain'. Lets try to be nice to them */
                    if (!strcasecmp(dup, "text")) {
                        msgc.content_type = "text/plain";
                    }
                    else {
                        /* Normalize the Content-Type */
                        ex_ap_str_tolower(dup);
                        msgc.content_type = dup;
                    }
                }
                else {
                    msgc.content_type = NULL;
                    msgc.boundary = NULL;
                    msgc.charset = NULL;
                }
            }
        }
        else {
            skipLine(&b);
        }
    }

    /**
     * The last message inside the .mbox file is now added to the cache.
     */
    if (msgID) {
        store_msgc(tpool, msgDB, msgID, &msgc, list, domain);
    }

    apr_pool_destroy(tpool);
    apr_dbm_close(msgDB);
#ifdef APR_HAS_MMAP
    apr_mmap_delete(b.mm);
#else
    /* If we aren't using MMAP, we relied on the open file passed in. */
#endif
    return OK;
}

/* This function returns a list of all messages contained within the mbox.
 * This information is stored within the DBMs, so this is fairly fast.
 */
MBOX_LIST *mbox_load_index(request_rec *r, apr_file_t *f, int *count)
{
    apr_status_t status;
    MBOX_LIST *head;
    apr_dbm_t *msgDB;
    apr_datum_t msgKey;
    char *temp;
    mb_dbm_data msgc;
    apr_pool_t *tpool;
    Message *curMsg;

    OPEN_DBM(r, msgDB, APR_DBM_READONLY, MSGID_DBM_SUFFIX, temp, status);

    if (status != APR_SUCCESS) {
        return NULL;
    }

    if (count) {
        *count = 0;
    }

    /* APR SDBM iteration is badly broken.  You can't skip around during
     * an iteration.  Fixing this would be nice.
     */
    head = 0;
    apr_pool_create(&tpool, r->pool);
    status = apr_dbm_firstkey(msgDB, &msgKey);
    while (msgKey.dptr != 0 && status == APR_SUCCESS) {
        /* Construct a new message */
        curMsg = (Message *) apr_pcalloc(r->pool, sizeof(Message));

        /* FIXME: When we evolve to MD5 hashes, switch this */
        curMsg->msgID = apr_pstrndup(r->pool, msgKey.dptr, msgKey.dsize);

        status = fetch_msgc(tpool, msgDB, curMsg->msgID, &msgc);

        if (status != APR_SUCCESS)
            break;

        curMsg->from = apr_pstrdup(r->pool, msgc.from);
        curMsg->subject = apr_pstrdup(r->pool, msgc.subject);
        curMsg->content_type = apr_pstrdup(r->pool, msgc.content_type);
        curMsg->charset = apr_pstrdup(r->pool, msgc.charset);
        curMsg->boundary = apr_pstrdup(r->pool, msgc.boundary);
        curMsg->date = msgc.date;
        curMsg->raw_ref = apr_pstrdup(r->pool, msgc.references);
        curMsg->msg_start = msgc.msg_start;
        curMsg->body_start = msgc.body_start;
        curMsg->body_end = msgc.body_end;
        curMsg->cte = msgc.cte;
        apr_pool_clear(tpool);

        /* Normalize the message and perform tweaks on it */
        normalize_message(r, curMsg);

        /* Store them in chronological order */
        put_entry(r, &head, curMsg->date, curMsg);

        status = apr_dbm_nextkey(msgDB, &msgKey);

        if (count) {
            (*count)++;
        }
    }

    apr_pool_destroy(tpool);
    apr_dbm_close(msgDB);

    return head;
}

/* This function returns the information about one particular message
 * that may or may not be in the DBM.  If it is not in the DBM index,
 * NULL will be returned.
 */
Message *mbox_fetch_index(request_rec *r, apr_file_t *f, const char *msgID)
{
    apr_status_t status;
    apr_dbm_t *msgDB;
    apr_datum_t msgKey;
    char *temp;
    Message *curMsg = NULL;
    mb_dbm_data msgc;

    /* If the message ID passed in is blank. */
    if (!msgID || *msgID == '\0')
        return NULL;

    OPEN_DBM(r, msgDB, APR_DBM_READONLY, MSGID_DBM_SUFFIX, temp, status);

    if (status != APR_SUCCESS)
        return NULL;

    msgKey.dptr = (char *) msgID;
    /* We add one to the strlen to encompass the term null */
    msgKey.dsize = strlen(msgID) + 1;

    /* Construct a new message */
    curMsg = (Message *) apr_pcalloc(r->pool, sizeof(Message));

    /* FIXME: When we evolve to MD5 hashes, switch this */
    curMsg->msgID = apr_pstrndup(r->pool, msgKey.dptr, msgKey.dsize);

    status = fetch_msgc(r->pool, msgDB, curMsg->msgID, &msgc);
    if (status != APR_SUCCESS)
        return NULL;

    curMsg->from = apr_pstrdup(r->pool, msgc.from);
    curMsg->subject = apr_pstrdup(r->pool, msgc.subject);
    curMsg->content_type = apr_pstrdup(r->pool, msgc.content_type);
    curMsg->charset = apr_pstrdup(r->pool, msgc.charset);
    curMsg->boundary = apr_pstrdup(r->pool, msgc.boundary);
    curMsg->date = msgc.date;
    curMsg->raw_ref = apr_pstrdup(r->pool, msgc.references);
    curMsg->msg_start = msgc.msg_start;
    curMsg->body_start = msgc.body_start;
    curMsg->body_end = msgc.body_end;
    curMsg->cte = msgc.cte;

    /* Normalize the message and perform tweaks on it */
    normalize_message(r, curMsg);

    apr_dbm_close(msgDB);

    return curMsg;
}


int mbox_msg_count(request_rec *r, char *path)
{
    apr_dbm_t *msgDB;
    int status;
    int count = 0;
    apr_datum_t msgKey;
    char *temp;

    temp = apr_pstrcat(r->pool, r->filename, path, MSGID_DBM_SUFFIX, NULL);
    status = apr_dbm_open_ex(&msgDB, APR_STRINGIFY(DBM_TYPE), temp, APR_DBM_READONLY,
                          APR_OS_DEFAULT, r->pool);

    if (status != APR_SUCCESS) {
        return -1;
    }

    /* FIXME: I think most DBMs have a faster method than
     *        iterating the keys..
     */
    status = apr_dbm_firstkey(msgDB, &msgKey);
    while (msgKey.dptr != 0 && status == APR_SUCCESS) {
        count++;
        status = apr_dbm_nextkey(msgDB, &msgKey);
    }

    apr_dbm_close(msgDB);

    return count;
}



static apr_table_t *fetch_first_headers(request_rec *r, apr_file_t *f)
{
    apr_status_t status;
    apr_table_t *table = NULL;
    MBOX_BUFF b;
#ifdef APR_HAS_MMAP
    apr_finfo_t fi;
    const char *temp;
#else
    char buf[HUGE_STRING_LEN + 1];
#endif

#ifdef APR_HAS_MMAP
    status = apr_file_name_get(&temp, f);

    if (status != APR_SUCCESS)
        return NULL;

    status = apr_stat(&fi, temp, APR_FINFO_SIZE, r->pool);

    if (status != APR_SUCCESS)
        return NULL;

    if (fi.size != (apr_size_t) fi.size)
        return NULL;

    /* FIXME: We do not need to mmap the entire file.
     *        Creating an MMAP is a slow operation on FreeBSD.
     *        Once created, it is faster than Linux...
     */
    status =
        apr_mmap_create(&b.mm, f, 0, (apr_size_t) fi.size, APR_MMAP_READ,
                        r->pool);

    if (status != APR_SUCCESS)
        return NULL;
    b.sb = b.rb = b.b = b.mm->mm;
    b.len = b.maxlen = b.mm->size;
    b.fd = 0;
#else
    buf[0] = '\0';
    b.sb = b.rb = b.b = buf;
    b.fd = f;
    b.maxlen = HUGE_STRING_LEN;
    b.len = 0;
#endif
    b.totalread = 0;

    mbox_fillbuf(&b);

    /* When we reach the end of the file, b is NULL.  */
    while (b.b) {
        if (b.b[0] == 'F' && b.b[1] == 'r' &&
            b.b[2] == 'o' && b.b[3] == 'm' && b.b[4] == ' ') {

            skipLine(&b);

            table = load_mbox_mime_tables(r, &b);
            break;
        }
    }

#ifdef APR_HAS_MMAP
    apr_mmap_delete(b.mm);
#endif
    return table;
}

/* Creates fake List-Post header from file generated by:
 *   printf '%s@%s\n' > PATH/.listname
 */
static char *read_listname(char *path, apr_pool_t *p)
{
    apr_status_t rv;
    apr_file_t *f;
    char buf[256];
    apr_size_t nread;

    rv = apr_file_open(&f,
                       apr_pstrcat(p, path, "/.listname", (char*)NULL),
                       APR_READ, APR_OS_DEFAULT, p);
    if (rv != APR_SUCCESS) {
        return NULL;
    }

    rv = apr_file_read_full(f, buf, sizeof(buf) - 1, &nread);
    if (rv != APR_EOF) {
        return NULL;
    }

    buf[nread] = '\0';
    if (nread > 0 && buf[--nread] == '\n') {
        buf[nread] = '\0';
    }
    apr_file_close(f);
    return apr_pstrcat(p, "<mailto:", buf, ">", NULL);
}

static const char *get_list_post(request_rec *r, char *path, char *fullpath)
{
    apr_status_t rv;
    apr_file_t *f;
    apr_table_t *headers;

    rv = apr_file_open(&f, fullpath, APR_READ, APR_OS_DEFAULT, r->pool);

    if (rv != APR_SUCCESS) {
        return NULL;
    }

    headers = fetch_first_headers(r, f);
    apr_file_close(f);

    if (headers == NULL) {
        return NULL;
    }

    return apr_table_get(headers, "List-Post");
}

char *mbox_get_list_post(request_rec *r, char *path)
{
    char *fullpath = apr_pstrcat(r->pool, r->filename, path, NULL);
    const char *list_post = get_list_post(r, path, fullpath);

    if (list_post != NULL)
        return apr_pstrdup(r->pool, list_post);
    return read_listname(dirname(fullpath), r->pool);
}

/**
 * List of all C-T-E Types found on httpd-dev and FreeBSD-current:
 *
 * Content-Transfer-Encoding:      8bit
 * Content-Transfer-Encoding:  7bit
 * Content-Transfer-Encoding: 7BIT
 * Content-Transfer-Encoding: 7Bit
 * Content-Transfer-Encoding: 7bit
 * Content-Transfer-Encoding: 8-bit
 * Content-Transfer-Encoding: 8BIT
 * Content-Transfer-Encoding: 8Bit
 * Content-Transfer-Encoding: 8bit
 * Content-Transfer-Encoding: BASE64
 * Content-Transfer-Encoding: BINARY
 * Content-Transfer-Encoding: Base64
 * Content-Transfer-Encoding: QUOTED-PRINTABLE
 * Content-Transfer-Encoding: Quoted-Printable
 * Content-Transfer-Encoding: base64
 * Content-Transfer-Encoding: binary
 * Content-Transfer-Encoding: none
 * Content-Transfer-Encoding: quoted-printable
 * Content-Transfer-Encoding: x-uuencode
 * Content-Transfer-Encoding:7bit
 * Content-Transfer-Encoding:quoted-printable
 *
 * This is why we have RFCs.
 */

mbox_cte_e mbox_parse_cte_header(char *src)
{
    ex_ap_str_tolower(src);
    if (strstr(src, "bi")) {
        if (strchr(src, '7')) {
            return CTE_7BIT;
        }
        else if (strchr(src, '8')) {
            return CTE_8BIT;
        }
        else if (strchr(src, 'y')) {
            return CTE_BINARY;
        }
    }
    else if (strchr(src, '6')) {
        return CTE_BASE64;
    }
    else if (strchr(src, 'q')) {
        return CTE_QP;
    }
    else if (strchr(src, 'u')) {
        return CTE_UUENCODE;
    }

    return CTE_NONE;
}
