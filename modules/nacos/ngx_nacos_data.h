//
// Created by dear on 22-5-30.
//

#ifndef NGINX_NACOS_NGX_NACOS_DATA_H
#define NGINX_NACOS_NGX_NACOS_DATA_H

#include <ngx_nacos.h>
#include <yaij/api/yajl_tree.h>

typedef struct {
    ngx_str_t group;
    ngx_str_t data_id;
    ngx_pool_t *pool;
    char *adr;
    ngx_uint_t version;
} ngx_nacos_data_t;

typedef struct {
    yajl_val json;
    ngx_uint_t prev_version;
    ngx_pool_t *pool;
    ngx_log_t *log;
    ngx_uint_t current_version;
    size_t out_buf_len;
    char *out_buf;
} ngx_nacos_resp_json_parser_t;

char *ngx_nacos_parse_addrs_from_json(ngx_nacos_resp_json_parser_t *parser);

char *ngx_nacos_parse_config_from_json(ngx_nacos_resp_json_parser_t *parser);

ngx_int_t ngx_nacos_fetch_disk_data(ngx_nacos_main_conf_t *mcf,
                                    ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_write_disk_data(ngx_nacos_main_conf_t *mcf,
                                    ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_fetch_addrs_net_data(ngx_nacos_main_conf_t *mcf,
                                         ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_fetch_config_net_data(ngx_nacos_main_conf_t *mcf,
                                          ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_deep_copy_addrs(char *src, ngx_nacos_service_addrs_t *dist);

ngx_int_t ngx_nacos_update_shm(ngx_nacos_main_conf_t *mcf, ngx_nacos_key_t *key,
                               const char *adr, ngx_log_t *log);

ngx_nacos_key_t *ngx_nacos_hash_find_key(ngx_hash_t *key_hash, u_char *k);

ngx_int_t ngx_nacos_get_config_md5(ngx_nacos_key_t *key, ngx_str_t *buf);

static ngx_inline ngx_uint_t ngx_nacos_shmem_version(ngx_nacos_key_t *key) {
    ngx_uint_t v;
    ngx_nacos_key_ctx_t *ctx;
    ctx = key->ctx;

    if (key->use_shared) {
        ngx_rwlock_rlock(&ctx->wrlock);
    }
    v = ctx->version;
    if (key->use_shared) {
        ngx_rwlock_unlock(&ctx->wrlock);
    }
    return v;
}

static ngx_inline ngx_flag_t ngx_nacos_shmem_change(ngx_nacos_key_t *key,
                                                    const ngx_uint_t version) {
    if (ngx_nacos_shmem_version(key) != version) {
        return 1;
    }
    return 0;
}

#endif  // NGINX_NACOS_NGX_NACOS_DATA_H
