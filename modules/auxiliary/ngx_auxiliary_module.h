//
// Created by dear on 22-5-28.
//

#ifndef NGINX_NACOS_NGX_AUXILIARY_MODULE_H
#define NGINX_NACOS_NGX_AUXILIARY_MODULE_H

#include <ngx_config.h>
#include <ngx_core.h>

typedef struct ngx_aux_proc_s ngx_aux_proc_t;

typedef ngx_int_t (*ngx_aux_proc_handler_t)(ngx_cycle_t *cycle, ngx_aux_proc_t *p);

struct ngx_aux_proc_s {
    ngx_str_t name;
    void *data;
    ngx_aux_proc_handler_t process;
};

typedef struct {
    ngx_array_t process; // ngx_aux_proc_t*
} ngx_aux_proc_main_conf_t;

ngx_int_t ngx_aux_add_proc(ngx_conf_t *cf, ngx_aux_proc_t *proc);

void ngx_aux_start_auxiliary_processes(ngx_cycle_t *cycle, ngx_uint_t respawn);

#endif //NGINX_NACOS_NGX_AUXILIARY_MODULE_H
