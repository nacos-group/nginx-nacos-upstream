//
// Created by eleme on 2023/4/20.
//

#include <ngx_nacos_data.h>
#include <ngx_nacos_http_parse.h>
#include <ngx_nacos_udp.h>

static void ngx_nacos_http_event_handler(ngx_event_t *ev);

static void ngx_nacos_udp_read_handler(ngx_event_t *ev);

static void ngx_nacos_udp_write_handler(ngx_event_t *ev);

static ngx_aux_nacos_conn_t *nax_nacos_create_http_connection(
    ngx_nacos_udp_conn_t *uc);

typedef ngx_int_t (*ngx_nacos_connect_handler_t)(ngx_aux_nacos_conn_t *nc,
                                                 ngx_event_t *ev);

static ngx_int_t ngx_nacos_write_sub_request(ngx_aux_nacos_conn_t *nc,
                                             ngx_event_t *ev);

static ngx_int_t ngx_nacos_read_sub_resp(ngx_aux_nacos_conn_t *nc,
                                         ngx_event_t *ev);

static void ngx_nacos_close_http_connection(ngx_aux_nacos_conn_t *nc,
                                            ngx_uint_t state);

static ngx_int_t ngx_nacos_create_req_buf(ngx_aux_nacos_conn_t *nc);

static ngx_int_t ngx_nacos_handle_sub_json(ngx_nacos_udp_conn_t *uc,
                                           yajl_val json, ngx_nacos_key_t *key,
                                           ngx_pool_t *pool, char *buf,
                                           size_t buf_len);

struct ngx_aux_nacos_conn_s {
    ngx_peer_connection_t peer;
    ngx_pool_t *pool;
    ngx_nacos_connect_handler_t read;
    ngx_nacos_connect_handler_t write;
    u_char *buf;
    off_t offset;
    size_t buf_len;
    enum {
        idle,
        connecting,
        prepare_req,
        writing_req,
        waiting_resp,
        reading_resp
    } stat;
    ngx_nacos_http_parse_t parser;
    ngx_nacos_udp_conn_t *uc;
};

#define NC_BUF_SIZE (64 * 1024)

#define NACOS_REQ_FMT                                         \
    "GET /nacos/v1/ns/instance/list?serviceName=%V%%40%%40%V" \
    "&udpPort=%V&clientIP=%V HTTP/1.1\r\n"                    \
    "Host: %V\r\n"                                            \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n"

