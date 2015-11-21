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
 * This provides for caching all of the basic information about a mailing list.
 * This includes the list name, the domain, and the message count for each month.
 */
#include "mbox_cache.h"
#include "mbox_dbm.h"

#define LIST_DB_NAME "listinfo.db"

#define OPEN_DBM(pool, mli, flags, path, temp, status) \
    temp = apr_pstrcat(pool, path, "/", LIST_DB_NAME, NULL); \
    mli = apr_palloc(pool, sizeof(mbox_cache_info)); \
    status = apr_dbm_open_ex(&mli->db, APR_STRINGIFY(DBM_TYPE), temp, flags, APR_OS_DEFAULT, pool);

static char *str_cache_version = "_cache_version";
static char *str_cache_mtime = "_cache_mtime";
static char *str_cache_list = "_cache_list";
static char *str_cache_domain = "_cache_domain";

static apr_status_t mli_cleanup(void *mlix)
{
    mbox_cache_info *mli = (mbox_cache_info *) mlix;
    apr_dbm_close(mli->db);
    return APR_SUCCESS;
}

#if 0
/* handy debugging function */
static void dump_dbm(apr_dbm_t *d)
{
    apr_status_t status;
    apr_datum_t key;
    status = apr_dbm_firstkey(d, &key);
    while (key.dptr != 0 && status == APR_SUCCESS) {
        /* FIXME: Assumes that keys are NULL terminated char. */
        printf("key: %s\n", key.dptr);
        status = apr_dbm_nextkey(d, &key);
    }
}
#endif

APR_DECLARE(void) mbox_cache_close(mbox_cache_info *mlix)
{
    apr_pool_cleanup_kill(mlix->pool, (void *) mlix, mli_cleanup);
    mli_cleanup(mlix);
}

APR_DECLARE(apr_status_t)
    mbox_cache_update(mbox_cache_info ** mlix,
                  const char *path, apr_pool_t *pool,
                  char *list, char *domain)
{
    apr_status_t rv;
    char *temp;
    apr_datum_t key;
    apr_datum_t nv;
    mbox_cache_info *mli;
    int update_only = 0;
    int tver;

    OPEN_DBM(pool, mli, APR_DBM_READWRITE, path, temp, rv);

    if (rv != APR_SUCCESS) {
        OPEN_DBM(pool, mli, APR_DBM_RWCREATE, path, temp, rv);

        mli->mtime = 0;
        if (rv != APR_SUCCESS) {
            return rv;
        }
    }
    else {
        update_only = 1;
    }

    mli->pool = pool;
    apr_pool_cleanup_register(pool, (void *) mli, mli_cleanup,
                              apr_pool_cleanup_null);

    key.dptr = str_cache_version;
    key.dsize = strlen(str_cache_version) + 1;
    tver = MBOX_CACHE_VERSION;

    temp = apr_palloc(pool, sizeof(tver));
    memcpy(temp, &tver, sizeof(tver));

    nv.dptr = temp;
    nv.dsize = sizeof(tver);
    rv = apr_dbm_store(mli->db, key, nv);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    if (update_only == 1) {
        key.dptr = str_cache_mtime;
        key.dsize = strlen(str_cache_mtime) + 1;
        rv = apr_dbm_fetch(mli->db, key, &nv);

        if (rv != APR_SUCCESS) {
            apr_dbm_close(mli->db);
            return rv;
        }
        if (nv.dptr && nv.dsize == sizeof(mli->mtime)) {
            memcpy(&mli->mtime, nv.dptr, sizeof(mli->mtime));
        }
        else {
            mli->mtime = 0;
        }
    }
    else {
        mli->mtime = 0;
    }

    key.dptr = str_cache_list;
    key.dsize = strlen(str_cache_list) + 1;
    nv.dptr = list;
    nv.dsize = strlen(list) + 1;
    rv = apr_dbm_store(mli->db, key, nv);
    if (rv != APR_SUCCESS) {
        return rv;
    }
    mli->domain = apr_pstrdup(pool, list);

    key.dptr = str_cache_domain;
    key.dsize = strlen(str_cache_domain) + 1;
    nv.dptr = domain;
    nv.dsize = strlen(domain) + 1;
    rv = apr_dbm_store(mli->db, key, nv);

    if (rv != APR_SUCCESS) {
        return rv;
    }
    mli->domain = apr_pstrdup(pool, domain);

    *mlix = mli;

    return rv;
}

