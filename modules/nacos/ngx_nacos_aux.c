//
// Created by dear on 22-6-1.
//

#include <ngx_auxiliary_module.h>
#include <ngx_event_connect.h>
#include <ngx_nacos.h>
#include <ngx_nacos_aux.h>
#include <ngx_nacos_data.h>
#include <ngx_nacos_grpc.h>
#include <ngx_nacos_http_parse.h>
#include <ngx_nacos_udp.h>

static ngx_int_t ngx_aux_nacos_proc_handler(ngx_cycle_t *cycle,
                                            ngx_aux_proc_t *p);

typedef struct {
    ngx_nacos_main_conf_t *ncf;
    ngx_nacos_udp_conn_t *uc;
    ngx_nacos_grpc_ctx_t *gc;
    ngx_uint_t err_times;
} ngx_nacos_aux_ctx_t;

static ngx_nacos_aux_ctx_t aux_ctx;

static ngx_aux_proc_t aux_proc = {ngx_string("nacos"), NULL,
                                  ngx_aux_nacos_proc_handler};

ngx_int_t ngx_nacos_aux_init(ngx_conf_t *cf) {
    if (aux_proc.data != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "ngx_nacos_aux_init inited");
        return NGX_ERROR;
    }
    aux_ctx.ncf = ngx_nacos_get_main_conf(cf);
    aux_proc.data = &aux_ctx;
    return ngx_aux_add_proc(cf, &aux_proc);
}

static ngx_int_t ngx_aux_nacos_proc_handler(ngx_cycle_t *cycle,
                                            ngx_aux_proc_t *p) {
    ngx_nacos_udp_conn_t *uc;
    ngx_nacos_grpc_ctx_t *gc;
    ngx_nacos_main_conf_t *ncf = aux_ctx.ncf;

    if (ncf->udp_port.len && ncf->udp_port.data) {
        uc = ngx_nacos_open_udp(ncf);
        if (uc == NULL) {
            return NGX_ERROR;
        }

        aux_ctx.uc = uc;
    } else {
        gc = ngx_nacos_open_grpc_ctx(ncf);
        if (gc == NULL) {
            return NGX_ERROR;
        }
        aux_ctx.gc = gc;
    }

    return NGX_OK;
}

void ngx_nacos_aux_free_addr(ngx_peer_connection_t *pc, void *data,
                             ngx_uint_t state) {
    ngx_uint_t i, n;
    ngx_array_t *adr_list;

    adr_list = data;
    i = aux_ctx.ncf->cur_srv_index;
    n = (ngx_uint_t) adr_list->nelts;

    if (state == NGX_NC_TIRED) {
        aux_ctx.err_times = 0;
        ngx_log_error(NGX_LOG_INFO, pc->log, 0,
                      "closing connection from nacos:%V", pc->name);
        return;
    }
    aux_ctx.err_times++;
    aux_ctx.ncf->cur_srv_index = ++i % n;
    ngx_log_error(NGX_LOG_ERR, aux_ctx.ncf->error_log, 0,
                  "use next addr because of error");
}

ngx_int_t ngx_nacos_aux_get_addr(ngx_peer_connection_t *pc, void *data) {
    ngx_addr_t *t;
    ngx_array_t *adr_list;
    ngx_uint_t i, n;

    adr_list = data;
    t = adr_list->elts;
    i = aux_ctx.ncf->cur_srv_index;
    n = adr_list->nelts;
    if (i >= n) {
        i %= n;
    }

    pc->name = &t[i].name;
    pc->sockaddr = t[i].sockaddr;
    pc->socklen = t[i].socklen;
    ngx_log_error_core(NGX_LOG_INFO, pc->log, 0,
                       "connecting to nacos server: %V", pc->name);
    return NGX_OK;
}