ngx_nacos_udp_conn_t *ngx_nacos_open_udp(ngx_nacos_main_conf_t *ncf) {
    ngx_socket_t s;
    ngx_event_t *rev, *wev;
    ngx_connection_t *c;
    ngx_log_t *log;
    ngx_int_t event;
    ngx_pool_t *pool;
    ngx_nacos_udp_conn_t *uc;

    int reuse_addr = 1;
    pool = NULL;
    uc = NULL;
    log = ncf->error_log;
    s = ngx_socket(ncf->udp_addr.sockaddr->sa_family, SOCK_DGRAM, 0);
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0, "open udp socket:%d", s);

    if (s == (ngx_socket_t) -1) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                      ngx_socket_n " failed");
        return NULL;
    }

    c = ngx_get_connection(s, log);
    if (c == NULL) {
        if (ngx_close_socket(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                          ngx_close_socket_n " failed");
        }

        return NULL;
    }
    c->type = SOCK_DGRAM;

    if (ngx_nonblocking(s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                      ngx_nonblocking_n " failed");

        goto failed;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const void *) &reuse_addr,
                   sizeof(int)) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                      "setsockopt(SO_REUSEADDR) failed");
        goto failed;
    }
    if (bind(s, ncf->udp_addr.sockaddr, ncf->udp_addr.socklen) == -1) {
        ngx_log_error(NGX_LOG_CRIT, log, ngx_socket_errno, "bind(%V) failed",
                      &ncf->udp_addr.name);

        goto failed;
    }

    c->recv = ngx_udp_recv;
    c->send = ngx_udp_send;
    c->send_chain = ngx_udp_send_chain;

    c->log_error = NGX_ERROR_INFO;
    c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);
    // c->start_time = ngx_current_msec;
    c->log = ncf->error_log;

    rev = c->read;
    wev = c->write;

    rev->log = log;
    wev->log = log;

    if (ngx_add_conn) {
        if (ngx_add_conn(c) == NGX_ERROR) {
            goto failed;
        }
    }
    if (ngx_event_flags & NGX_USE_IOCP_EVENT) {
        if (ngx_blocking(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                          ngx_blocking_n " failed");
            goto failed;
        }
        rev->ready = 1;
        wev->ready = 1;
        return NGX_OK;
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {
        event = NGX_CLEAR_EVENT;
    } else {
        event = NGX_LEVEL_EVENT;
    }
    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK) {
        goto failed;
    }

    pool = ngx_create_pool(ncf->udp_pool_size, ncf->error_log);
    if (pool == NULL) {
        goto failed;
    }

    uc = ngx_pcalloc(pool, sizeof(*uc));
    if (uc == NULL) {
        goto failed;
    }
    uc->ncf = ncf;

    switch (ncf->udp_addr.sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
        case AF_INET6:
            uc->udp_remote_addr_text_len = NGX_INET6_ADDRSTRLEN;
            break;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
        case AF_UNIX:
            uc->udp_remote_addr_text_len = NGX_UNIX_ADDRSTRLEN;
            break;
#endif
        case AF_INET:
            uc->udp_remote_addr_text_len = NGX_INET_ADDRSTRLEN;
            break;
        default:
            uc->udp_remote_addr_text_len = NGX_SOCKADDR_STRLEN;
            break;
    }
    uc->udp_remote_addr_text = ngx_palloc(pool, uc->udp_remote_addr_text_len);
    if (uc->udp_remote_addr_text == NULL) {
        goto failed;
    }

    c->data = uc;
    c->pool = pool;
    c->read->handler = ngx_nacos_udp_read_handler;
    c->write->handler = ngx_nacos_udp_write_handler;

    ngx_add_timer(c->write, 100);
    return uc;

failed:
    ngx_close_connection(c);
    if (pool != NULL) {
        ngx_destroy_pool(pool);
    }
    return NULL;
}

static void ngx_nacos_udp_write_handler(ngx_event_t *ev) {
    ngx_aux_nacos_conn_t *nc;
    ngx_nacos_udp_conn_t *uc;
    ngx_connection_t *c;

    if (!ev->timedout) {  // only timeout
        return;
    }
    ev->timedout = 0;

    c = ev->data;
    uc = c->data;
    nc = uc->nc;
    if (nc != NULL) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "event write but nacos http connection exists");
        uc->nc = NULL;
        ngx_nacos_close_http_connection(nc, NGX_NC_TIRED);
    }

    nc = nax_nacos_create_http_connection(uc);

    if (nc == NULL) {
        ngx_add_timer(ev, 10000);  // retry 10s
        return;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                   "open http connection success");
    uc->nc = nc;
}

static void ngx_nacos_close_http_connection(ngx_aux_nacos_conn_t *nc,
                                            ngx_uint_t state) {
    ngx_log_debug0(NGX_LOG_DEBUG_CORE, nc->peer.log, 0,
                   "close http connection");
    ngx_close_connection(nc->peer.connection);
    nc->peer.free(&nc->peer, nc->peer.data, state);
    ngx_destroy_pool(nc->pool);
}

#define NACOS_DOM_RESP_FMT \
    "{\"type\": \"push-ack\", \"lastRefTime\":%s,\"data\":\"\"}"
#define NACOS_UNKNOWN_RESP_FMT \
    "{\"type\": \"unknown-ack\", \"lastRefTime\":\"%s\",\"data\":\"\"}"

