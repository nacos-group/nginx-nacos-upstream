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
} ngx_nacos_addr_resp_parser_t;

char *ngx_nacos_parse_addrs_from_json(ngx_nacos_addr_resp_parser_t *parser);

ngx_int_t ngx_nacos_fetch_disk_data(ngx_nacos_main_conf_t *mcf, ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_write_disk_data(ngx_nacos_main_conf_t *mcf, ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_fetch_net_data(ngx_nacos_main_conf_t *mcf, ngx_nacos_data_t *cache);

ngx_int_t ngx_nacos_deep_copy_addrs(char *src, ngx_array_t *dist);

ngx_int_t ngx_nacos_update_addrs(ngx_nacos_key_t *key, const char *adr, ngx_log_t *log);


#endif //NGINX_NACOS_NGX_NACOS_DATA_H