APR_DECLARE(apr_status_t)
    mbox_cache_get(mbox_cache_info ** mlix, const char *path, apr_pool_t *p)
{
    apr_status_t rv;
    char *temp;
    apr_datum_t key;
    apr_datum_t nv;
    int tver;
    mbox_cache_info *mli;

    OPEN_DBM(p, mli, APR_DBM_READONLY, path, temp, rv);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    mli->pool = p;

    apr_pool_cleanup_register(p, (void *) mli, mli_cleanup,
                              apr_pool_cleanup_null);

    key.dptr = str_cache_version;
    key.dsize = strlen(str_cache_version) + 1;

    rv = apr_dbm_fetch(mli->db, key, &nv);

    if (rv != APR_SUCCESS) {
        apr_dbm_close(mli->db);
        return rv;
    }

    memcpy(&tver, nv.dptr, sizeof(tver));

    if (tver != MBOX_CACHE_VERSION) {
        apr_dbm_close(mli->db);
        return 1;
    }
    mli->version = tver;

    key.dptr = str_cache_mtime;
    key.dsize = strlen(str_cache_mtime) + 1;

    rv = apr_dbm_fetch(mli->db, key, &nv);

    if (rv != APR_SUCCESS) {
        apr_dbm_close(mli->db);
        return rv;
    }
    memcpy(&mli->mtime, nv.dptr, sizeof(mli->mtime));

    key.dptr = str_cache_list;
    key.dsize = strlen(str_cache_list) + 1;
    rv = apr_dbm_fetch(mli->db, key, &nv);
    if (rv != APR_SUCCESS) {
        apr_dbm_close(mli->db);
        return rv;
    }
    mli->list = apr_pstrdup(p, nv.dptr);

    key.dptr = str_cache_domain;
    key.dsize = strlen(str_cache_domain) + 1;
    rv = apr_dbm_fetch(mli->db, key, &nv);
    if (rv != APR_SUCCESS) {
        apr_dbm_close(mli->db);
        return rv;
    }
    mli->domain = apr_pstrdup(p, nv.dptr);

    *mlix = mli;

    return rv;
}

APR_DECLARE(apr_status_t)
    mbox_cache_get_count(mbox_cache_info *mli, int *count, char *path)
{
    apr_status_t rv = APR_SUCCESS;
    apr_datum_t key;
    apr_datum_t nv;

    key.dptr = path;
    key.dsize = strlen(path) + 1;

    rv = apr_dbm_fetch(mli->db, key, &nv);

    if (rv == APR_SUCCESS && nv.dsize == sizeof(int)) {
        memcpy(count, nv.dptr, sizeof(int));
    }
    else {
        *count = 0;
    }
    return rv;
}

APR_DECLARE(apr_status_t)
    mbox_cache_set_count(mbox_cache_info *mli, int count, char *path)
{
    apr_status_t rv = APR_SUCCESS;
    char v[sizeof(int)];
    apr_datum_t key;
    apr_datum_t nv;

    key.dptr = path;
    key.dsize = strlen(path) + 1;

    memcpy(v, &count, sizeof(count));
    nv.dptr = v;
    nv.dsize = sizeof(v);

    rv = apr_dbm_store(mli->db, key, nv);

    return rv;
}


APR_DECLARE(apr_status_t)
    mbox_cache_touch(mbox_cache_info *mli)
{
    apr_status_t rv = APR_SUCCESS;
    char v[sizeof(apr_time_t)];
    apr_datum_t key;
    apr_datum_t nv;

    key.dptr = str_cache_mtime;
    key.dsize = strlen(str_cache_mtime) + 1;

    memcpy(v, &mli->mtime, sizeof(mli->mtime));
    nv.dptr = v;
    nv.dsize = sizeof(mli->mtime);

    rv = apr_dbm_store(mli->db, key, nv);
    /* dump_dbm(mli->db); */
    return rv;
}