static void ngx_nacos_udp_read_handler(ngx_event_t *ev) {
    ngx_connection_t *c;
    ngx_nacos_udp_conn_t *uc;
    ssize_t n;
    static u_char buffer[65535];
    ngx_err_t err;
    ngx_sockaddr_t sa;
    socklen_t socklen;
    ngx_str_t addr_text;
    yajl_val json, type, ref, d, data, name;
    size_t resp_len;
    ngx_flag_t dom_update;
    ngx_nacos_key_t *key;
    ngx_pool_t *temp;
    char *type_str;

    if (ev->timedout) {
        ev->timedout = 0;
        return;
    }
    c = ev->data;
    uc = c->data;
    temp = NULL;
    dom_update = 0;
    data = NULL;
    socklen = sizeof(sa);

    n = recvfrom(c->fd, buffer, sizeof(buffer), 0, (struct sockaddr *) &sa,
                 &socklen);

    if (n == -1) {
        err = ngx_socket_errno;
        if (err == NGX_EAGAIN) {
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, err,
                           "recvmsg() not ready");
            return;
        }
        ngx_log_error(NGX_LOG_ALERT, ev->log, err, "nacos recvmsg() failed");
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "nacos recvmsg() failed");
        return;
    }

    if (socklen > (socklen_t) sizeof(ngx_sockaddr_t)) {
        socklen = sizeof(ngx_sockaddr_t);
    }

    if (socklen == 0) {
        /*
         * on Linux recvmsg() returns zero msg_namelen
         * when receiving packets from unbound AF_UNIX sockets
         */

        socklen = sizeof(struct sockaddr);
        ngx_memzero(&sa, sizeof(struct sockaddr));
        sa.sockaddr.sa_family = uc->ncf->udp_addr.sockaddr->sa_family;
    }
    addr_text.data = uc->udp_remote_addr_text;
    addr_text.len = uc->udp_remote_addr_text_len;
    addr_text.len =
        ngx_sock_ntop(&sa.sockaddr, socklen, addr_text.data, addr_text.len, 1);
    json = yajl_tree_parse_with_len((const char *) buffer, n);
    if (json == NULL) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "receive udp msg from %V not valid json", &addr_text);
        return;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, c->log, 0, "udp received a json from %V",
                   &addr_text);
    if (!YAJL_IS_OBJECT(json)) {
        yajl_tree_free(json);
        return;
    }

    type = yajl_tree_get_field(json, "type", yajl_t_string);
    ref = yajl_tree_get_field(json, "lastRefTime", yajl_t_number);
    d = yajl_tree_get_field(json, "data", yajl_t_string);
    if (type == NULL || ref == NULL || d == NULL) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "receive udp msg from %V absent type/lastRefTime/data",
                      &c->addr_text);
        goto end;
    }

    type_str = YAJL_GET_STRING(type);
    if (ngx_strncmp(type_str, "dom", 3) == 0) {
        dom_update = 1;
        data = yajl_tree_parse(YAJL_GET_STRING(d), NULL, 0);
        if (data == NULL) {
            ngx_log_error(NGX_LOG_WARN, c->log, 0,
                          "receive udp msg from %V data is not json string",
                          &addr_text);
            goto end;
        }

        name = yajl_tree_get_field(data, "name", yajl_t_string);
        if (name == NULL) {
            ngx_log_error(NGX_LOG_WARN, c->log, 0,
                          "receive udp msg from %V data name is absent",
                          &c->addr_text);
            goto end;
        }
        key = ngx_nacos_hash_find_key(uc->ncf->key_hash,
                                      (u_char *) YAJL_GET_STRING(name));
        if (key != NULL) {
            temp = ngx_create_pool(512, c->log);
            if (temp == NULL) {
                goto end;
            }
            ngx_log_error(NGX_LOG_INFO, c->log, 0,
                          "nacos udp dom %s is received from:%V",
                          YAJL_GET_STRING(name), &addr_text);
            ngx_nacos_handle_sub_json(uc, data, key, temp, (char *) buffer,
                                      sizeof(buffer));
        } else {
            ngx_log_error(NGX_LOG_WARN, c->log, 0,
                          "nacos udp dom  %s is unknown from:%V",
                          YAJL_GET_STRING(name), &addr_text);
        }
    }

