//
// Created by dear on 22-5-20.
//

#include <ngx_nacos.h>
#include <ngx_http_nacos.h>

typedef struct {
    ngx_pool_t *pool;
    ngx_uint_t ref;
    ngx_nacos_key_t *key;
    ngx_uint_t version;
    ngx_array_t addrs;// ngx_addr_t
    ngx_http_upstream_srv_conf_t *us;
    // origin
    void *original_data;
    ngx_event_get_peer_pt original_get_peer;
    ngx_event_free_peer_pt original_free_peer;
#if (NGX_HTTP_SSL)
    ngx_event_set_peer_session_pt original_set_session;
    ngx_event_save_peer_session_pt original_save_session;
#endif
} ngx_http_nacos_peers_t;

static void *ngx_http_nacos_create_srv_conf(ngx_conf_t *cf);

static char *ngx_http_conf_use_nacos_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_nacos_init_upstream(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us);

static ngx_int_t ngx_http_nacos_init_peers(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *us);

static ngx_int_t ngx_http_nacos_create_new_us(ngx_http_nacos_peers_t *new_peers, ngx_http_upstream_srv_conf_t *us);

static ngx_http_nacos_peers_t *ngx_http_get_nacos_peers(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *us);

static ngx_int_t ngx_http_nacos_get_peer(ngx_peer_connection_t *pc, void *data);

static void ngx_http_nacos_free_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t state);

#if (NGX_HTTP_SSL)

static ngx_int_t ngx_http_nacos_peer_session(ngx_peer_connection_t *pc, void *data);

static void ngx_http_nacos_save_peer_session(ngx_peer_connection_t *pc, void *data);

#endif

