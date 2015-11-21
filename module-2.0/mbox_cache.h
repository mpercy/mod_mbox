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

#ifndef MBOX_CACHE_H
#define MBOX_CACHE_H

/*
 * Data structures and header files needed for mbox_cache.c
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
#include "apr_pools.h"
#include "apr_strings.h"
#include "apr_dbm.h"

#include <stdio.h>

#define MBOX_CACHE_VERSION 0x2

/**
 * Supported Mailing List Managers
 *
 * Changes how directions are given to subscribe.
 */
typedef enum mbox_mlist_manager_e
{
    MANAGER_EZMLM = 0,
} mbox_mlist_manager_e;

typedef struct mbox_cache_info
{
    int version;
    apr_time_t mtime;
    mbox_mlist_manager_e type;
    const char *list;
    const char *domain;
    apr_dbm_t *db;
    apr_pool_t *pool;
} mbox_cache_info;

APR_DECLARE(void) mbox_cache_close(mbox_cache_info *mli);

APR_DECLARE(apr_status_t)
    mbox_cache_update(mbox_cache_info ** mli,
                  const char *path, apr_pool_t *p, char *list, char *domain);

APR_DECLARE(apr_status_t)
    mbox_cache_get(mbox_cache_info ** mli, const char *path, apr_pool_t *p);

APR_DECLARE(apr_status_t)
    mbox_cache_get_count(mbox_cache_info *mli, int *count, char *path);

APR_DECLARE(apr_status_t)
    mbox_cache_set_count(mbox_cache_info *mli, int count, char *path);

APR_DECLARE(apr_status_t)
    mbox_cache_touch(mbox_cache_info *mli);

#endif