end:
    if (dom_update) {
        resp_len = ngx_snprintf(buffer, sizeof(buffer), NACOS_DOM_RESP_FMT,
                                YAJL_GET_NUMBER(ref)) -
                   buffer;
    } else {
        resp_len = ngx_snprintf(buffer, sizeof(buffer), NACOS_UNKNOWN_RESP_FMT,
                                YAJL_GET_NUMBER(ref)) -
                   buffer;
    }

    n = sendto(c->fd, buffer, resp_len, 0, &sa.sockaddr, socklen);
    if (n != (ssize_t) resp_len) {
        ngx_log_error(NGX_LOG_WARN, c->log, ngx_socket_errno,
                      "udp resp not send successfully???");
    }

    if (temp != NULL) {
        ngx_destroy_pool(temp);
    }
    if (data != NULL) {
        yajl_tree_free(data);
    }
    yajl_tree_free(json);
}

static void ngx_nacos_http_event_handler(ngx_event_t *ev) {
    ngx_connection_t *c;
    ngx_aux_nacos_conn_t *nc;
    ngx_nacos_udp_conn_t *uc;
    ngx_int_t rc;

    c = ev->data;
    nc = c->data;
    uc = nc->uc;

    if (c->close) {
        ngx_destroy_pool(nc->pool);
        ngx_close_connection(c);
        return;
    }

    if (ev->write) {
        rc = nc->write(nc, ev);
    } else {
        rc = nc->read(nc, ev);
    }

    if (rc == NGX_OK) {
        return;
    } else if (rc == NGX_AGAIN) {
        if (ev->write) {
            if (ngx_handle_write_event(ev, 0) == NGX_OK) {
                return;
            }
        } else {
            return;
        }
    }
    ngx_nacos_close_http_connection(
        nc, rc == NGX_DONE ? NGX_NC_TIRED : NGX_NC_ERROR);
    uc->nc = NULL;
    ngx_add_timer(uc->uc->write, 100);
}

static ngx_aux_nacos_conn_t *nax_nacos_create_http_connection(
    ngx_nacos_udp_conn_t *uc) {
    ngx_pool_t *pool;
    ngx_aux_nacos_conn_t *nc;
    ngx_connection_t *c;
    ngx_int_t rc;
    ngx_uint_t try;

    try = 0;

    pool = ngx_create_pool(uc->ncf->udp_pool_size, uc->ncf->error_log);
    if (pool == NULL) {
        return NULL;
    }

    nc = ngx_pcalloc(pool, sizeof(*nc));
    if (nc == NULL) {
        return NULL;
    }
    nc->uc = uc;

    nc->buf = ngx_palloc(pool, NC_BUF_SIZE);
    if (nc->buf == NULL) {
        goto connect_failed;
    }

    nc->peer.start_time = ngx_current_msec;
    nc->peer.log_error = NGX_ERROR_INFO;
    nc->peer.log = uc->ncf->error_log;
    nc->peer.get = ngx_nacos_aux_get_addr;
    nc->peer.free = ngx_nacos_aux_free_addr;
    nc->peer.data = &uc->ncf->server_list;
    nc->write = ngx_nacos_write_sub_request;
    nc->read = ngx_nacos_read_sub_resp;

connect:
    rc = ngx_event_connect_peer(&nc->peer);
    try++;
    if (rc == NGX_ERROR) {
        if (nc->peer.name) {
            ngx_log_error(NGX_LOG_WARN, nc->peer.log, 0,
                          "http connection connect to %V error", nc->peer.name);
        }
        if (nc->peer.sockaddr) {
            nc->peer.free(&nc->peer, nc->peer.data, NGX_ERROR);
        }
        if (try < uc->ncf->server_list.nelts) {
            goto connect;
        }
        goto connect_failed;
    }

    c = nc->peer.connection;
    c->data = nc;
    c->pool = pool;
    c->write->handler = ngx_nacos_http_event_handler;
    c->read->handler = ngx_nacos_http_event_handler;
    c->requests = 0;
    nc->pool = pool;

    if (rc == NGX_AGAIN) {  // connecting
        nc->stat = connecting;
        c->log->action = "nacos http connection connecting";
        ngx_add_timer(c->write, 3000);  // set connect time out
        return nc;
    }

    nc->stat = prepare_req;
    // rc == NGX_OK
    rc = nc->write(nc, c->write);
    if (rc == NGX_OK || rc == NGX_AGAIN) {
        return nc;
    }
    ngx_destroy_pool(pool);
    return NULL;

connect_failed:
    ngx_log_error(NGX_LOG_WARN, nc->peer.log, 0,
                  "create http connection error after try %d", try);
    ngx_destroy_pool(pool);
    return NULL;
}

