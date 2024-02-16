//
// Created by eleme on 2023/4/20.
//

#ifndef NGINX_NACOS_NGX_NACOS_UDP_H
#define NGINX_NACOS_NGX_NACOS_UDP_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event_connect.h>
#include <ngx_nacos.h>

typedef struct ngx_aux_nacos_conn_s ngx_aux_nacos_conn_t;

typedef struct {
    ngx_nacos_main_conf_t *ncf;
    ngx_aux_nacos_conn_t *nc;
    ngx_uint_t key_idx;
    ngx_connection_t *uc;
    ngx_uint_t sub_keys;
    ngx_msec_t heart_time;
    size_t udp_remote_addr_text_len;
    u_char *udp_remote_addr_text;
} ngx_nacos_udp_conn_t;

ngx_nacos_udp_conn_t *ngx_nacos_open_udp(ngx_nacos_main_conf_t *ncf);

#endif  // NGINX_NACOS_NGX_NACOS_UDP_H
