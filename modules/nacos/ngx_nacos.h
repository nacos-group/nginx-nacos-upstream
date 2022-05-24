//
// Created by dear on 22-5-22.
//

#ifndef NGINX_NACOS_NGX_NACOS_H
#define NGINX_NACOS_NGX_NACOS_H

#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_NACOS_MODULE    0x4e41434f /*NACO*/
#define NGX_NACOS_MAIN_CONF 0x02000000

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_array_t addrs; // ngx_addr_t t
} ngx_nacos_key;

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_array_t *out_addrs;
} ngx_nacos_sub_t;

typedef struct {
    ngx_array_t server_list;// ngx_addr_t
    ngx_uint_t cur_srv_index;
    ngx_str_t default_group;
    ngx_str_t udp_port;
    ngx_str_t udp_ip;
    ngx_str_t udp_bind;
    size_t key_pool_size;
    ngx_log_t *error_log;

    ngx_listening_t *udp_listen;
    ngx_pool_t *keys_pool;
    ngx_array_t keys; // ngx_nacos_key
} ngx_nacos_main_conf_t;

ngx_nacos_main_conf_t *ngx_nacos_get_main_conf(ngx_conf_t *cf);

ngx_int_t ngx_nacos_subscribe(ngx_conf_t *cf, ngx_nacos_sub_t *sub);

#define nacos_key_eq(a, b) ((a).data_id.len == (b).data_id.len \
            && ngx_strncmp((a).data_id.data, (b).data_id.data, (a).data_id.len) == 0 \
            && (a).group.len == (b).group.len                  \
            && ngx_strncmp((a).group.data, (b).group.data, (a).group.len) == 0)
#endif //NGINX_NACOS_NGX_NACOS_H