static ngx_int_t ngx_nacos_write_sub_request(ngx_aux_nacos_conn_t *nc,
                                             ngx_event_t *ev) {
    ssize_t rd;
    int err;
    socklen_t len;
    ngx_connection_t *c = nc->peer.connection;

    if (ev->timedout) {
        ev->timedout = 0;
        if (nc->stat == writing_req || nc->stat == connecting) {
            return NGX_BUSY;
        }
        if (nc->stat == idle) {
            nc->stat = prepare_req;
        }
    }

    if (nc->stat == connecting) {
        if (ev->timer_set) {
            ngx_del_timer(ev);
        }
        err = 0;
        len = sizeof(err);
        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len) ==
            -1) {
            err = ngx_socket_errno;
        }

        if (err) {
            c->log->action = "connecting to upstream";
            (void) ngx_connection_error(c, err, "connect() failed");
            return NGX_ERROR;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                       "http connection connect successfully");
        nc->stat = prepare_req;
    }

    if (nc->stat == prepare_req) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                       "nacos http connection prepare request");
        ngx_nacos_create_req_buf(nc);
        nc->stat = writing_req;
    }

    if (nc->stat == writing_req) {
        do {
            rd = c->send(c, nc->buf + nc->offset, nc->buf_len - nc->offset);
            if (rd == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (rd < 0) {
                // NGX_AGAIN
                ngx_add_timer(ev, 1000);  // 1s
                return NGX_AGAIN;
            }
            nc->offset += rd;
        } while ((size_t) nc->offset < nc->buf_len);

        nc->stat = waiting_resp;
        nc->buf_len = 0;
        nc->offset = 0;
        ngx_add_timer(c->read, 5000);
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                       "http connection write request successfully");
    }

    // ignore
    return NGX_OK;
}

static ngx_int_t ngx_nacos_handle_sub_json(ngx_nacos_udp_conn_t *uc,
                                           yajl_val json, ngx_nacos_key_t *key,
                                           ngx_pool_t *pool, char *buf,
                                           size_t buf_len) {
    ngx_nacos_resp_json_parser_t resp_parser;
    ngx_nacos_data_t cache;
    ngx_int_t rc;
    char *adr;

    resp_parser.log = pool->log;
    resp_parser.pool = pool;
    resp_parser.out_buf = buf;
    resp_parser.out_buf_len = buf_len;
    resp_parser.json = json;
    resp_parser.prev_version = ngx_nacos_shmem_version(key);
    adr = ngx_nacos_parse_addrs_from_json(&resp_parser);
    if (adr != NULL) {
        rc = ngx_nacos_update_shm(uc->ncf, key, adr, pool->log);
        if (rc == NGX_OK) {
            ngx_log_error(NGX_LOG_INFO, pool->log, 0,
                          "nacos service %V@@%V is updated!!!", &key->group,
                          &key->data_id);
        }
        cache.pool = pool;
        cache.data_id = key->data_id;
        cache.group = key->group;
        cache.version = resp_parser.current_version;
        cache.adr = adr;
        return ngx_nacos_write_disk_data(uc->ncf, &cache);
    }
    return NGX_DECLINED;
}

