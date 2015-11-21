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

#include "mod_mbox.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

/* Fetch a message from mailbox */
Message *fetch_message(request_rec *r, apr_file_t *f, char *msgID)
{
    apr_size_t len = 0;
    Message *m;

    /* Fetch message from mbox backend */
    m = mbox_fetch_index(r, f, msgID);
    if (!m) {
        return NULL;
    }

    r->mtime = m->date;
    ap_set_last_modified(r);

    /* Fetch message (from msg_start to body_end) */
    if (apr_file_seek(f, APR_SET, &m->msg_start) != APR_SUCCESS) {
        return NULL;
    }

    len = m->body_end - m->msg_start;

    /* If message len is invalid or nul, return immediately with NULL
       values */
    if (!len) {
        m->raw_msg = NULL;
        m->raw_body = NULL;

        return m;
    }

    m->raw_msg = apr_palloc(r->pool, len + 1);

    if (apr_file_read_full(f, m->raw_msg, len, &len) != APR_SUCCESS) {
        return NULL;
    }

    m->raw_msg[len] = '\0';
    m->raw_body = m->raw_msg + (m->body_start - m->msg_start);

    return m;
}

/* Fetch a message from mailbox */
void load_message(apr_pool_t *p, apr_file_t *f, Message *m)
{
    apr_size_t len;

    /* Fetch message (from msg_start to body_end) */
    if (apr_file_seek(f, APR_SET, &m->msg_start) != APR_SUCCESS) {
        return;
    }

    len = m->body_end - m->msg_start;
    m->raw_msg = apr_palloc(p, len + 1);

    if (apr_file_read_full(f, m->raw_msg, len, &len) != APR_SUCCESS) {
        return;
    }

    m->raw_msg[len] = '\0';
    m->raw_body = m->raw_msg + (m->body_start - m->msg_start);
}

/* Find thread starting with message 'msgID' in container 'c' */
static Container *find_thread(request_rec *r, char *msgID, Container *c)
{
    Container *next = NULL;

    if (c->message && strcmp(msgID, c->message->msgID) == 0)
        return c;

    if (c->child)
        next = find_thread(r, msgID, c->child);

    if (!next && c->next)
        next = find_thread(r, msgID, c->next);

    return next;
}

static Container *find_prev_thread(request_rec *r, char *msgID,
                                   Container *c)
{
    Container *next = NULL;

    /* Don't go any further */
    if (c->message && strcmp(msgID, c->message->msgID) == 0)
        return NULL;

    if (c->child) {
        next = (!strcmp(msgID, c->child->message->msgID) ? c :
                find_prev_thread(r, msgID, c->child));
    }

    if (!next && c->next) {
        /* Root set potentially does not have message */
        if (c->next->message && strcmp(msgID, c->next->message->msgID) == 0) {
            if (c->message) {
                next = c;
            }
            else {
                next = c->child;
            }
        }
        else {
            /* Message did not match. */
            if (!c->next->message && c->next->child &&
                strcmp(msgID, c->next->child->message->msgID) == 0) {
                if (c->message) {
                    next = c;
                }
                else {
                    next = c->child;
                }
            }
            else {
                next = find_prev_thread(r, msgID, c->next);
            }
        }
    }

    return next;
}

static Container *find_next_thread(request_rec *r, char *msgID,
                                   Container *c)
{
    c = find_thread(r, msgID, c);

    if (!c)
        return NULL;

    if (c->child)
        return c->child;

    /* Root elements don't have parents */
    if (c->next && c->parent)
        return c->next;

    /* We are at the end of this level, so let's go up levels until we
       find a next message. */
    while (c->parent) {
        c = c->parent;

        /* This node must have a parent as well, otherwise we need to
         * enter the root node checks below.
         */
        if (c->next && c->parent)
            return c->next;
    }

    /* Allow skipping to non-related root nodes.  This makes for a
       better browsing experience.  However, if a root node doesn't
       have a message, we need to return its first child. */
    if (c->next) {
        if (c->next->message)
            return c->next;

        return c->next->child;
    }

    return NULL;
}

/* Return an array of 4 strings : the prev, next, prev by thread an
 * next by thread msgIDs relative to the given msgID.
 *
 * FIXME: not working very well, must investigate!
 */
