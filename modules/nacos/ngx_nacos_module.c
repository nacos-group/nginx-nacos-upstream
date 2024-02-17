//
// Created by dear on 22-5-22.
//

#include <ngx_event.h>
#include <ngx_nacos.h>
#include <ngx_nacos_aux.h>
#include <ngx_nacos_data.h>

static char *ngx_nacos_init_conf(ngx_cycle_t *cycle, void *conf);

static char *ngx_nacos_conf_block(ngx_conf_t *cf, ngx_command_t *cmd,
                                  void *conf);

static char *ngx_nacos_conf_server_list(ngx_conf_t *cf, ngx_command_t *cmd,
                                        void *conf);

static char *ngx_nacos_conf_error_log(ngx_conf_t *cf, ngx_command_t *cmd,
                                      void *conf);

static ngx_int_t ngx_nacos_init_key_zone(ngx_shm_zone_t *zone, void *data);

static ngx_int_t ngx_nacos_subscribe(ngx_conf_t *cf, ngx_nacos_sub_t *sub,
                                     int naming);

static ngx_core_module_t nacos_module = {
    ngx_string("nacos"),
    NULL,                // 解析配置文件之前执行
    ngx_nacos_init_conf  // 解析配置文件之后执行
};

static ngx_command_t cmds[] = {
    {ngx_string("nacos"), NGX_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
     ngx_nacos_conf_block, 0, 0, NULL},
    {ngx_string("server_list"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
     ngx_nacos_conf_server_list, 0,
     offsetof(ngx_nacos_main_conf_t, server_list), NULL},
    {ngx_string("grpc_server_list"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
     ngx_nacos_conf_server_list, 0,
     offsetof(ngx_nacos_main_conf_t, grpc_server_list), NULL},
    {ngx_string("udp_port"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, 0, offsetof(ngx_nacos_main_conf_t, udp_port), NULL},
    {ngx_string("udp_ip"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, 0, offsetof(ngx_nacos_main_conf_t, udp_ip), NULL},
    {ngx_string("udp_bind"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, 0, offsetof(ngx_nacos_main_conf_t, udp_bind), NULL},
    {ngx_string("default_group"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, 0, offsetof(ngx_nacos_main_conf_t, default_group),
     NULL},
    {ngx_string("tenant_namespace"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, 0,
     offsetof(ngx_nacos_main_conf_t, tenant_namespace), NULL},
    {ngx_string("server_host"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_str_slot, 0, offsetof(ngx_nacos_main_conf_t, server_host),
     NULL},
    {ngx_string("key_zone_size"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_size_slot, 0, offsetof(ngx_nacos_main_conf_t, key_zone_size),
     NULL},
    {ngx_string("keys_hash_max_size"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_num_slot, 0,
     offsetof(ngx_nacos_main_conf_t, keys_hash_max_size), NULL},
    {ngx_string("keys_bucket_size"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_num_slot, 0,
     offsetof(ngx_nacos_main_conf_t, keys_bucket_size), NULL},
    {ngx_string("config_keys_hash_max_size"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_num_slot, 0,
     offsetof(ngx_nacos_main_conf_t, config_keys_hash_max_size), NULL},
    {ngx_string("config_keys_bucket_size"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_num_slot, 0,
     offsetof(ngx_nacos_main_conf_t, config_keys_bucket_size), NULL},
    {ngx_string("udp_pool_size"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
     ngx_conf_set_size_slot, 0, offsetof(ngx_nacos_main_conf_t, udp_pool_size),
     NULL},
    {ngx_string("error_log"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
     ngx_nacos_conf_error_log, 0, 0, NULL

    },
    {ngx_string("cache_dir"),
     NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
     ngx_conf_set_str_slot, 0, offsetof(ngx_nacos_main_conf_t, cache_dir), NULL

    },
    ngx_null_command};

ngx_module_t ngx_nacos_module = {NGX_MODULE_V1,
                                 &nacos_module,
                                 cmds,
                                 NGX_CORE_MODULE,
                                 NULL, /* init master */
                                 NULL, /* init module */
                                 NULL, /* init process */
                                 NULL, /* init thread */
                                 NULL, /* exit thread */
                                 NULL, /* exit process */
                                 NULL, /* exit master */
                                 NGX_MODULE_V1_PADDING};

static char *ngx_nacos_init_conf(ngx_cycle_t *cycle, void *conf) {
    ngx_nacos_main_conf_t *ncf = conf;
    ngx_hash_init_t ha;
    ngx_hash_key_t *hkeys;
    ngx_nacos_key_t **k_ptr;
    ngx_pool_t *temp;
    ngx_uint_t i, n;
    u_char *buf;
    size_t buf_len;
    if (ncf == NULL) {  // no nacos config
        return NGX_CONF_OK;
    }
    if (ncf->keys.nelts == 0 && ncf->config_keys.nelts == 0) {
        return "remove nacos block if not need";
    }

    temp = ngx_create_pool(4096, cycle->log);
    if (temp == NULL) {
        return NGX_CONF_ERROR;
    }

    // nacos_keys_hash
    ha.pool = cycle->pool;
    ha.temp_pool = temp;
    {
        ha.bucket_size = ncf->keys_bucket_size;
        ha.max_size = ncf->keys_hash_max_size;
        ha.key = ngx_hash_key_lc;
        ha.name = "nacos_keys_hash";
        ha.hash = NULL;

        n = ncf->keys.nelts;
        k_ptr = ncf->keys.elts;

        hkeys = ngx_palloc(ha.temp_pool, n * sizeof(*hkeys));
        if (hkeys == NULL) {
            ngx_destroy_pool(temp);
            return NGX_CONF_ERROR;
        }

        for (i = 0; i < n; ++i) {
            buf_len = k_ptr[i]->group.len + k_ptr[i]->data_id.len + 5;
            buf = ngx_palloc(ha.temp_pool, buf_len);
            if (buf == NULL) {
                ngx_destroy_pool(temp);
                return NGX_CONF_ERROR;
            }

            hkeys[i].key.len =
                ngx_snprintf(buf, buf_len - 1, "%V@@%V", &k_ptr[i]->group,
                             &k_ptr[i]->data_id) -
                buf;
            hkeys[i].key.data = buf;
            hkeys[i].value = k_ptr[i];
            hkeys[i].key_hash = ha.key(buf, hkeys[i].key.len);
        }

        if (ngx_hash_init(&ha, hkeys, n) != NGX_OK) {
            ngx_destroy_pool(temp);
            return NGX_CONF_ERROR;
        }
        ncf->key_hash = ha.hash;
    }

    {
        ha.bucket_size = ncf->config_keys_bucket_size;
        ha.max_size = ncf->config_keys_hash_max_size;
        ha.key = ngx_hash_key_lc;
        ha.name = "nacos_config_keys_hash";
        ha.hash = NULL;

        n = ncf->config_keys.nelts;
        k_ptr = ncf->config_keys.elts;

        hkeys = ngx_palloc(ha.temp_pool, n * sizeof(*hkeys));
        if (hkeys == NULL) {
            ngx_destroy_pool(temp);
            return NGX_CONF_ERROR;
        }

        for (i = 0; i < n; ++i) {
            buf_len = k_ptr[i]->group.len + k_ptr[i]->data_id.len + 5;
            buf = ngx_palloc(ha.temp_pool, buf_len);
            if (buf == NULL) {
                ngx_destroy_pool(temp);
                return NGX_CONF_ERROR;
            }
            hkeys[i].key.len =
                ngx_snprintf(buf, buf_len - 1, "%V@@%V", &k_ptr[i]->group,
                             &k_ptr[i]->data_id) -
                buf;
            hkeys[i].key.data = buf;
            hkeys[i].value = k_ptr[i];
            hkeys[i].key_hash = ha.key(buf, hkeys[i].key.len);
        }

        if (ngx_hash_init(&ha, hkeys, n) != NGX_OK) {
            ngx_destroy_pool(temp);
            return NGX_CONF_ERROR;
        }
        ncf->config_key_hash = ha.hash;
    }

    ngx_destroy_pool(temp);
    return NGX_CONF_OK;
}

static char *ngx_nacos_conf_block(ngx_conf_t *cf, ngx_command_t *cmd,
                                  void *conf) {
    ngx_conf_t pcf;
    char *rv;
    ngx_int_t i;
    ngx_url_t u;
    ngx_err_t err;
    ngx_nacos_main_conf_t *ncf, **mncf = conf;

    if (*mncf) {
        return "is duplicate";
    }
    ncf = *mncf = ngx_pcalloc(cf->pool, sizeof(*ncf));
    if (ncf == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_array_init(&ncf->server_list, cf->pool, 4, sizeof(ngx_addr_t)) !=
        NGX_OK) {
        return NGX_CONF_ERROR;
    }
    if (ngx_array_init(&ncf->grpc_server_list, cf->pool, 4,
                       sizeof(ngx_addr_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ncf->keys_bucket_size = NGX_CONF_UNSET_UINT;
    ncf->keys_hash_max_size = NGX_CONF_UNSET_UINT;
    ncf->config_keys_bucket_size = NGX_CONF_UNSET_UINT;
    ncf->config_keys_hash_max_size = NGX_CONF_UNSET_UINT;
    ncf->key_zone_size = NGX_CONF_UNSET_SIZE;
    ncf->udp_pool_size = NGX_CONF_UNSET_SIZE;

    pcf = *cf;
    cf->cmd_type = NGX_NACOS_MAIN_CONF;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        goto end;
    }

    if (!ncf->server_list.nelts) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "nacos server_list is empty");
        rv = NGX_CONF_ERROR;
        goto end;
    }

    if (ncf->udp_port.len && ncf->udp_port.data) {
        if ((i = ngx_atoi(ncf->udp_port.data, ncf->udp_port.len)) ==
            NGX_ERROR) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "nacos udp_port not number");
            rv = NGX_CONF_ERROR;
            goto end;
        }
        if (i <= 0 || i > 65535) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "nacos udp_port=\"%V\" is invalid", i);
            rv = NGX_CONF_ERROR;
            goto end;
        }
        if (!ncf->udp_ip.len || !ncf->udp_ip.data) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "nacos udp_ip is not config");
            rv = NGX_CONF_ERROR;
            goto end;
        }
        if (!ncf->udp_bind.data) {
            ncf->udp_bind.len = ncf->udp_ip.len + ncf->udp_port.len + 1;
            ncf->udp_bind.data = ngx_palloc(cf->pool, ncf->udp_bind.len);
            if (ncf->udp_bind.data == NULL) {
                rv = NGX_CONF_ERROR;
                goto end;
            }
            memcpy(ncf->udp_bind.data, ncf->udp_ip.data, ncf->udp_ip.len);
            ncf->udp_bind.data[ncf->udp_ip.len] = ':';
            memcpy(ncf->udp_bind.data + ncf->udp_ip.len + 1, ncf->udp_port.data,
                   ncf->udp_port.len);
        }
        ngx_memzero(&u, sizeof(ngx_url_t));
        u.url = ncf->udp_bind;
        u.listen = 1;
        if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "%s in upstream \"%V\"", u.err, &u.url);
            }

            rv = NGX_CONF_ERROR;
            goto end;
        }

        ncf->udp_addr = u.addrs[0];
    } else if (ncf->grpc_server_list.nelts == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nacos grpc_server_list is not config");
        rv = NGX_CONF_ERROR;
        goto end;
    }

    if (!ncf->cache_dir.len) {
        ngx_str_set(&ncf->cache_dir, "nacos_cache");
    }
    if (ngx_conf_full_name(cf->cycle, &ncf->cache_dir, 0) != NGX_OK) {
        rv = NGX_CONF_ERROR;
        goto end;
    }
    if ((err = ngx_create_full_path(ncf->cache_dir.data, 0744))) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, err,
                           "nacos create cache dir \"%V\" error",
                           &ncf->cache_dir);
        rv = NGX_CONF_ERROR;
        goto end;
    }

    if (!ncf->default_group.data) {
        ngx_str_set(&ncf->default_group, "DEFAULT_GROUP");
    }

    if (!ncf->server_host.data) {
        ngx_str_set(&ncf->server_host, "nacos");
    }

    if (!ncf->tenant_namespace.data) {
        ngx_str_set(&ncf->tenant_namespace, "public");
    }

    if (!ncf->error_log) {
        ncf->error_log = &cf->cycle->new_log;
    }

    ngx_conf_init_size_value(ncf->key_zone_size, 4 << 20);  // 4M
    ngx_conf_init_size_value(ncf->udp_pool_size, 8192);
    ngx_conf_init_uint_value(ncf->keys_hash_max_size, 128);
    ngx_conf_init_uint_value(ncf->keys_bucket_size, 128);
    ngx_conf_init_uint_value(ncf->config_keys_hash_max_size, 128);
    ngx_conf_init_uint_value(ncf->config_keys_bucket_size, 128);
    ncf->keys_bucket_size =
        ngx_align(ncf->keys_bucket_size, ngx_cacheline_size);
    ncf->config_keys_bucket_size =
        ngx_align(ncf->config_keys_bucket_size, ngx_cacheline_size);

    if (ngx_nacos_aux_init(cf) != NGX_OK) {
        rv = NGX_CONF_ERROR;
        goto end;
    }

end:
    *cf = pcf;
    return rv;
}

static char *ngx_nacos_conf_server_list(ngx_conf_t *cf, ngx_command_t *cmd,
                                        void *conf) {
    ngx_array_t *list_arr;
    ngx_uint_t i, j, n;
    ngx_str_t *value;
    ngx_url_t u;
    ngx_addr_t *adr;

    list_arr = (ngx_array_t *) (((char *) conf) + cmd->offset);
    value = cf->args->elts;
    n = cf->args->nelts;

    for (i = 1; i < n; ++i) {
        memset(&u, 0, sizeof(u));
        u.url = value[i];
        u.default_port = 8848;
        if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "%s in nacos server_list \"%V\"", u.err,
                                   &u.url);
            }
            return NGX_CONF_ERROR;
        }

        for (j = 0; j < u.naddrs; ++j) {
            adr = ngx_array_push(list_arr);
            if (adr == NULL) {
                return NGX_CONF_ERROR;
            }
            *adr = u.addrs[j];
        }
    }

    return NGX_CONF_OK;
}

static char *ngx_nacos_conf_error_log(ngx_conf_t *cf, ngx_command_t *cmd,
                                      void *conf) {
    ngx_nacos_main_conf_t *mcf = conf;
    return ngx_log_set_log(cf, &mcf->error_log);
}

ngx_nacos_main_conf_t *ngx_nacos_get_main_conf(ngx_conf_t *cf) {
    return (ngx_nacos_main_conf_t *)
        cf->cycle->conf_ctx[ngx_nacos_module.index];
}

ngx_int_t ngx_nacos_subscribe_naming(ngx_conf_t *cf, ngx_nacos_sub_t *sub) {
    return ngx_nacos_subscribe(cf, sub, 1);
}

ngx_int_t ngx_nacos_subscribe_config(ngx_conf_t *cf, ngx_nacos_sub_t *sub) {
    return ngx_nacos_subscribe(cf, sub, 0);
}

static ngx_int_t ngx_nacos_subscribe(ngx_conf_t *cf, ngx_nacos_sub_t *sub,
                                     int naming) {
    ngx_nacos_main_conf_t *mcf;
    ngx_uint_t i, n;
    ngx_nacos_key_t **kptr, *k;
    ngx_nacos_data_t tmp;
    ngx_str_t zone_name;
    ngx_int_t rc;
    ngx_array_t *all_keys;

    mcf = ngx_nacos_get_main_conf(cf);
    all_keys = naming ? &mcf->keys : &mcf->config_keys;
    if (!naming && mcf->udp_port.len) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "nacos config is not supported by udp");
        return NGX_ERROR;
    }

    tmp.data_id = sub->data_id;
    tmp.group = sub->group;
    if (!tmp.group.len) {
        tmp.group = mcf->default_group;
    }
    if (tmp.data_id.len + tmp.group.len + 2 >= 512) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "nacos data_id and group is too long");
        return NGX_ERROR;
    }

    if (mcf->zone == NULL) {
        ngx_str_set(&zone_name, "nacos_zone");
        mcf->zone = ngx_shared_memory_add(cf, &zone_name, mcf->key_zone_size,
                                          &ngx_nacos_module);
        if (mcf->zone == NULL) {
            return NGX_ERROR;
        }
        mcf->zone->noreuse = 1;
        mcf->zone->data = mcf;
        mcf->zone->init = ngx_nacos_init_key_zone;
        mcf->cur_srv_index = rand() % mcf->server_list.nelts;
    }

    if (all_keys->size == 0) {
        if (ngx_array_init(all_keys, cf->pool, 16, sizeof(ngx_nacos_key_t *)) !=
            NGX_OK) {
            return NGX_ERROR;
        }
    } else {
        n = all_keys->nelts;
        kptr = all_keys->elts;
        for (i = 0; i < n; ++i) {
            if (nacos_key_eq(kptr[i], &tmp)) {
                *sub->key_ptr = kptr[i];
                return NGX_OK;
            }
        }
    }

    tmp.pool = cf->temp_pool;
    rc = ngx_nacos_fetch_disk_data(mcf, &tmp);
    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }
    if (rc == NGX_DECLINED) {
        if (naming) {
            if (ngx_nacos_fetch_addrs_net_data(mcf, &tmp) != NGX_OK) {
                return NGX_ERROR;
            }
        } else {
            if (ngx_nacos_fetch_config_net_data(mcf, &tmp) != NGX_OK) {
                return NGX_ERROR;
            }
        }

        if (ngx_nacos_write_disk_data(mcf, &tmp) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    kptr = ngx_array_push(all_keys);
    if (kptr == NULL) {
        return NGX_ERROR;
    }
    k = ngx_palloc(cf->pool, sizeof(*k));
    if (k == NULL) {
        return NGX_ERROR;
    }

    *kptr = k;
    k->data_id = tmp.data_id;
    k->group = tmp.group;
    k->ctx = ngx_palloc(cf->temp_pool, sizeof(ngx_nacos_key_ctx_t));
    if (k->ctx == NULL) {
        return NGX_ERROR;
    }
    k->ctx->wrlock = 0;
    k->ctx->version = tmp.version;
    k->ctx->data = tmp.adr;
    k->use_shared = 0;
    *sub->key_ptr = k;
    return NGX_OK;
}

static ngx_int_t ngx_nacos_init_key_zone(ngx_shm_zone_t *zone, void *data) {
    ngx_nacos_main_conf_t *mcf;
    ngx_nacos_key_t **key;
    ngx_nacos_key_ctx_t *ctx;
    ngx_uint_t i, n;
    size_t len;
    char *c;

    mcf = zone->data;
    mcf->sh = (ngx_slab_pool_t *) zone->shm.addr;
    key = mcf->keys.elts;
    n = mcf->keys.nelts;

    for (i = 0; i < n; ++i) {
        ctx = key[i]->ctx;
        c = ctx->data;
        key[i]->use_shared = 1;
        key[i]->ctx = ngx_slab_alloc_locked(mcf->sh, sizeof(*ctx));
        if (key[i]->ctx == NULL) {
            return NGX_ERROR;
        }
        if (c != NULL) {
            len = *(size_t *) c;
            key[i]->ctx->data = ngx_slab_alloc_locked(mcf->sh, len);
            if (key[i]->ctx->data == NULL) {
                return NGX_ERROR;
            }
            key[i]->ctx->wrlock = ctx->wrlock;
            key[i]->ctx->version = ctx->version;
            memcpy(key[i]->ctx->data, c, len);
        } else {
            key[i]->ctx->data = NULL;
            key[i]->ctx->wrlock = ctx->wrlock;
            key[i]->ctx->version = ctx->version;
        }
    }

    key = mcf->config_keys.elts;
    n = mcf->config_keys.nelts;
    for (i = 0; i < n; ++i) {
        ctx = key[i]->ctx;
        c = ctx->data;
        key[i]->use_shared = 1;
        key[i]->ctx = ngx_slab_alloc_locked(mcf->sh, sizeof(*ctx));
        if (key[i]->ctx == NULL) {
            return NGX_ERROR;
        }
        if (c != NULL) {
            len = *(size_t *) c;
            key[i]->ctx->data = ngx_slab_alloc_locked(mcf->sh, len);
            if (key[i]->ctx->data == NULL) {
                return NGX_ERROR;
            }
            key[i]->ctx->wrlock = ctx->wrlock;
            key[i]->ctx->version = ctx->version;
            memcpy(key[i]->ctx->data, c, len);
        } else {
            key[i]->ctx->data = NULL;
            key[i]->ctx->wrlock = ctx->wrlock;
            key[i]->ctx->version = ctx->version;
        }
    }

    return NGX_OK;
}

ngx_int_t nax_nacos_get_addrs(ngx_nacos_key_t *key, ngx_uint_t *version,
                              ngx_array_t *out_addrs) {
    ngx_uint_t old_ver, new_ver;
    ngx_int_t rc;
    ngx_nacos_key_ctx_t *ctx = key->ctx;
    old_ver = *version;
    rc = NGX_DECLINED;
    if (key->use_shared) {
        ngx_rwlock_rlock(&ctx->wrlock);
    }
    if (old_ver != (new_ver = ctx->version)) {
        rc = ngx_nacos_deep_copy_addrs(ctx->data, out_addrs);
    }
    if (key->use_shared) {
        ngx_rwlock_unlock(&ctx->wrlock);
    }
    *version = new_ver;
    return rc;
}

ngx_int_t nax_nacos_get_config(ngx_nacos_key_t *key,
                               ngx_nacos_config_fetcher_t *out) {
    ngx_uint_t old_ver, new_ver;
    ngx_nacos_key_ctx_t *ctx;
    char *out_mem;
    size_t out_len, last_len = 0;
    ngx_flag_t use_shared, suc;

    use_shared = key->use_shared;
    ctx = key->ctx;
    old_ver = out->version;
    out_mem = NULL;
    for (;;) {
        if (use_shared) {
            ngx_rwlock_rlock(&ctx->wrlock);
        }
        new_ver = ctx->version;
        out_len = ctx->data ? *(size_t *) ctx->data : 0;
        if (use_shared) {
            ngx_rwlock_unlock(&ctx->wrlock);
        }

        if (new_ver == old_ver) {
            return NGX_DECLINED;
        }
        if (out_len == 0) {
            break;
        }
        if (out_mem == NULL || out_len > last_len) {
            last_len = out_len;
            out_mem = ngx_palloc(out->pool, out_len);
            if (out_mem == NULL) {
                return NGX_ERROR;
            }
        }

        if (use_shared) {
            ngx_rwlock_rlock(&ctx->wrlock);
        }
        if (new_ver == ctx->version) {
            suc = 1;
            ngx_memcpy(out_mem, ctx->data, out_len);
        } else {
            suc = 0;
        }
        if (use_shared) {
            ngx_rwlock_unlock(&ctx->wrlock);
        }
        if (suc) {
            break;
        }
    }
    out->out_config.len = out_len;
    out->out_config.data = (u_char *) out_mem;
    out->version = new_ver;
    return NGX_OK;
}