static ngx_int_t ngx_nacos_read_sub_resp(ngx_aux_nacos_conn_t *nc,
                                         ngx_event_t *ev) {
    ssize_t rd;
    ngx_int_t rc;
    ngx_uint_t next_req_time;
    char buf[1];
    yajl_val json;
    ngx_nacos_udp_conn_t *uc;
    ngx_connection_t *c = nc->peer.connection;
    uc = nc->uc;
    if (ev->timedout) {
        if (nc->stat == waiting_resp || nc->stat == reading_resp) {
            return NGX_BUSY;
        }
    }

    if (ev->timer_set) {
        ngx_del_timer(ev);
    }

    if (nc->stat == waiting_resp) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                       "http connection start reading response");
        nc->stat = reading_resp;
        ngx_memzero(&nc->parser, sizeof(nc->parser));
        nc->parser.log = c->log;
        nc->parser.buf = nc->buf;
    }

    if (nc->stat == reading_resp) {
        for (;;) {
            c->log->action = "nacos reading response";
            rd = c->recv(c, nc->parser.buf + nc->parser.limit,
                         NC_BUF_SIZE - nc->parser.limit);
            if (rd >= 0) {
                nc->parser.limit += rd;
                nc->parser.conn_eof = ev->eof;
                c->log->action = "nacos parsing response";
                rc = ngx_nacos_http_parse(&nc->parser);
                if (rc == NGX_ERROR) {
                    ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                                   "http connection protocol error");
                    return NGX_ERROR;
                } else if (rc == NGX_OK) {
                    goto request_complete;
                }

                if (ev->eof) {
                    // close premature
                    ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                                   "http connection close abruptly");
                    return NGX_ERROR;
                }
            } else if (rd == NGX_AGAIN) {
                return NGX_OK;
            } else {
                ngx_log_debug0(NGX_LOG_DEBUG_CORE, c->log, 0,
                               "http connection encounter socket error");
                // socket error
                return NGX_ERROR;
            }
        }
    } else if (nc->stat == connecting) {
        return NGX_OK;
    } else {
        rc = recv(c->fd, buf, 1, MSG_PEEK);
        if (rc == -1 && ngx_socket_errno == NGX_EAGAIN) {
            ev->ready = 0;
            return NGX_OK;
        }
        if (rc == 0) {
            return NGX_DONE;
        }
        return NGX_ERROR;
    }

request_complete:
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, c->log, 0,
                   "http connection read response successfully,status=%d",
                   nc->parser.status);
    c->requests++;

    if (nc->parser.json_parser) {
        json = yajl_tree_finish_get(nc->parser.json_parser);
        if (json == NULL) {
            goto free;
        }
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "http connection received json response");
    }

free:
    if (nc->parser.json_parser) {
        yajl_tree_free_parser(nc->parser.json_parser);
        nc->parser.json_parser = NULL;
    }

    next_req_time = 10;
    if (nc->parser.status == 200) {
        uc->key_idx++;
        // subscribe ok. because of push msg by udp;
        if (uc->heart_time == 0) {
            if (++uc->sub_keys >= uc->ncf->keys.nelts) {
                uc->heart_time = ngx_current_msec;
            }
        } else if (ngx_current_msec - uc->heart_time >= 120000) {  // 120s
            uc->sub_keys = 0;
            uc->heart_time = 0;
        }
        if (uc->heart_time) {
            next_req_time = 15000;  // 15s
        }
    }

    if (nc->parser.close_conn == 1) {
        return NGX_DONE;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, c->log, 0,
                   "send next nacos sub request after %ul", next_req_time);
    ngx_add_timer(c->write, next_req_time);  // now write next request
    nc->stat = idle;
    return NGX_OK;
}

static ngx_int_t ngx_nacos_create_req_buf(ngx_aux_nacos_conn_t *nc) {
    ngx_uint_t idx, len;
    ngx_nacos_key_t *key;
    ngx_nacos_udp_conn_t *uc;
    ngx_str_t req;
    u_char *p;

    uc = nc->uc;

    idx = uc->key_idx;
    key = uc->ncf->keys.elts;
    len = uc->ncf->keys.nelts;
    if (idx >= len) {
        uc->key_idx = 0;
        idx = 0;
    }

    nc->buf_len =
        ngx_sprintf(nc->buf, NACOS_REQ_FMT, &key[idx].group, &key[idx].data_id,
                    &uc->ncf->udp_port, &uc->ncf->udp_ip, nc->peer.name) -
        nc->buf;

    req.data = nc->buf;
    req.len = nc->buf_len;

    if (ngx_nacos_http_append_user_pass_header(uc->ncf, &req,
                                               NC_BUF_SIZE - 2) != NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, uc->uc->log, 0,
                      "http connection received json response");
        return NGX_ERROR;
    }
    p = req.data + req.len;
    *p = '\r';
    *(p + 1) = '\n';
    req.len += 2;
    nc->buf_len = req.len;
    return NGX_OK;
}
