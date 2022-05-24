//
// Created by dear on 22-5-20.
//

#include <ngx_nacos.h>
#include <ngx_http_nacos.h>

static void *ngx_http_nacos_create_main_conf(ngx_conf_t *cf);

static ngx_int_t ngx_http_nacos_post_conf(ngx_conf_t *cf);

static void *ngx_http_nacos_create_srv_conf(ngx_conf_t *cf);

static char *ngx_http_conf_use_nacos_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_nacos_init_upstream(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us);

static ngx_http_module_t ngx_http_nacos_module_ctx = {
        NULL,                                  /* preconfiguration */
        ngx_http_nacos_post_conf,       /* postconfiguration */

        ngx_http_nacos_create_main_conf,    /* create main configuration */
        NULL,                                 /* init main configuration */

        ngx_http_nacos_create_srv_conf,    /* create server configuration */
        NULL,                                  /* merge server configuration */

        NULL,        /* create location configuration */
        NULL                                   /* merge location configuration */
};

static ngx_command_t cmds[] = {
        {
                ngx_string("use_nacos_address"),
                NGX_HTTP_UPS_CONF | NGX_CONF_TAKE12,
                ngx_http_conf_use_nacos_address,
                NGX_HTTP_SRV_CONF_OFFSET,
                0,
                NULL
        },
        ngx_null_command
};

ngx_module_t ngx_http_nacos_module = {
        NGX_MODULE_V1,
        &ngx_http_nacos_module_ctx,
        cmds,
        NGX_HTTP_MODULE,
        NULL,                                  /* init master */
        NULL,                                  /* init module */
        NULL,                                  /* init process */
        NULL,                                  /* init thread */
        NULL,                                  /* exit thread */
        NULL,                                  /* exit process */
        NULL,                                  /* exit master */
        NGX_MODULE_V1_PADDING
};

static void *ngx_http_nacos_create_main_conf(ngx_conf_t *cf) {
    ngx_http_nacos_main_conf_t *mncf = ngx_pcalloc(cf->pool, sizeof(*mncf));
    if (mncf == NULL) {
        return NULL;
    }
    mncf->udp_port = NGX_CONF_UNSET_UINT;
    if (ngx_array_init(&mncf->confs, cf->pool, 4, sizeof(ngx_http_nacos_conf_t)) != NGX_OK) {
        return NULL;
    }
    return mncf;
}


static void *ngx_http_nacos_create_srv_conf(ngx_conf_t *cf) {
    return ngx_pcalloc(cf->pool, sizeof(ngx_http_nacos_srv_conf_t));
}

static char *ngx_http_conf_use_nacos_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_nacos_srv_conf_t *nlcf = conf;
    ngx_uint_t i;
    ngx_uint_t n = cf->args->nelts;
    ngx_str_t *value = cf->args->elts;
    ngx_nacos_sub_t tmp;
    ngx_url_t u;
    ngx_http_nacos_main_conf_t *mncf;
    ngx_nacos_main_conf_t *mf;
    ngx_http_nacos_conf_t *ncfs;

    if (nlcf->uscf) {
        return "is duplicate";
    }

    memset(&u, 0, sizeof(u));

    for (i = 1; i < n; ++i) {
        if (value[i].len > 8 && ngx_strncmp(value[i].data, "data_id=", 8) == 0) {
            tmp.data_id.data = value[i].data + 8;
            tmp.data_id.len = value[i].len - 8;
            continue;
        }
        if (value[i].len > 6 && ngx_strncmp(value[i].data, "group=", 6) == 0) {
            tmp.group.data = value[i].data + 6;
            tmp.group.len = value[i].len - 6;
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (!tmp.data_id.len) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "require data_id");
        return NGX_CONF_ERROR;
    }

    mncf = ngx_http_conf_get_module_main_conf(cf, ngx_http_nacos_module);

    n = mncf->confs.nelts;
    ncfs = mncf->confs.elts;
    for (i = 0; i < n; ++i) {
        if (nacos_key_eq(tmp, ncfs[i])) {
            break;
        }
    }
    if (i < n) {
        nlcf->ncf = &ncfs[i];
        return NGX_CONF_OK;
    }

    ncfs = ngx_array_push(&mncf->confs);
    if (ncfs == NULL) {
        return NGX_CONF_ERROR;
    }

    ncfs->data_id = tmp.data_id;
    ncfs->group = tmp.group;
    nlcf->ncf = ncfs;
    nlcf->uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);
    nlcf->origin_init_upstream = nlcf->uscf->peer.init_upstream;
    if (!nlcf->origin_init_upstream) {
        nlcf->origin_init_upstream = ngx_http_upstream_init_round_robin;
    }
    nlcf->uscf->peer.init_upstream = ngx_http_nacos_init_upstream;

    mf = ngx_nacos_get_main_conf(cf);
    if (mf == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nacos block is required before");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_nacos_post_conf(ngx_conf_t *cf) {
    ngx_str_t tmp;
    ngx_http_nacos_main_conf_t *mncf;
    ngx_http_nacos_conf_t *ncf;
    ngx_uint_t n, i;
    mncf = ngx_http_conf_get_module_main_conf(cf, ngx_http_nacos_module);

    ncf = mncf->confs.elts;
    n = mncf->confs.nelts;
    for (i = 0; i < n; ++i) {
        tmp.len = ncf[i].data_id.len + ncf[i].group.len + 2;
        tmp.data = ngx_pcalloc(cf->pool, tmp.len);
        if (!tmp.data) {
            return NGX_ERROR;
        }

        // tmp == "data_id@@group"
        memcpy(tmp.data, ncf[i].data_id.data, ncf[i].data_id.len);
        tmp.data[ncf[i].data_id.len] = '@';
        tmp.data[ncf[i].data_id.len + 1] = '@';
        memcpy(tmp.data + 2, ncf[i].group.data, ncf[i].group.len);

    }

    return NGX_OK;
}

static ngx_int_t ngx_http_nacos_init_upstream(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us) {
    ngx_nacos_sub_t sub;
    ngx_array_t addrs;
    ngx_uint_t i, n;
    ngx_http_upstream_server_t *server;
    ngx_addr_t *adr;
    ngx_http_nacos_srv_conf_t *ncf = ngx_http_conf_upstream_srv_conf(us, ngx_http_nacos_module);

    if (ngx_array_init(&addrs, us->servers->pool, 16, sizeof(ngx_addr_t)) != NGX_OK) {
        return NGX_ERROR;
    }
    sub.out_addrs = &addrs;
    sub.data_id = ncf->ncf->data_id;
    sub.group = ncf->ncf->group;
    if (ngx_nacos_subscribe(cf, &sub) != NGX_OK) {
        return NGX_ERROR;
    }

    n = addrs.nelts;
    adr = addrs.elts;
    for (i = 0; i < n; ++i) {
        server = ngx_array_push(us->servers);
        if (server == NULL) {
            return NGX_ERROR;
        }
        server->addrs = &adr[i];
        server->naddrs = 1;
        server->name = adr[i].name;
        server->weight = 1;
        server->down = 0;
        server->backup = 0;
        server->max_conns = 128;
        server->fail_timeout = 3000;
    }


    return ncf->origin_init_upstream(cf, us);
}
