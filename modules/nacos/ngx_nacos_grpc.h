#include <ngx_event_connect.h>
#include <ngx_nacos.h>

typedef struct ngx_nacos_grpc_ctx_s ngx_nacos_grpc_ctx_t;

ngx_nacos_grpc_ctx_t *ngx_nacos_open_grpc_ctx(ngx_nacos_main_conf_t *ncf);
