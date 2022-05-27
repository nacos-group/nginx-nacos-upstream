//
// Created by dear on 22-5-20.
//

#ifndef NGINX_NACOS_NGX_HTTP_NACOS_H
#define NGINX_NACOS_NGX_HTTP_NACOS_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_array_t confs; // ngx_http_nacos_conf_t
    ngx_int_t udp_port;
} ngx_http_nacos_main_conf_t;

typedef struct {
    ngx_str_t data_id;
    ngx_str_t group;
    ngx_shm_zone_t *zone;
} ngx_http_nacos_conf_t;

typedef struct {
    ngx_http_upstream_srv_conf_t *uscf;
    ngx_http_upstream_init_pt original_init_upstream;
    ngx_http_upstream_init_peer_pt original_init_peer;
    ngx_http_nacos_conf_t *ncf;
} ngx_http_nacos_srv_conf_t;

#endif //NGINX_NACOS_NGX_HTTP_NACOS_H