static ngx_http_module_t ngx_http_nacos_module_ctx = {
        NULL,                                  /* preconfiguration */
        NULL,                           /* postconfiguration */

        NULL,                                /* create main configuration */
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
    ngx_nacos_main_conf_t *mf;

    if (nlcf->uscf) {
        return "is duplicate";
    }

    ngx_memzero(&u, sizeof(u));
    ngx_memzero(&tmp, sizeof(tmp));

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

    nlcf->data_id = tmp.data_id;
    nlcf->group = tmp.group;
    nlcf->uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);
    nlcf->original_init_upstream = nlcf->uscf->peer.init_upstream;
    if (!nlcf->original_init_upstream) {
        nlcf->original_init_upstream = ngx_http_upstream_init_round_robin;
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

u_char *ngx_http_nacos_log_handler(ngx_log_t *log, u_char *buf, size_t len) {
    ngx_http_nacos_peers_t *peers;
    u_char * p = buf;
    if (log->action) {
        p = ngx_snprintf(buf, len, " while %s", log->action);
        len -= p - buf;
    }

    peers = log->data;
    p = ngx_snprintf(p, len, ": %V:%V", &peers->key->group, &peers->key->data_id);
    return p;
}

static ngx_http_nacos_peers_t *ngx_http_nacos_create_peers(ngx_log_t *log) {
    ngx_pool_t *pool;
    ngx_http_nacos_peers_t *peers;
    ngx_log_t *new_log;

    pool = ngx_create_pool(1024, log);
    if (pool == NULL) {
        return NULL;
    }

    new_log = ngx_palloc(pool, sizeof(ngx_log_t));
    if (new_log == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    *new_log = *log;
    pool->log = new_log;


    peers = ngx_pcalloc(pool, sizeof(ngx_http_nacos_peers_t));
    if (peers == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    new_log->data = peers;
    new_log->handler = ngx_http_nacos_log_handler;
    new_log->action = "nacos update addrs";

    peers->pool = pool;
    peers->ref = 1;
    if (ngx_array_init(&peers->addrs, peers->pool, 16, sizeof(ngx_addr_t)) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    return peers;
}

static ngx_int_t ngx_http_nacos_add_server(ngx_http_nacos_peers_t *peers) {
    ngx_http_upstream_server_t *server;
    ngx_http_upstream_srv_conf_t *us;
    ngx_addr_t *adr;
    ngx_uint_t i, n;
    us = peers->us;

    n = peers->addrs.nelts;
    adr = peers->addrs.elts;
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
    return NGX_OK;
}

static ngx_int_t ngx_http_nacos_init_upstream(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us) {
    ngx_nacos_sub_t sub;
    ngx_pool_t *pool;
    ngx_http_nacos_peers_t *peers;
    ngx_http_nacos_srv_conf_t *ncf;
    ngx_conf_t new_cf;

    ncf = ngx_http_conf_upstream_srv_conf(us, ngx_http_nacos_module);
    peers = ngx_http_nacos_create_peers(cf->log);
    if (peers == NULL) {
        return NGX_ERROR;
    }

    pool = peers->pool;
    if (ngx_http_nacos_create_new_us(peers, us) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

    sub.key_ptr = &peers->key;
    sub.data_id = ncf->data_id;
    sub.group = ncf->group;
    if (ngx_nacos_subscribe(cf, &sub) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

    if (!ngx_nacos_addrs_change(peers->key, peers->version)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "nacos no addrs????");
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

    if (nax_nacos_get_addrs(peers->key, &peers->version, &peers->addrs) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

    if (ngx_http_nacos_add_server(peers) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

    if (peers->addrs.nelts > 0) {
        new_cf = *cf;
        new_cf.pool = pool;
        if (ncf->original_init_upstream(&new_cf, peers->us) != NGX_OK) {
            ngx_destroy_pool(pool);
            return NGX_ERROR;
        }
    }

    us->peer.init = ngx_http_nacos_init_peers;
    us->peer.data = peers;
    return NGX_OK;
}

static ngx_int_t ngx_http_nacos_create_new_us(ngx_http_nacos_peers_t *new_peers, ngx_http_upstream_srv_conf_t *us) {
    ngx_http_upstream_srv_conf_t *new_us;

    new_us = ngx_palloc(new_peers->pool, sizeof(*new_us));
    if (new_us == NULL) {
        return NGX_ERROR;
    }
    *new_us = *us;
    new_us->servers = ngx_array_create(new_peers->pool, 16, sizeof(ngx_http_upstream_server_t));
    if (new_us->servers == NULL) {
        return NGX_ERROR;
    }
    new_peers->us = new_us;
    return NGX_OK;
}

static ngx_int_t ngx_http_nacos_init_peers(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *us) {
    ngx_http_nacos_peers_t *peers;
    ngx_int_t rc;

    peers = ngx_http_get_nacos_peers(r, us);
    if (peers == NULL || peers->addrs.nelts == 0) {
        return NGX_ERROR;
    }
    rc = peers->us->peer.init(r, peers->us);
    if (rc != NGX_OK) {
        return rc;
    }

    peers->original_data = r->upstream->peer.data;
    peers->original_get_peer = r->upstream->peer.get;
    peers->original_free_peer = r->upstream->peer.free;

    r->upstream->peer.data = peers;
    r->upstream->peer.get = ngx_http_nacos_get_peer;
    r->upstream->peer.free = ngx_http_nacos_free_peer;
#if (NGX_HTTP_SSL)
    peers->original_set_session = r->upstream->peer.set_session;
    peers->original_save_session = r->upstream->peer.save_session;
    r->upstream->peer.set_session = ngx_http_nacos_peer_session;
    r->upstream->peer.save_session = ngx_http_nacos_save_peer_session;
#endif

    return NGX_OK;
}

static ngx_http_nacos_peers_t *ngx_http_get_nacos_peers(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *us) {
    ngx_http_nacos_peers_t *peers, *new_peers;
    ngx_http_nacos_srv_conf_t *nusf;
    ngx_int_t rc;
    ngx_conf_t cf;

    peers = us->peer.data;

    if (!ngx_nacos_addrs_change(peers->key, peers->version)) {
        return peers;
    }

    new_peers = ngx_http_nacos_create_peers(r->pool->log);
    if (new_peers == NULL) {
        return NULL;
    }
    new_peers->key = peers->key;
    new_peers->version = peers->version;
    rc = nax_nacos_get_addrs(new_peers->key, &new_peers->version, &new_peers->addrs);
    if (rc != NGX_OK) {
        ngx_destroy_pool(new_peers->pool);
        return NULL;
    }

    rc = ngx_http_nacos_create_new_us(new_peers, peers->us);
    if (rc != NGX_OK) {
        ngx_destroy_pool(new_peers->pool);
        return NULL;
    }

    if (ngx_http_nacos_add_server(new_peers) != NGX_OK) {
        ngx_destroy_pool(new_peers->pool);
        return NULL;
    }

    if (new_peers->addrs.nelts > 0) {
        memset(&cf, 0, sizeof(cf));
        cf.pool = new_peers->pool;
        cf.temp_pool = r->pool;
        cf.log = r->connection->log;
        nusf = ngx_http_conf_upstream_srv_conf(us, ngx_http_nacos_module);
        if (nusf->original_init_upstream(&cf, new_peers->us) != NGX_OK) {
            ngx_destroy_pool(new_peers->pool);
            return NULL;
        }
    }
    us->peer.data = new_peers;
    if (--peers->ref == 0) {
        ngx_destroy_pool(peers->pool);
    }
    return new_peers;
}


static ngx_int_t ngx_http_nacos_get_peer(ngx_peer_connection_t *pc, void *data) {
    ngx_http_nacos_peers_t *peers;
    ngx_int_t rc;

    peers = data;
    if ((rc = peers->original_get_peer(pc, peers->original_data)) != NGX_OK) {
        return rc;
    }
    peers->ref++;
    return NGX_OK;
}

static void ngx_http_nacos_free_peer(ngx_peer_connection_t *pc, void *data, ngx_uint_t state) {
    ngx_http_nacos_peers_t *peers;

    peers = data;
    peers->original_free_peer(pc, peers->original_data, state);
    if (--peers->ref == 0) {
        ngx_destroy_pool(peers->pool);
    }
}

#if (NGX_HTTP_SSL)

static ngx_int_t ngx_http_nacos_peer_session(ngx_peer_connection_t *pc, void *data) {
    ngx_http_nacos_peers_t *peers = data;
    return peers->original_set_session(pc, peers->original_data);
}

static void ngx_http_nacos_save_peer_session(ngx_peer_connection_t *pc, void *data) {
    ngx_http_nacos_peers_t *peers = data;
    peers->original_save_session(pc, peers->original_data);
}

#endif
