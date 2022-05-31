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
    ngx_atomic_t wrlock;
    ngx_uint_t version;
    char *addrs;// [uint bytes_len][uint number][uint name_length][name_length name][uint sock_len][sock_len sock]....
} ngx_nacos_key_ctx_t;

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_shm_zone_t *zone;
    ngx_slab_pool_t *sh;
    ngx_nacos_key_ctx_t *ctx;
} ngx_nacos_key_t;

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_nacos_key_t **key_ptr;
} ngx_nacos_sub_t;

typedef struct {
    ngx_array_t server_list;// ngx_addr_t
    ngx_int_t cur_srv_index;
    ngx_str_t default_group;
    ngx_str_t udp_port;
    ngx_str_t udp_ip;
    ngx_str_t udp_bind;
    ngx_uint_t  keys_hash_max_size;
    ngx_uint_t keys_bucket_size;
    size_t key_zone_size;
    size_t udp_pool_size;
    ngx_log_t *error_log;
    ngx_str_t cache_dir;


    ngx_pool_t *pool;
    ngx_array_t *keys; //  ngx_nacos_key_t *
    ngx_hash_t *key_hash;
} ngx_nacos_main_conf_t;

ngx_nacos_main_conf_t *ngx_nacos_get_main_conf(ngx_conf_t *cf);

ngx_int_t ngx_nacos_subscribe(ngx_conf_t *cf, ngx_nacos_sub_t *sub);

static ngx_inline ngx_flag_t
ngx_nacos_addrs_change(ngx_nacos_key_t *key, const ngx_uint_t version) {
    ngx_uint_t new_version;
    ngx_nacos_key_ctx_t *ctx;

    ctx = key->ctx;
    if (key->sh) {
        ngx_rwlock_rlock(&ctx->wrlock);
    }
    new_version = ctx->version;
    if (key->sh) {
        ngx_rwlock_unlock(&ctx->wrlock);
    }
    if (new_version != version) {
        return 1;
    }
    return 0;
}

ngx_int_t nax_nacos_get_addrs(ngx_nacos_key_t *key, ngx_uint_t *version, ngx_array_t *out_addrs);

#define nacos_key_eq(a, b) ((a).data_id.len == (b).data_id.len \
            && ngx_strncasecmp((a).data_id.data, (b).data_id.data, (a).data_id.len) == 0 \
            && (a).group.len == (b).group.len                  \
            && ngx_strncasecmp((a).group.data, (b).group.data, (a).group.len) == 0)
#endif //NGINX_NACOS_NGX_NACOS_H
