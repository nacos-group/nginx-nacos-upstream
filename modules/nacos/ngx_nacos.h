//
// Created by dear on 22-5-22.
//

#ifndef NGINX_NACOS_NGX_NACOS_H
#define NGINX_NACOS_NGX_NACOS_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event_connect.h>

#define NGX_NACOS_MODULE 0x4e41434f /*NACO*/
#define NGX_NACOS_MAIN_CONF 0x02000000

typedef struct {
    ngx_atomic_t wrlock;
    ngx_uint_t version;
    // server_list [uint bytes_len][uint number][uint name_length][name_length
    // name][uint sock_len][sock_len sock].... config [uint md5_len][md5_len
    // md5][uint content_len][content_len content]
    char *data;
} ngx_nacos_key_ctx_t;

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_flag_t use_shared;
    ngx_nacos_key_ctx_t *ctx;
} ngx_nacos_key_t;

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_nacos_key_t **key_ptr;
} ngx_nacos_sub_t;

typedef struct {
    ngx_array_t server_list;       // ngx_addr_t
    ngx_array_t grpc_server_list;  // ngx_addr_t
    ngx_uint_t cur_srv_index;
    ngx_str_t default_group;
    ngx_str_t udp_port;
    ngx_str_t udp_ip;
    ngx_str_t udp_bind;
    ngx_str_t server_host;
    ngx_str_t config_tenant;
    ngx_str_t service_namespace;
    ngx_str_t username;
    ngx_str_t password;
    ngx_uint_t keys_hash_max_size;
    ngx_uint_t keys_bucket_size;
    ngx_uint_t config_keys_hash_max_size;
    ngx_uint_t config_keys_bucket_size;
    size_t key_zone_size;
    size_t udp_pool_size;
    ngx_log_t *error_log;
    ngx_str_t cache_dir;

    ngx_array_t keys;         //  ngx_nacos_key_t *
    ngx_array_t config_keys;  // ngx_nacos_key_t *
    ngx_shm_zone_t *zone;
    ngx_slab_pool_t *sh;
    ngx_hash_t *key_hash;
    ngx_hash_t *config_key_hash;
    ngx_addr_t udp_addr;
} ngx_nacos_main_conf_t;

ngx_nacos_main_conf_t *ngx_nacos_get_main_conf(ngx_conf_t *cf);

ngx_int_t ngx_nacos_subscribe_naming(ngx_conf_t *cf, ngx_nacos_sub_t *sub);

ngx_int_t ngx_nacos_subscribe_config(ngx_conf_t *cf, ngx_nacos_sub_t *sub);

void ngx_nacos_aux_free_addr(ngx_peer_connection_t *pc, void *data,
                             ngx_uint_t state);

ngx_int_t ngx_nacos_aux_get_addr(ngx_peer_connection_t *pc, void *data);

ngx_int_t nax_nacos_get_addrs(ngx_nacos_key_t *key, ngx_uint_t *version,
                              ngx_array_t *out_addrs);

typedef struct {
    ngx_pool_t *pool;
    ngx_uint_t ref;
    ngx_uint_t version;
    ngx_str_t out_config;
} ngx_nacos_config_fetcher_t;

ngx_int_t nax_nacos_get_config(ngx_nacos_key_t *key,
                               ngx_nacos_config_fetcher_t *out);

#define nacos_key_eq(a, b)                                 \
    ((a)->data_id.len == (b)->data_id.len &&               \
     ngx_strncasecmp((a)->data_id.data, (b)->data_id.data, \
                     (a)->data_id.len) == 0 &&             \
     (a)->group.len == (b)->group.len &&                   \
     ngx_strncasecmp((a)->group.data, (b)->group.data, (a)->group.len) == 0)

#define NGX_NC_ERROR 2
#define NGX_NC_TIRED 4

#endif  // NGINX_NACOS_NGX_NACOS_H
