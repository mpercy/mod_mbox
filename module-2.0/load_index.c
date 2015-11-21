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

#define CORE_PRIVATE
#include "httpd.h"
#include "apr_pools.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "mbox_parse.h"

int main(int argc, char **argv)
{
    request_rec r;
    server_rec s;
    apr_file_t *f;
    apr_status_t status;
    MBOX_LIST *l;
    Message *m;

    if (argc <= 1) {
        puts("Please give me a filename to generate an index for.\n");
        return EXIT_FAILURE;
    }

    status = EXIT_SUCCESS;

    apr_initialize();
    atexit(apr_terminate);

    r.server = &s;
    s.limit_req_fieldsize = DEFAULT_LIMIT_REQUEST_FIELDSIZE;
    s.limit_req_fields = DEFAULT_LIMIT_REQUEST_FIELDS;

    apr_pool_create(&r.pool, NULL);

    r.filename = apr_pstrdup(r.pool, argv[1]);
    if ((status = apr_file_open(&f, r.filename, APR_READ, APR_OS_DEFAULT,
                                r.pool)) != APR_SUCCESS)
        return status;

    l = load_index(&r, f);
    while (l) {
        m = (Message *) l->value;
        printf("From: %s\n", m->from);
        l = l->next;
    }

    return EXIT_SUCCESS;
}
