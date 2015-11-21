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

/* This is a module which will present an mbox file stored locally to the
 * world in a nice format.
 *
 * This Apache-2.x module is dependent upon mbox_parse.c and mbox_thread.c.
 * Most of the logic for storing and retrieving data about the mbox is
 * located in mbox_parse.c.  The logic for determining threading information
 * is located in mbox_thread.c.
 *
 * All presentation logic for the mbox is stored here in the module.
 *
 * By placing the appropriate AddHandler configuration in httpd.conf
 * (mbox-handler), you can then access the list of messages of any mbox
 * file by accessing it like the following:
 * http://www.example.com/foo/bar.mbox/
 *
 * Direct link to raw messages are also available:
 * http://www.example.com/foo/bar.mbox/raw?%3c12345@example.com%3e
 *
 * Please note that the actual indexing of the messages does not occur
 * here in the module, but in the "generate_index" standalone program.
 */

#include "mod_mbox.h"

#include "apr_lib.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mbox);
#endif

typedef struct {
    const char *base_uri;
    const char *base_path;
    const char *base_name;
} mbox_req_cfg_t;

/* Register module hooks.
 */
static void mbox_register_hooks(apr_pool_t *p)
{
    ap_hook_handler(mbox_file_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(mbox_index_handler, NULL, NULL, APR_HOOK_FIRST);
}

/* Module configuration management.
 */
static void *mbox_create_dir_config(apr_pool_t *p, char *x)
{
    mbox_dir_cfg_t *conf;

    conf = apr_pcalloc(p, sizeof(mbox_dir_cfg_t));

    conf->enabled = 0;
    conf->hide_empty = 1;
    conf->antispam = 1;
    conf->root_path = NULL;
    conf->style_path = NULL;
    conf->script_path = NULL;

    return conf;
}

static void *mbox_merge_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    mbox_dir_cfg_t *from = (mbox_dir_cfg_t *) basev;
    mbox_dir_cfg_t *merge = (mbox_dir_cfg_t *) addv;
    mbox_dir_cfg_t *to;

    to = apr_palloc(p, sizeof(mbox_dir_cfg_t));

    /* Update 'enabled' */
    if (merge->enabled == 1) {
        to->enabled = 1;
    }
    else {
        to->enabled = from->enabled;
    }

    /* Update 'hide_empty' */
    if (merge->hide_empty == 1) {
        to->hide_empty = 1;
    }
    else {
        to->hide_empty = from->hide_empty;
    }

    /* Update 'antispam' */
    if (merge->antispam == 1) {
        to->antispam = 1;
    }
    else {
        to->antispam = from->antispam;
    }

    /* Update 'root_path' */
    if (merge->root_path != NULL) {
        to->root_path = apr_pstrdup(p, merge->root_path);
    }
    else if (from->root_path != NULL) {
        to->root_path = apr_pstrdup(p, from->root_path);
    }
    else {
        to->root_path = NULL;
    }

    /* Update 'style_path' */
    if (merge->style_path != NULL) {
        to->style_path = apr_pstrdup(p, merge->style_path);
    }
    else if (from->style_path != NULL) {
        to->style_path = apr_pstrdup(p, from->style_path);
    }
    else {
        to->style_path = NULL;
    }

    /* Update 'style_path' */
    if (merge->script_path != NULL) {
        to->script_path = apr_pstrdup(p, merge->script_path);
    }
    else if (from->script_path != NULL) {
        to->script_path = apr_pstrdup(p, from->script_path);
    }
    else {
        to->script_path = NULL;
    }

    return to;
}

/* Wrap text to MBOX_WRAP_TO. Changes passed string. */
char *mbox_wrap_text(char *str)
{
    int i, pos;
    apr_size_t len;

    if (!str)
        return NULL;

    len = strlen(str);

    /* Don't wrap messages with a size larger than MBOX_MAX_WRAP or
       smaller than a line length. */
    if ((len < MBOX_WRAP_TO) || (len > MBOX_MAX_WRAP))
        return str;

    for (i = 0, pos = 0; i < len; i++, pos++) {
        /* Reset the position counter if we pass a newline character */
        if (str[i] == '\n') {
            pos = 0;
        }

        /* If the position counter is after the wrap limit, wrap text at
           first space available */
        if ((pos >= MBOX_WRAP_TO) && ((str[i] == ' ') || (str[i] == '\t'))) {
            str[i] = '\n';
            pos = 0;
        }
    }

    return str;
}

