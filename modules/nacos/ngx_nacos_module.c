//
// Created by dear on 22-5-22.
//

#include <ngx_nacos.h>

static void *ngx_nacos_create_conf(ngx_cycle_t *cycle);

static char *ngx_nacos_init_conf(ngx_cycle_t *cycle, void *conf);

static char *ngx_nacos_conf_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_nacos_conf_server_list(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_nacos_conf_error_log(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void ngx_nacos_udp_handler(ngx_connection_t *c);

static u_char *ngx_nacos_log_error(ngx_log_t *log, u_char *buf, size_t len);


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
                ngx_string("key_pool_size"),
                NGX_NACOS_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
                ngx_conf_set_size_slot,
                0,
                offsetof(ngx_nacos_main_conf_t, key_pool_size),
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


#define NACOS_REQ_FMT "GET /nacos/v1/ns/instance/list?serviceName=%V%%40%%40%V" \
    "&udpPort=%V&clientIp=%V HTTP/1.0\r\n"                                      \
    "Host: %V\r\n"                                                              \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n"                                 \
    "Connection: close\r\n\r\n"                                                 \

#define NACOS_SUB_RESP_BUF_SIZE  (16 * 1024)

static void *ngx_nacos_create_conf(ngx_cycle_t *cycle) {

    return NULL;
}

static char *ngx_nacos_init_conf(ngx_cycle_t *cycle, void *conf) {
    ngx_nacos_main_conf_t *ncf = conf;
    if (ncf == NULL) {// no nacos config
        return NGX_CONF_OK;
    }

    return NGX_CONF_OK;
}

static char *ngx_nacos_conf_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_conf_t pcf;
    char *rv;
    ngx_int_t i;
    ngx_url_t u;
    ngx_listening_t *ls;
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

    ncf->key_pool_size = NGX_CONF_UNSET_SIZE;

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

    ngx_conf_init_size_value(ncf->key_pool_size, 4096);

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

    ls = ncf->udp_listen = ngx_create_listening(cf, u.addrs[0].sockaddr, u.addrs[0].socklen);
    if (ls == NULL) {
        rv = NGX_CONF_ERROR;
        goto end;
    }
    ls->type = SOCK_DGRAM;// udp
    ls->handler = ngx_nacos_udp_handler;
    ls->addr_ntop = 1;
    ls->pool_size = 4096;
    ls->logp = ncf->error_log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = ngx_nacos_log_error;
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

static void ngx_nacos_udp_handler(ngx_connection_t *c) {

}

static u_char *ngx_nacos_log_error(ngx_log_t *log, u_char *buf, size_t len) {
    return ngx_snprintf(buf, len, " while accepting new message on %V",
                        log->data);
}

static ngx_int_t ngx_nacos_deep_copy_addrs(ngx_array_t *src, ngx_array_t *dist) {
    ngx_addr_t *t, *v;
    ngx_uint_t i, n = src->nelts;
    t = src->elts;
    for (i = 0; i < n; ++i) {
        v = ngx_array_push(dist);
        if (v == NULL) {
            return NGX_ERROR;
        }
        if (t[i].name.len) {
            v->name.len = t[i].name.len;
            v->name.data = ngx_palloc(dist->pool, v->name.len);
            if (v->name.data == NULL) {
                return NGX_ERROR;
            }
            memcpy(v->name.data, t[i].name.data, v->name.len);
        } else {
            v->name.len = 0;
            v->name.data = NULL;
        }

        v->socklen = t[i].socklen;
        v->sockaddr = ngx_palloc(dist->pool, v->socklen);
        if (v->sockaddr == NULL) {
            return NGX_ERROR;
        }
        memcpy(v->sockaddr, t[i].sockaddr, v->socklen);
    }
    return NGX_OK;
}

ngx_int_t ngx_nacos_subscribe(ngx_conf_t *cf, ngx_nacos_sub_t *sub) {
    ngx_nacos_main_conf_t *mcf;
    ngx_uint_t i, n, len, tries;
    ssize_t rd, sd;
    ngx_addr_t *addrs;
    char *tbuf;
    u_char rbuf[1024];
    ngx_err_t err;
    ngx_socket_t s;
    ngx_nacos_key tmp, *key;

    mcf = ngx_nacos_get_main_conf(cf);

    if (!mcf->keys_pool) {
        mcf->keys_pool = ngx_create_pool(mcf->key_pool_size, cf->log);
        if (mcf->keys_pool == NULL) {
            return NGX_ERROR;
        }
        if (ngx_array_init(&mcf->keys, mcf->keys_pool, 4, sizeof(ngx_nacos_key)) != NGX_OK) {
            ngx_destroy_pool(mcf->keys_pool);
            mcf->keys_pool = NULL;
            return NGX_ERROR;
        }

        mcf->cur_srv_index = rand() % mcf->server_list.nelts;
    }

    tmp.data_id = sub->data_id;
    tmp.group = sub->group;
    if (!tmp.group.len) {
        tmp.group = mcf->default_group;
    }


    n = mcf->keys.nelts;
    key = mcf->keys.elts;
    for (i = 0; i < n; ++i) {
        if (nacos_key_eq(tmp, key[i])) {
            return ngx_nacos_deep_copy_addrs(&key->addrs, sub->out_addrs);
        }
    }


    addrs = mcf->server_list.elts;

    tbuf = NULL;
    tries = 0;

    tbuf = malloc(NACOS_SUB_RESP_BUF_SIZE);// 16k
    if (tbuf == NULL) {
        goto fetch_failed;
    }

    s = -1;
    retry:
    if (++tries > mcf->server_list.nelts) {
        goto fetch_failed;
    }

    n = mcf->server_list.nelts;
    i = (mcf->cur_srv_index++) % n;
    if (mcf->cur_srv_index >= n) {
        mcf->cur_srv_index = 0;
    }

    if (s > 0) {
        close(s);
    }
    s = ngx_socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        return NGX_ERROR;
    }

    if (connect(s, addrs[i].sockaddr, addrs[i].socklen) != 0) {
        err = ngx_socket_errno;
        ngx_log_error(NGX_LOG_WARN, cf->log, err, "nacos connect() to %V failed",
                      addrs[i].name);
        goto retry;
    }


    len = ngx_snprintf((u_char *) rbuf, sizeof(rbuf), NACOS_REQ_FMT,
                       &tmp.group,
                       &tmp.data_id,
                       &mcf->udp_port,
                       &mcf->udp_ip,
                       &addrs[i].name) - rbuf;

    sd = 0;
    do {
        rd = ngx_write_fd(s, rbuf, len);
        if (rd > 0) {
            sd += rd;
        } else if (rd == 0) {
            ngx_log_error(NGX_LOG_WARN, cf->log, 0, "write request to %V failed, because of EOF occur",
                          &addrs[i].name);
            goto retry;
        } else {
            err = ngx_socket_errno;
            ngx_log_error(NGX_LOG_WARN, cf->log, err, "write request to %V failed",
                          &addrs[i].name);
            goto retry;
        }
    } while (sd < len);

    sd = 0;
    do {
        rd = ngx_read_fd(s, tbuf, NACOS_SUB_RESP_BUF_SIZE);
        if (rd > 0) {
            sd += rd;
        } else if (rd == -1) {
            err = ngx_socket_errno;
            ngx_log_error(NGX_LOG_WARN, cf->log, err, "read response from %V failed",
                          &addrs[i].name);
            goto retry;
        }
    } while (rd);


    fetch_failed:
    if (s > 0) {
        close(s);
    }
    if (tbuf) {
        free(tbuf);
    }
    ngx_log_error(NGX_LOG_WARN, cf->log, 0, "subscribe to nacos servers failed");
    return NGX_ERROR;
}
