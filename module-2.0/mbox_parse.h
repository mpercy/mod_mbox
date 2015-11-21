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

#ifndef MBOX_PARSE_H
#define MBOX_PARSE_H

/*
 * Data structures and header files needed for mbox_parse.c
 */

#define CORE_PRIVATE
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"

#include "apr.h"
#include "apr_strings.h"
#include "apr_mmap.h"

#include <stdio.h>

#define MBOX_SORT_DATE   0
#define MBOX_SORT_AUTHOR 1
#define MBOX_SORT_THREAD 2
#define MBOX_SORT_REVERSE_DATE 3

/*
 * MBOX_BUFF emulates the Apache BUFF structure, however it only
 * has a string inside of it, not a socket.
 *
 * sb is the original base of the char * pointer.
 * rb is the pointer to where the next write should go to
 * b  is the pointer to where the next read should come from
 * fd is the pointer to the apr_file_t which contains the original data source
 * mm is the pointer to the apr_mmap_t which contains the original data source
 * maxlen contains the maximum allocatable length of sb
 * len contains the number of characters we have just read
 * totalread contains the number of characters we have finished reading
 */
typedef struct mbox_buff_struct MBOX_BUFF;

struct mbox_buff_struct
{
    char *sb, *rb, *b;
    apr_size_t maxlen, len, totalread;
    apr_file_t *fd;
#ifdef APR_HAS_MMAP
    apr_mmap_t *mm;
#endif
};

/*
 * MBOX_LIST is a generic linked list node.
 * key is the value associated with the node
 * value is the value associated with the key
 * next is the next item in the list
 */
typedef struct mbox_list_struct MBOX_LIST;

struct mbox_list_struct
{
    MBOX_LIST *next;
    apr_time_t key;
    void *value;
};

/*
 * All possible Content-Transfer-Encodings.
 */
typedef enum
{
    CTE_NONE = 0,
    CTE_7BIT = 1,
    CTE_8BIT = 2,
    CTE_UUENCODE = 3,
    CTE_BINARY = 4,
    CTE_QP = 5,
    CTE_BASE64 = 6,
} mbox_cte_e;

mbox_cte_e mbox_parse_cte_header(char *src);

/* The following is based on Jamie Zawinski's description of the Netscape 3.x
 * threading algorithm at <http://www.jwz.org/doc/threading.html>.
 */

/* As JWZ says, this shouldn't be char*, but something that could be
 * derived from an MD5 hash.  That can come later.
 */
typedef char *ID;

/* Typedefs for Message and Container */
typedef struct Message_Struct Message;
typedef struct Container_Struct Container;

typedef struct mbox_mime_message
{
    char *body;
    apr_size_t body_len;
    char *boundary;

    char *content_type;
    char *charset;
    char *content_encoding;
    char *content_disposition;
    char *content_name;
    mbox_cte_e cte;

    struct mbox_mime_message **sub;
    unsigned int sub_count;
} mbox_mime_message_t;

/* The basic information about a message.  */
struct Message_Struct
{
    ID msgID;

    char *from;
    char *str_from;

    char *subject;

    apr_time_t date;
    char *str_date;
    char *rfc822_date;

    char *content_type;
    char *charset;
    char *boundary;
    mbox_cte_e cte;

    apr_table_t *references;
    char *raw_ref;

    apr_off_t msg_start;
    apr_off_t body_start;
    apr_off_t body_end;

    char *raw_msg;
    char *raw_body;

    mbox_mime_message_t *mime_msg;
};

/* The threading information about a message. */
struct Container_Struct
{
    Message *message;
    Container *parent;          /* Only one parent */
    Container *child;           /* Many children */
    Container *next;            /* Many siblings */
};

/*
 * Fills the MBOX_BUFF with data from the backing store.
 */
void mbox_fillbuf(MBOX_BUFF *fb);

/*
 * Reads a line of protocol input.
 */
int mbox_getline(char *s, int n, MBOX_BUFF *in, int fold);

/*
 * Sorts a list of MBOX_LIST items by the specified order.
 */
MBOX_LIST *mbox_sort_list(MBOX_LIST *l, int sortFlags);

/*
 * Generates the DBM file.
 */
apr_status_t mbox_generate_index(request_rec *r, apr_file_t *f,
                                 const char *list, const char *domain);

/*
 * Returns a list of Messages
 */
MBOX_LIST *mbox_load_index(request_rec *r, apr_file_t *f, int *count);

/*
 * Returns a single message based on message ID
 */
Message *mbox_fetch_index(request_rec *r, apr_file_t *f, const char *msgID);

/*
 * Get the total message count for a file.
 */
int mbox_msg_count(request_rec *r, char *path);

char *mbox_get_list_post(request_rec *r, char *path);

#endif
