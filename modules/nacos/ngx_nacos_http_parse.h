//
// Created by dear on 22-6-11.
//

#ifndef NGINX_NACOS_NGX_NACOS_HTTP_PARSE_H
#define NGINX_NACOS_NGX_NACOS_HTTP_PARSE_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_nacos.h>
#include <yaij/api/yajl_tree.h>

typedef struct {
    ngx_log_t *log;
    u_char *buf;
    size_t capacity;
    size_t offset;
    size_t limit;
    enum { none, cont_len, chunk, oef } body_type;
    enum { line, head, body, ended } parse_state;
    ngx_int_t status;
    enum { invalid, v_09, v_10, v_11 } http_version;
    ngx_int_t content_len;
    ngx_int_t real_body_len;
    signed json_body : 2;
    signed close_conn : 2;
    unsigned conn_eof : 1;  // already oef
    ngx_int_t chunk_size;
    yajl_tree_parser json_parser;
} ngx_nacos_http_parse_t;

ngx_int_t ngx_nacos_http_parse(ngx_nacos_http_parse_t *parse);

typedef ngx_int_t (*ngx_nacos_http_result_processor)(
    ngx_nacos_http_parse_t *parse, void *ctx);

ngx_int_t ngx_nacos_http_req_json_sync(
    ngx_nacos_main_conf_t *mcf, ngx_str_t *req,
    ngx_nacos_http_result_processor processor, void *ctx);

ngx_int_t ngx_nacos_http_append_user_pass_header(ngx_nacos_main_conf_t *mcf,
                                                 ngx_str_t *req,
                                                 size_t capacity);

#endif  // NGINX_NACOS_NGX_NACOS_HTTP_PARSE_H
