//
// Created by zhwaaaaaa on 2023/5/5.
//
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_nacos.h>
#include <ngx_nacos_data.h>

static char *ngx_http_nacos_config_var(ngx_conf_t *cf, ngx_command_t *cmd,
                                       void *conf);

static ngx_int_t ngx_http_nacos_get_variable(ngx_http_request_t *r,
                                             ngx_http_variable_value_t *v,
                                             uintptr_t data);
static ngx_int_t ngx_http_nacos_md5_get_variable(ngx_http_request_t *r,
                                                 ngx_http_variable_value_t *v,
                                                 uintptr_t data);

static void *ngx_http_nacos_create_main_conf(ngx_conf_t *cf);

static void *ngx_http_nacos_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_nacos_merge_loc_conf(ngx_conf_t *cf, void *prev,
                                           void *conf);
typedef struct {
    ngx_nacos_key_t *key;
    ngx_nacos_config_fetcher_t *fetcher;
} ngx_http_nacos_cf_t;

typedef struct {
    ngx_int_t cf_idx;
    ngx_int_t var_index;
    ngx_int_t md5_index;
    ngx_str_t def_val;
    ngx_flag_t enabled;
} ngx_http_nacos_var_item_t;

typedef struct {
    ngx_http_nacos_cf_t *cft;
    ngx_str_t def_val;
} ngx_http_nacos_cfg_item_t;

typedef struct {
    ngx_array_t *arr;  // ngx_http_nacos_var_item_t
    ngx_http_nacos_cfg_item_t *cf_items;
    ngx_http_nacos_cf_t **md5_items;
} ngx_http_nacos_loc_conf_t;

typedef struct {
    ngx_uint_t max_nacos_index;
    ngx_uint_t max_md5_index;
    ngx_array_t key_fetchers;  // ngx_http_nacos_cf_t;
} ngx_http_nacos_main_conf_t;

static ngx_nacos_config_fetcher_t *ngx_http_nacos_create_ref(ngx_log_t *log);
static void ngx_http_nacos_dec_ref(void *data);

static ngx_int_t ngx_http_nacos_fetch_config(ngx_http_nacos_cf_t *cft);

static ngx_http_module_t module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    ngx_http_nacos_create_main_conf, /* create main configuration */
    NULL,                            /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    ngx_http_nacos_create_loc_conf, /* create location configuration */
    ngx_http_nacos_merge_loc_conf,  /* merge location configuration */
};