char **fetch_context_msgids(request_rec *r, apr_file_t *f, char *msgID)
{
    MBOX_LIST *head, *prev = NULL;
    Container *threads, *c;

    char **context = apr_pcalloc(r->pool, 4 * sizeof(char *));

    head = mbox_load_index(r, f, NULL);

    /* Compute threads before touching 'head' */
    threads = calculate_threads(r->pool, head);

    /* First, set the MBOX_PREV and MBOX_NEXT IDs */
    head = mbox_sort_list(head, MBOX_SORT_DATE);
    while (head && strcmp(msgID, ((Message *) (head->value))->msgID) != 0) {
        prev = head;
        head = head->next;
    }

    if (prev) {
        context[0] = ((Message *) (prev->value))->msgID;
    }

    if (head && head->next) {
        context[1] = ((Message *) (head->next->value))->msgID;
    }

    /* And the MBOX_PREV_THREAD and MBOX_NEXT_THREAD ones */
    if (threads) {
        c = find_prev_thread(r, msgID, threads);

        if (c && c->message) {
            context[2] = c->message->msgID;
        }

        c = find_next_thread(r, msgID, threads);
        if (c && c->message) {
            context[3] = c->message->msgID;
        }
    }

    return context;
}

/* The return value instructs the caller concerning what happened and what to
 * do next:
 *  OK ("we did our thing")
 *  DECLINED ("this isn't something with which we want to get involved")
 *  HTTP_mumble ("an error status should be reported")
 */
int mbox_file_handler(request_rec *r)
{
    apr_file_t *f;
    apr_finfo_t fi;
    apr_status_t status;

    /* Only get involved in our requests:
       r->handler == null or
       r->handler != MBOX_MAGIC_TYPE or
       r->handler != MBOX_HANDLER */
    if (!r->handler ||
        (strcmp(r->handler, MBOX_MAGIC_TYPE) &&
         strcmp(r->handler, MBOX_HANDLER))) {
        return DECLINED;
    }

    /* Only allow GETs */
    r->allowed |= (AP_METHOD_BIT << M_GET);
    if (r->method_number != M_GET) {
        return HTTP_METHOD_NOT_ALLOWED;
    }

    /* Make sure file exists - Allows us to give NOT_FOUND */
    if ((apr_stat(&fi, r->filename, APR_FINFO_TYPE, r->pool)) != APR_SUCCESS) {
        return HTTP_NOT_FOUND;
    }

    /* Allow the core to handle this... */
    if (!r->path_info || r->path_info[0] == '\0') {
        r->handler = "default-handler";
        return DECLINED;
    }

    /* Required? */
    /* Ideally, we'd like to make sure this is a valid subpath */
    if (r->path_info[0] != '/') {
        return HTTP_BAD_REQUEST;
    }

    /* Open the file */
    if ((status = apr_file_open(&f, r->filename, APR_READ,
                                APR_OS_DEFAULT, r->pool)) != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, status, r,
                      "file permissions deny server access: %s", r->filename);
        return HTTP_FORBIDDEN;
    }

    /* AJAX requests return XML */
    if (strncmp(r->path_info, "/ajax", 5) == 0) {
        /* Set content type */
        ap_set_content_type(r, "application/xml");

        if (r->header_only) {
            return OK;
        }

        if (strcmp(r->path_info, "/ajax/boxlist") == 0)
            status = mbox_xml_boxlist(r);

        else if (strcmp(r->path_info, "/ajax/thread") == 0)
            status = mbox_xml_msglist(r, f, MBOX_SORT_THREAD);
        else if (strcmp(r->path_info, "/ajax/author") == 0)
            status = mbox_xml_msglist(r, f, MBOX_SORT_AUTHOR);
        else if (strcmp(r->path_info, "/ajax/date") == 0)
            status = mbox_xml_msglist(r, f, MBOX_SORT_DATE);
        else
            status = mbox_xml_message(r, f);
    }
    else if (strncmp(r->path_info, "/raw", 4) == 0) {
        status = mbox_raw_message(r, f);
    }
    else {
        /* Set content type */
        ap_set_content_type(r, "text/html");

        if (r->header_only) {
            return OK;
        }

        if (strcmp(r->path_info, "/browser") == 0)
            status = mbox_ajax_browser(r);

        else if (strcmp(r->path_info, "/thread") == 0)
            status = mbox_static_msglist(r, f, MBOX_SORT_THREAD);
        else if (strcmp(r->path_info, "/author") == 0)
            status = mbox_static_msglist(r, f, MBOX_SORT_AUTHOR);
        else if (strcmp(r->path_info, "/date") == 0)
            status = mbox_static_msglist(r, f, MBOX_SORT_DATE);

        else
            status = mbox_static_message(r, f);
    }

    /* Close the file - don't let its status interfere with our request */
    apr_file_close(f);

    return status;
}
