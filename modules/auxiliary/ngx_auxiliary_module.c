//
// Created by dear on 22-5-28.
//
#include <ngx_auxiliary_module.h>
#include <ngx_channel.h>

static void ngx_aux_process_cycle(ngx_cycle_t *cycle, void *data);

static void ngx_pass_open_channel(ngx_cycle_t *cycle);

static ngx_int_t ngx_aux_init_master(ngx_cycle_t *cycle);

static ngx_core_module_t auxiliary_module = {
        ngx_string("auxiliary"),
        NULL, // 解析配置文件之前执行
        NULL // 解析配置文件之后执行
};

ngx_module_t ngx_auxiliary_module = {
        NGX_MODULE_V1,
        &auxiliary_module,
        NULL,
        NGX_CORE_MODULE,
        NULL,                                 /* init master */
        NULL,                                  /* init module */
        ngx_aux_init_master,                   /* init process */
        NULL,                                  /* init thread */
        NULL,                                  /* exit thread */
        NULL,                                  /* exit process */
        NULL,                                  /* exit master */
        NGX_MODULE_V1_PADDING
};

#define ngx_aux_get_main_conf_ptr(cf)  (ngx_aux_proc_main_conf_t **) &(cf)->cycle->conf_ctx[ngx_auxiliary_module.index]


ngx_int_t ngx_aux_add_proc(ngx_conf_t *cf, ngx_aux_proc_t *proc) {
    ngx_aux_proc_main_conf_t **ptr, *mcf;
    ptr = ngx_aux_get_main_conf_ptr(cf);
    if (*ptr == NULL) {
        *ptr = ngx_palloc(cf->pool, sizeof(ngx_aux_proc_main_conf_t));
    }
    mcf = *ptr;
    if (mcf == NULL) {
        return NGX_ERROR;
    }
    if (ngx_array_init(&mcf->process, cf->pool, 4, sizeof(ngx_aux_proc_t *)) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_aux_proc_t **n_proc = ngx_array_push(&mcf->process);
    if (n_proc == NULL) {
        return NGX_ERROR;
    }
    *n_proc = proc;
    return NGX_OK;
}

void ngx_aux_start_auxiliary_processes(ngx_cycle_t *cycle, ngx_uint_t respawn) {
    ngx_aux_proc_main_conf_t *mcf;
    ngx_aux_proc_t **proc;
    ngx_uint_t i, n;
    char buf[256];

    mcf = (ngx_aux_proc_main_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_auxiliary_module);
    if (mcf == NULL) {
        return;
    }

    n = mcf->process.nelts;
    proc = mcf->process.elts;

    for (i = 0; i < n; ++i) {
        ngx_sprintf((u_char *) buf, "auxiliary: %V", &proc[i]->name);

        ngx_spawn_process(cycle, ngx_aux_process_cycle, proc[i], buf,
                          respawn ? NGX_PROCESS_JUST_RESPAWN : NGX_PROCESS_RESPAWN);
        ngx_pass_open_channel(cycle);
    }

}


static void ngx_pass_open_channel(ngx_cycle_t *cycle) {
    ngx_int_t i;
    ngx_channel_t ch;

    ch.command = NGX_CMD_OPEN_CHANNEL;
    ch.pid = ngx_processes[ngx_process_slot].pid;
    ch.slot = ngx_process_slot;
    ch.fd = ngx_processes[ngx_process_slot].channel[0];

    for (i = 0; i < ngx_last_process; i++) {

        if (i == ngx_process_slot
            || ngx_processes[i].pid == -1
            || ngx_processes[i].channel[0] == -1) {
            continue;
        }

        ngx_log_debug6(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "pass channel s:%i pid:%P fd:%d to s:%i pid:%P fd:%d",
                       ch.slot, ch.pid, ch.fd,
                       i, ngx_processes[i].pid,
                       ngx_processes[i].channel[0]);


        ngx_write_channel(ngx_processes[i].channel[0],
                          &ch, sizeof(ngx_channel_t), cycle->log);
    }
}

static void ngx_aux_process_cycle(ngx_cycle_t *cycle, void *data) {
    char buf[256];
    ngx_int_t ret;
    ngx_aux_proc_t *proc = data;
    ngx_process = NGX_PROCESS_HELPER;

    ngx_use_accept_mutex = 0;

    ngx_close_listening_sockets(cycle);

    cycle->connection_n = 512;
    ngx_worker_aux_process_init(cycle);

    ngx_sprintf((u_char *) buf, "auxiliary %V", &proc->name);
    ngx_setproctitle(buf);
    ret = proc->process(cycle, proc);
    if (ret != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, ret, "exiting auxiliary process: %s", buf);
        exit(1);
    }
    for (;;) {

        if (ngx_terminate || ngx_quit) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            exit(0);
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }

        ngx_process_events_and_timers(cycle);
    }

}

static ngx_int_t ngx_aux_init_master(ngx_cycle_t *cycle) {
    ngx_aux_proc_main_conf_t *mcf;
    ngx_aux_proc_t **proc;
    ngx_uint_t i, n;

    if (ngx_process != NGX_PROCESS_SINGLE) {
        return NGX_OK;
    }

    mcf = (ngx_aux_proc_main_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_auxiliary_module);
    if (mcf == NULL) {
        return NGX_ERROR;
    }
    proc = mcf->process.elts;
    n = mcf->process.nelts;

    for (i = 0; i < n; ++i) {
        if (proc[i]->process(cycle, proc[i]) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "auxiliary run in single mod");
    return NGX_OK;
}