static ngx_command_t cmds[] = {
    {ngx_string("nacos_config_var"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_SIF_CONF |
         NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_2MORE,
     ngx_http_nacos_config_var, NGX_HTTP_LOC_CONF_OFFSET, 0, NULL},
    ngx_null_command};

ngx_module_t ngx_http_nacos_config_module = {NGX_MODULE_V1,
                                             &module_ctx,
                                             cmds,
                                             NGX_HTTP_MODULE,
                                             NULL, /* init master */
                                             NULL, /* init module */
                                             NULL, /* init process */
                                             NULL, /* init thread */
                                             NULL, /* exit thread */
                                             NULL, /* exit process */
                                             NULL, /* exit master */
                                             NGX_MODULE_V1_PADDING};

static char *ngx_http_nacos_config_var(ngx_conf_t *cf, ngx_command_t *cmd,
                                       void *conf) {
    ngx_str_t *value, def_val, md5_var;
    ngx_http_variable_t *v;
    ngx_nacos_sub_t tmp;
    ngx_uint_t i, n;
    ngx_nacos_key_t *key;
    ngx_http_nacos_var_item_t *item;
    ngx_http_nacos_cf_t *cft;
    ngx_http_nacos_loc_conf_t *nlcf;
    ngx_http_nacos_main_conf_t *nmcf;

    nlcf = conf;
    if (ngx_nacos_get_main_conf(cf) == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nacos block is required before");
        return NGX_CONF_ERROR;
    }

    nmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_nacos_config_module);

    ngx_str_null(&def_val);
    ngx_str_null(&md5_var);
    ngx_memzero(&tmp, sizeof(tmp));
    value = cf->args->elts;

    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid variable name \"%V\"",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    for (i = 2, n = cf->args->nelts; i < n; ++i) {
        if (value[i].len > 8 &&
            ngx_strncmp(value[i].data, "data_id=", 8) == 0) {
            tmp.data_id.data = value[i].data + 8;
            tmp.data_id.len = value[i].len - 8;
            continue;
        }
        if (value[i].len > 6 && ngx_strncmp(value[i].data, "group=", 6) == 0) {
            tmp.group.data = value[i].data + 6;
            tmp.group.len = value[i].len - 6;
            continue;
        }

        if (value[i].len > 8 &&
            ngx_strncmp(value[i].data, "default=", 8) == 0) {
            def_val.data = value[i].data + 8;
            def_val.len = value[i].len - 8;
            continue;
        }

        if (value[i].len > 8 &&
            ngx_strncmp(value[i].data, "md5_var=", 8) == 0) {
            md5_var.data = value[i].data + 8;
            md5_var.len = value[i].len - 8;
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid parameter \"%V\"",
                           &value[i]);
        return NGX_CONF_ERROR;
    }

    if (!tmp.data_id.len) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "require data_id");
        return NGX_CONF_ERROR;
    }

    if (md5_var.len > 0 && md5_var.data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "md5 variable name must start with $ \"%V\"",
                           &md5_var);
        return NGX_CONF_ERROR;
    }

    if (nlcf->arr == NULL) {
        nlcf->arr = ngx_array_create(cf->temp_pool, 4, sizeof(*item));
        if (nlcf->arr == NULL) {
            return NGX_CONF_ERROR;
        }
    }
    item = ngx_array_push(nlcf->arr);
    if (item == NULL) {
        return NGX_CONF_ERROR;
    }
    item->cf_idx = -1;

    tmp.key_ptr = &key;
    if (ngx_nacos_subscribe_config(cf, &tmp) == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    if (nmcf->key_fetchers.size == 0) {
        if (ngx_array_init(&nmcf->key_fetchers, cf->pool, 16, sizeof(*cft)) !=
            NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    if (nmcf->key_fetchers.nelts > 0) {
        cft = nmcf->key_fetchers.elts;
        for (i = 0; i < nmcf->key_fetchers.nelts; i++) {
            if (key == cft[i].key) {
                item->cf_idx = (ngx_int_t) i;
                break;
            }
        }
    }

    if (item->cf_idx == -1) {
        cft = ngx_array_push(&nmcf->key_fetchers);
        if (cft == NULL) {
            return NGX_CONF_ERROR;
        }

        item->cf_idx = (ngx_int_t) nmcf->key_fetchers.nelts - 1;
        cft->fetcher = ngx_http_nacos_create_ref(cf->log);
        cft->key = key;
        if (cft->fetcher == NULL) {
            return NGX_CONF_ERROR;
        }
        if (nax_nacos_get_config(cft->key, cft->fetcher) == NGX_ERROR) {
            return NGX_CONF_ERROR;
        }
    }

    value[1].len--;
    value[1].data++;
    v = ngx_http_add_variable(cf, &value[1], NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    if (v->get_handler != ngx_http_nacos_get_variable) {
        v->get_handler = ngx_http_nacos_get_variable;
        v->data = nmcf->max_nacos_index++;
    }
    item->var_index = (ngx_int_t) v->data;
    item->md5_index = -1;
    item->def_val = def_val;
    item->enabled = 1;

    if (md5_var.len) {
        md5_var.len--;
        md5_var.data++;
        v = ngx_http_add_variable(cf, &md5_var, NGX_HTTP_VAR_CHANGEABLE);
        if (v == NULL) {
            return NGX_CONF_ERROR;
        }
        if (v->get_handler != ngx_http_nacos_md5_get_variable) {
            v->get_handler = ngx_http_nacos_md5_get_variable;
            v->data = nmcf->max_md5_index++;
        }
        item->md5_index = (ngx_int_t) v->data;
    }

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_nacos_get_variable(ngx_http_request_t *r,
                                             ngx_http_variable_value_t *v,
                                             uintptr_t data) {
    ngx_http_nacos_cfg_item_t *item;
    ngx_nacos_config_fetcher_t *fetcher;
    ngx_uint_t md5_len;
    ngx_http_cleanup_t *cleanup;
    ngx_http_nacos_loc_conf_t *nlcf;

    nlcf = ngx_http_get_module_loc_conf(r, ngx_http_nacos_config_module);

    item = nlcf->cf_items + (ngx_int_t) data;
    if (item->cft == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (ngx_http_nacos_fetch_config(item->cft) != NGX_OK) {
        return NGX_ERROR;
    }

    fetcher = item->cft->fetcher;

    if (fetcher->out_config.len == 0) {
        v->len = item->def_val.len;
        v->data = item->def_val.data;
    } else {
        // version md5_len;
        md5_len = *(ngx_uint_t *) (fetcher->out_config.data + sizeof(size_t) +
                                   sizeof(ngx_uint_t));
        v->len =
            *(ngx_uint_t *) (fetcher->out_config.data + sizeof(size_t) +
                             sizeof(ngx_uint_t) + sizeof(ngx_uint_t) + md5_len);
        v->data = fetcher->out_config.data + sizeof(size_t) +
                  sizeof(ngx_uint_t) + sizeof(ngx_uint_t) + md5_len +
                  sizeof(ngx_uint_t);
        ++fetcher->ref;

        cleanup = ngx_http_cleanup_add(r, 0);
        if (cleanup == NULL) {
            return NGX_ERROR;
        }
        cleanup->data = fetcher;
        cleanup->handler = ngx_http_nacos_dec_ref;
    }
    v->valid = 1;
    return NGX_OK;
}

static ngx_int_t ngx_http_nacos_md5_get_variable(ngx_http_request_t *r,
                                                 ngx_http_variable_value_t *v,
                                                 uintptr_t data) {
    ngx_http_nacos_cf_t *cft;
    ngx_nacos_config_fetcher_t *fetcher;
    ngx_uint_t md5_len;
    ngx_http_cleanup_t *cleanup;
    ngx_http_nacos_loc_conf_t *nlcf;

    nlcf = ngx_http_get_module_loc_conf(r, ngx_http_nacos_config_module);

    cft = nlcf->md5_items[(ngx_int_t) data];
    if (cft == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (ngx_http_nacos_fetch_config(cft) != NGX_OK) {
        return NGX_ERROR;
    }

    fetcher = cft->fetcher;

    if (fetcher->out_config.len == 0) {
        v->len = 0;
        v->data = NULL;
        v->not_found = 1;
    } else {
        // version md5_len;
        md5_len = *(ngx_uint_t *) (fetcher->out_config.data + sizeof(size_t) +
                                   sizeof(ngx_uint_t));
        v->len = md5_len;
        v->data = fetcher->out_config.data + sizeof(size_t) +
                  sizeof(ngx_uint_t) + sizeof(ngx_uint_t);
        v->valid = 1;

        ++fetcher->ref;

        cleanup = ngx_http_cleanup_add(r, 0);
        if (cleanup == NULL) {
            return NGX_ERROR;
        }
        cleanup->data = fetcher;
        cleanup->handler = ngx_http_nacos_dec_ref;
    }
    return NGX_OK;
}

static ngx_int_t ngx_http_nacos_fetch_config(ngx_http_nacos_cf_t *cft) {
    ngx_nacos_config_fetcher_t *fetcher;

    fetcher = cft->fetcher;
    if (ngx_nacos_shmem_change(cft->key, fetcher->version)) {
        fetcher = ngx_http_nacos_create_ref(fetcher->pool->log);
        if (fetcher == NULL) {
            return NGX_ERROR;
        }

        if (nax_nacos_get_config(cft->key, fetcher) != NGX_OK) {
            ngx_http_nacos_dec_ref(fetcher);
            return NGX_ERROR;
        }

        ngx_http_nacos_dec_ref(cft->fetcher);
        cft->fetcher = fetcher;
    }
    return NGX_OK;
}

static ngx_nacos_config_fetcher_t *ngx_http_nacos_create_ref(ngx_log_t *log) {
    ngx_pool_t *pool;
    ngx_nacos_config_fetcher_t *ref;

    pool = ngx_create_pool(512, log);
    if (pool == NULL) {
        return NULL;
    }
    ref = ngx_pcalloc(pool, sizeof(*ref));
    if (ref == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ref->ref = 1;
    ref->pool = pool;
    return ref;
}
static void ngx_http_nacos_dec_ref(void *data) {
    ngx_nacos_config_fetcher_t *ref = data;
    if (--ref->ref == 0) {
        ngx_destroy_pool(ref->pool);
    }
}

static void *ngx_http_nacos_create_main_conf(ngx_conf_t *cf) {
    return ngx_pcalloc(cf->pool, sizeof(ngx_http_nacos_main_conf_t));
}

static void *ngx_http_nacos_create_loc_conf(ngx_conf_t *cf) {
    return ngx_pcalloc(cf->pool, sizeof(ngx_http_nacos_loc_conf_t));
}

static char *ngx_http_nacos_merge_loc_conf(ngx_conf_t *cf, void *prev,
                                           void *conf) {
    ngx_http_nacos_loc_conf_t *parent, *nlcf;
    ngx_http_nacos_main_conf_t *nmcf;
    ngx_uint_t i, n;
    ngx_flag_t has_md5;
    ngx_http_nacos_var_item_t *it;
    ngx_http_nacos_cfg_item_t *cit;

    parent = prev;
    nlcf = conf;
    nmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_nacos_config_module);

    if (parent->arr != NULL && parent->cf_items == NULL) {
        parent->cf_items =
            ngx_pcalloc(cf->pool, nmcf->max_nacos_index * sizeof(*cit));
        if (parent->cf_items == NULL) {
            return NGX_CONF_ERROR;
        }

        has_md5 = 0;
        it = parent->arr->elts;
        n = parent->arr->nelts;
        for (i = 0; i < n; i++) {
            if (it[i].md5_index >= 0) {
                has_md5 = 1;
            }
            cit = parent->cf_items + it[i].var_index;
            cit->def_val = it[i].def_val;
            cit->cft =
                (ngx_http_nacos_cf_t *) nmcf->key_fetchers.elts + it[i].cf_idx;
        }

        if (has_md5) {
            parent->md5_items = ngx_pcalloc(
                cf->pool, nmcf->max_md5_index * sizeof(ngx_http_nacos_cf_t *));
            if (parent->md5_items == NULL) {
                return NGX_CONF_ERROR;
            }
            for (i = 0; i < n; i++) {
                if (it[i].md5_index >= 0) {
                    parent->md5_items[it[i].md5_index] =
                        (ngx_http_nacos_cf_t *) nmcf->key_fetchers.elts +
                        it[i].cf_idx;
                }
            }
        }

        parent->arr = NULL;
    }

    if (nlcf->arr == NULL) {
        nlcf->cf_items = parent->cf_items;
        nlcf->md5_items = parent->md5_items;
    } else {
        nlcf->cf_items =
            ngx_pcalloc(cf->pool, nmcf->max_nacos_index * sizeof(*cit));
        if (nlcf->cf_items == NULL) {
            return NGX_CONF_ERROR;
        }

        has_md5 = 0;
        it = nlcf->arr->elts;
        n = nlcf->arr->nelts;
        for (i = 0; i < n; i++) {
            if (it[i].md5_index >= 0) {
                has_md5 = 1;
            }
            cit = nlcf->cf_items + it[i].var_index;
            cit->def_val = it[i].def_val;
            cit->cft =
                (ngx_http_nacos_cf_t *) nmcf->key_fetchers.elts + it[i].cf_idx;
        }
        if (parent->cf_items != NULL) {
            cit = parent->cf_items;
            for (i = 0; i < nmcf->max_nacos_index; ++i) {
                if (cit[i].cft && !nlcf->cf_items[i].cft) {
                    nlcf->cf_items[i].cft = cit[i].cft;
                    nlcf->cf_items[i].def_val = cit[i].def_val;
                }
            }
        }

        if (has_md5) {
            nlcf->md5_items = ngx_pcalloc(
                cf->pool, nmcf->max_md5_index * sizeof(ngx_http_nacos_cf_t *));
            if (nlcf->md5_items == NULL) {
                return NGX_CONF_ERROR;
            }

            for (i = 0; i < n; i++) {
                if (it[i].md5_index >= 0) {
                    nlcf->md5_items[it[i].md5_index] =
                        (ngx_http_nacos_cf_t *) nmcf->key_fetchers.elts +
                        it[i].cf_idx;
                }
            }

            if (parent->md5_items != NULL) {
                for (i = 0; i < nmcf->max_md5_index; ++i) {
                    if (parent->md5_items[i] && !nlcf->md5_items[i]) {
                        nlcf->md5_items[i] = parent->md5_items[i];
                    }
                }
            }
        } else {
            nlcf->md5_items = parent->md5_items;
        }

        nlcf->arr = NULL;
    }
    return NGX_CONF_OK;
}
