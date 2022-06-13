//
// Created by dear on 22-5-22.
//

#include <ngx_nacos.h>
#include <ngx_event.h>
#include <ngx_nacos_data.h>
#include <ngx_nacos_aux.h>

static char *ngx_nacos_init_conf(ngx_cycle_t *cycle, void *conf);

static char *ngx_nacos_conf_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_nacos_conf_server_list(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_nacos_conf_error_log(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_nacos_init_key_zone(ngx_shm_zone_t *zone, void *data);


static ngx_core_module_t nacos_module = {
        ngx_string("nacos"),
        NULL, // 解析配置文件之前执行
        ngx_nacos_init_conf // 解析配置文件之后执行
};

static ngx_command_t cmds[] = {
        {
                ngx_string("nacos"),
                NGX_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
                ngx_nacos_conf_block,
                0,
                0,
                NULL
        },
        {
                ngx_string("server_list"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
                ngx_nacos_conf_server_list,
                0,
                0,
                NULL
        },
        {
                ngx_string("udp_port"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_str_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, udp_port),
                NULL
        },
        {
                ngx_string("udp_ip"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_str_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, udp_ip),
                NULL
        },
        {
                ngx_string("udp_bind"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_str_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, udp_bind),
                NULL
        },
        {
                ngx_string("default_group"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_str_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, default_group),
                NULL
        },
        {
                ngx_string("key_zone_size"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_size_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, key_zone_size),
                NULL
        },
        {
                ngx_string("keys_hash_max_size"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_num_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, keys_hash_max_size),
                NULL
        },
        {
                ngx_string("keys_bucket_size"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_num_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, keys_bucket_size),
                NULL
        },
        {
                ngx_string("udp_pool_size"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_size_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, udp_pool_size),
                NULL
        },
        {
                ngx_string("error_log"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
                ngx_nacos_conf_error_log,
                0,
                0,
                NULL

        },
        {
                ngx_string("cache_dir"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
                ngx_conf_set_str_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, cache_dir),
                NULL

        },
        ngx_null_command
};

ngx_module_t ngx_nacos_module = {
        NGX_MODULE_V1,
        &nacos_module,
        cmds,
        NGX_CORE_MODULE,
        NULL,                                 /* init master */
        NULL,                                  /* init module */
        NULL,                                  /* init process */
        NULL,                                  /* init thread */
        NULL,                                  /* exit thread */
        NULL,                                  /* exit process */
        NULL,                                  /* exit master */
        NGX_MODULE_V1_PADDING
};


static char *ngx_nacos_init_conf(ngx_cycle_t *cycle, void *conf) {
    ngx_nacos_main_conf_t *ncf = conf;
    ngx_hash_init_t ha;
    ngx_hash_key_t *hkeys;
    ngx_nacos_key_t * k;
    ngx_pool_t *temp;
    ngx_uint_t i, n;
    u_char buf[512];
    if (ncf == NULL) {// no nacos config
        return NGX_CONF_OK;
    }
    if (ncf->keys.nelts == 0) {
        return "remove nacos block if not need";
    }

    temp = ngx_create_pool(4096, cycle->log);
    if (temp == NULL) {
        return NGX_CONF_ERROR;
    }

    ha.pool = cycle->pool;
    ha.temp_pool = temp;
    ha.bucket_size = ncf->keys_bucket_size;
    ha.max_size = ncf->keys_hash_max_size;
    ha.key = ngx_hash_key_lc;
    ha.name = "nacos_keys_hash";
    ha.hash = NULL;

    n = ncf->keys.nelts;
    k = ncf->keys.elts;

    hkeys = ngx_palloc(ha.temp_pool, n * sizeof(*hkeys));
    if (hkeys == NULL) {
        ngx_destroy_pool(temp);
        return NGX_CONF_ERROR;
    }

    for (i = 0; i < n; ++i) {
        hkeys[i].key.len = ngx_snprintf(buf, sizeof(buf) - 1, "%V@@%V", &k[i].group, &k[i].data_id) - buf;
        hkeys[i].key.data = buf;
        hkeys[i].value = &k[i];
        hkeys[i].key_hash = ha.key(buf, hkeys[i].key.len);
    }

    if (ngx_hash_init(&ha, hkeys, n) != NGX_OK) {
        ngx_destroy_pool(temp);
        return NGX_CONF_ERROR;
    }
    ncf->key_hash = ha.hash;
    ngx_destroy_pool(temp);
    return NGX_CONF_OK;
}

static char *ngx_nacos_conf_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
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

    if (ngx_array_init(&ncf->server_list, cf->pool, 4, sizeof(ngx_addr_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ncf->keys_bucket_size = NGX_CONF_UNSET_UINT;
    ncf->keys_hash_max_size = NGX_CONF_UNSET_UINT;
    ncf->key_zone_size = NGX_CONF_UNSET_SIZE;
    ncf->udp_pool_size = NGX_CONF_UNSET_SIZE;

    pcf = *cf;
    cf->cmd_type = NGX_NACOS_MAIN_CONF;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        goto end;
    }

    if (!ncf->server_list.nelts) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nacos server_list is empty");
        rv = NGX_CONF_ERROR;
        goto end;
    }

    if (!ncf->udp_port.len || !ncf->udp_port.data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nacos udp_port is not config");
        rv = NGX_CONF_ERROR;
        goto end;
    }

    if ((i = ngx_atoi(ncf->udp_port.data, ncf->udp_port.len)) == NGX_ERROR) {
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

    if (!ncf->cache_dir.len) {
        ngx_str_set(&ncf->cache_dir, "nacos_cache");
    }
    if (ngx_conf_full_name(cf->cycle, &ncf->cache_dir, 0) != NGX_OK) {
        rv = NGX_CONF_ERROR;
        goto end;
    }
    if ((err = ngx_create_full_path(ncf->cache_dir.data, 0744))) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, err,
                           "nacos create cache dir \"%V\" error", &ncf->cache_dir);
        rv = NGX_CONF_ERROR;
        goto end;
    }
    if (!ncf->udp_ip.len || !ncf->udp_ip.data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nacos udp_ip is not config");
        rv = NGX_CONF_ERROR;
        goto end;
    }

    if (!ncf->default_group.data) {
        ngx_str_set(&ncf->default_group, "DEFAULT_GROUP");
    }

    if (!ncf->error_log) {
        ncf->error_log = &cf->cycle->new_log;
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
        memcpy(ncf->udp_bind.data + ncf->udp_ip.len + 1, ncf->udp_port.data, ncf->udp_port.len);
    }

    ngx_conf_init_size_value(ncf->key_zone_size, 16384);
    ngx_conf_init_size_value(ncf->udp_pool_size, 8192);
    ngx_conf_init_uint_value(ncf->keys_hash_max_size, 128);
    ngx_conf_init_uint_value(ncf->keys_bucket_size, 128);
    ncf->keys_bucket_size = ngx_align(ncf->keys_bucket_size, ngx_cacheline_size);

    if (ngx_nacos_aux_init(cf) != NGX_OK) {
        rv = NGX_CONF_ERROR;
        goto end;
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
    end:
    *cf = pcf;
    return rv;
}

static char *ngx_nacos_conf_server_list(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_nacos_main_conf_t *mcf;
    ngx_uint_t i, j, n;
    ngx_str_t *value;
    ngx_url_t u;
    ngx_addr_t *adr;

    mcf = conf;
    value = cf->args->elts;
    n = cf->args->nelts;

    for (i = 1; i < n; ++i) {
        memset(&u, 0, sizeof(u));
        u.url = value[i];
        u.default_port = 8848;
        if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "%s in nacos server_list \"%V\"", u.err, &u.url);
            }
            return NGX_CONF_ERROR;
        }

        for (j = 0; j < u.naddrs; ++j) {
            adr = ngx_array_push(&mcf->server_list);
            if (adr == NULL) {
                return NGX_CONF_ERROR;
            }
            *adr = u.addrs[j];
        }
    }

    return NGX_CONF_OK;
}

static char *ngx_nacos_conf_error_log(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_nacos_main_conf_t *mcf = conf;
    return ngx_log_set_log(cf, &mcf->error_log);
}

ngx_nacos_main_conf_t *ngx_nacos_get_main_conf(ngx_conf_t *cf) {
    return (ngx_nacos_main_conf_t *) cf->cycle->conf_ctx[ngx_nacos_module.index];
}

ngx_int_t ngx_nacos_subscribe(ngx_conf_t *cf, ngx_nacos_sub_t *sub) {
    ngx_nacos_main_conf_t *mcf;
    ngx_uint_t i, n;
    ngx_nacos_key_t * k;
    ngx_nacos_data_t tmp;
    ngx_int_t rc;
    ngx_str_t zone_name;

    mcf = ngx_nacos_get_main_conf(cf);

    tmp.data_id = sub->data_id;
    tmp.group = sub->group;
    if (!tmp.group.len) {
        tmp.group = mcf->default_group;
    }
    if (tmp.data_id.len + tmp.group.len + 2 >= 512) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "nacos data_id and group is too long");
        return NGX_ERROR;
    }

    if (mcf->keys.size == 0) {
        if (ngx_array_init(&mcf->keys, cf->pool, 16, sizeof(*k)) != NGX_OK) {
            return NGX_ERROR;
        }
        mcf->cur_srv_index = rand() % mcf->server_list.nelts;
    } else {
        n = mcf->keys.nelts;
        k = mcf->keys.elts;
        for (i = 0; i < n; ++i) {
            if (nacos_key_eq(k[i], tmp)) {
                *sub->key_ptr = &k[i];
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
        if (ngx_nacos_fetch_net_data(mcf, &tmp) != NGX_OK) {
            return NGX_ERROR;
        }
        if (ngx_nacos_write_disk_data(mcf, &tmp) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    k = ngx_array_push(&mcf->keys);
    if (k == NULL) {
        return NGX_ERROR;
    }

    k->data_id = tmp.data_id;
    k->group = tmp.group;
    k->ctx = ngx_palloc(cf->temp_pool, sizeof(ngx_nacos_key_ctx_t));
    if (k->ctx == NULL) {
        return NGX_ERROR;
    }
    k->ctx->wrlock = 0;
    k->ctx->version = tmp.version;
    k->ctx->addrs = tmp.adr;
    zone_name.len = tmp.data_id.len + tmp.group.len + 2;
    zone_name.data = ngx_palloc(cf->pool, zone_name.len + 1);
    ngx_snprintf(zone_name.data, zone_name.len + 1, "%V@@%V", &tmp.group, &tmp.data_id);
    k->zone = ngx_shared_memory_add(cf, &zone_name, mcf->key_zone_size, &ngx_nacos_module);
    if (k->zone == NULL) {
        return NGX_ERROR;
    }
    k->zone->noreuse = 1;
    k->zone->data = k;
    k->zone->init = ngx_nacos_init_key_zone;
    *sub->key_ptr = k;
    return NGX_OK;
}

static ngx_int_t ngx_nacos_init_key_zone(ngx_shm_zone_t *zone, void *data) {
    ngx_nacos_key_t * key;
    ngx_nacos_key_ctx_t *ctx;
    ngx_uint_t len;
    char *c;
    key = zone->data;
    ctx = key->ctx;
    c = ctx->addrs;

    len = *(ngx_uint_t *) c;
    c += sizeof(ngx_uint_t);
    key->sh = (ngx_slab_pool_t *) zone->shm.addr;
    key->ctx = ngx_slab_alloc_locked(key->sh, sizeof(*ctx));
    if (key->ctx == NULL) {
        return NGX_ERROR;
    }
    key->ctx->addrs = ngx_slab_alloc_locked(key->sh, len);
    if (key->ctx->addrs == NULL) {
        return NGX_ERROR;
    }
    key->ctx->wrlock = ctx->wrlock;
    key->ctx->version = ctx->version;
    memcpy(key->ctx->addrs, c, len);
    return NGX_OK;
}

ngx_int_t nax_nacos_get_addrs(ngx_nacos_key_t *key, ngx_uint_t *version, ngx_array_t *out_addrs) {
    ngx_uint_t old_ver, new_ver;
    ngx_int_t rc;
    ngx_nacos_key_ctx_t *ctx = key->ctx;
    old_ver = *version;
    rc = NGX_DECLINED;
    if (key->sh) {
        ngx_rwlock_rlock(&ctx->wrlock);
    }
    if (old_ver != (new_ver = ctx->version)) {
        rc = ngx_nacos_deep_copy_addrs(ctx->addrs, out_addrs);
    }
    if (key->sh) {
        ngx_rwlock_unlock(&ctx->wrlock);
    }
    *version = new_ver;
    return rc;
}