/* Escape control chars */
char *mbox_cntrl_escape(apr_pool_t *p, char *s)
{
    int i, j;
    char *x;

    /* first, count the number of extra characters */
    for (i = 0, j = 0; s[i] != '\0'; i++)
        if (apr_iscntrl(s[i]))
            j += 5;

    if (j == 0)
        return s;

    x = apr_palloc(p, i + j + 1);
    for (i = 0, j = 0; s[i] != '\0'; i++, j++) {
        if (apr_iscntrl(s[i])) {
            snprintf(&x[j], 7, "&#%3.3d;", (unsigned char)s[i]);
            j += 5;
        }
        else
            x[j] = s[i];
    }

    x[j] = '\0';
    return x;
}

/* Escape chars in msg ids which are have special meaning in URIs */
char *mbox_msg_id_escape(apr_pool_t *p, char *s)
{
    int i, j;
    char *x;

    /* first, count the number of extra characters */
    for (i = 0, j = 0; s[i] != '\0'; i++)
        if (s[i] == '&')
            /* Length of "%26" minus 1 (original character "&") */
            j += 2;

    if (j == 0)
        return s;

    x = apr_palloc(p, i + j + 1);
    for (i = 0, j = 0; s[i] != '\0'; i++, j++) {
        if (s[i] == '&') {
            strncpy(&x[j], "%26", 3);
            j += 2;
        }
        else
            x[j] = s[i];
    }

    x[j] = '\0';
    return x;
}

static mbox_req_cfg_t *get_req_conf(request_rec *r)
{
    mbox_req_cfg_t *conf = ap_get_module_config(r->request_config, &mbox_module);
    const char *temp;
    if (conf)
        return conf;

    conf = apr_pcalloc(r->pool, sizeof(*conf));

    temp = ap_strstr_c(r->uri, r->path_info);
    if (temp)
        conf->base_uri = apr_pstrmemdup(r->pool, r->uri, temp - r->uri);
    else
        conf->base_uri = r->uri;

    temp = ap_strstr_c(conf->base_uri, ".mbox");
    /* 7 is length of "/yyyymm" */
    if (temp && temp >= conf->base_uri + 7) {
        conf->base_path = apr_pstrmemdup(r->pool, conf->base_uri,
                                         temp - 7 - conf->base_uri);

        conf->base_name = ap_strrchr_c(conf->base_path, '/') + 1;
    }
    ap_set_module_config(r->request_config, &mbox_module, conf);
    return conf;
}

/* Returns the archives base path */
const char *get_base_path(request_rec *r)
{
    mbox_req_cfg_t *conf = get_req_conf(r);
    return conf->base_path;
}

/* Returns the base URI, stripping the path_info */
const char *get_base_uri(request_rec *r)
{
    mbox_req_cfg_t *conf = get_req_conf(r);
    return conf->base_uri;
}

/* Returns the base name, stripping the path */
const char *get_base_name(request_rec *r)
{
    mbox_req_cfg_t *conf = get_req_conf(r);
    return conf->base_name;
}

static const command_rec mbox_cmds[] = {
    AP_INIT_FLAG("mboxindex", ap_set_flag_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, enabled), OR_INDEXES,
                 "Enable mod_mbox to create directory listings of .mbox files."),
    AP_INIT_FLAG("mboxantispam", ap_set_flag_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, antispam), OR_INDEXES,
                 "Enable mod_mbox email obfuscation."),
    AP_INIT_TAKE1("mboxrootpath", ap_set_string_slot,
                  (void *) APR_OFFSETOF(mbox_dir_cfg_t, root_path),
                  OR_INDEXES,
                  "Set the path to the site index."),
    AP_INIT_TAKE1("mboxstyle", ap_set_string_slot,
                  (void *) APR_OFFSETOF(mbox_dir_cfg_t, style_path),
                  OR_INDEXES,
                  "Set the path to Css stylesheet file."),
    AP_INIT_TAKE1("mboxscript", ap_set_string_slot,
                  (void *) APR_OFFSETOF(mbox_dir_cfg_t, script_path),
                  OR_INDEXES,
                  "Set the path to the Javascript file."),
    AP_INIT_FLAG("mboxhideempty", ap_set_flag_slot,
                 (void *) APR_OFFSETOF(mbox_dir_cfg_t, hide_empty),
                 OR_INDEXES,
                 "Whether to display empty mboxes in index listing."),
    {NULL}
};

module mbox_module = {
    STANDARD20_MODULE_STUFF,
    mbox_create_dir_config,     /* per-directory config creator */
    mbox_merge_dir_config,      /* dir config merger */
    NULL,                       /* server config creator */
    NULL,                       /* server config merger */
    mbox_cmds,                  /* command table */
    mbox_register_hooks         /* set up other request processing hooks */
};
