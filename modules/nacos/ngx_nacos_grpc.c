//
// Created by eleme on 2023/2/17.
//

#include <nacos_grpc_service.pb-c.h>
#include <ngx_http_v2.h>
#include <ngx_nacos_data.h>
#include <ngx_nacos_grpc.h>
#include <yaij/api/yajl_gen.h>
#include <yaij/api/yajl_tree.h>

typedef struct ngx_nacos_grpc_stream_s ngx_nacos_grpc_stream_t;
typedef struct ngx_nacos_grpc_conn_s ngx_nacos_grpc_conn_t;
typedef struct ngx_nacos_grpc_buf_s ngx_nacos_grpc_buf_t;

typedef ngx_int_t (*grpc_buf_callback)(ngx_nacos_grpc_stream_t *s,
                                       ngx_int_t state);

struct ngx_nacos_grpc_buf_s {
    ngx_nacos_grpc_stream_t *stream;
    grpc_buf_callback callback;
    ngx_nacos_grpc_buf_t *next;
    u_char *b;
    size_t cap;
    size_t len;
    size_t consume_win;
};

typedef struct {
    ngx_nacos_grpc_buf_t *head;
    ngx_nacos_grpc_buf_t *tail;
} ngx_nacos_grpc_bufs_t;

struct ngx_nacos_grpc_conn_s {
    ngx_connection_t *conn;
    ngx_pool_t *pool;
    uint32_t next_stream_id;
    enum {
        init,
        connecting,
        prepare_conn,
        waiting_conn_prepared,
        prepare_subscribe,
        subscribing_config,
        subscribing_service,
        subscribed
    } stat;
    ngx_peer_connection_t peer;
    ngx_nacos_grpc_stream_t *m_stream;
    ngx_rbtree_t st_tree;
    ngx_rbtree_node_t st_sentinel;
    ngx_queue_t all_streams;
    ngx_queue_t io_blocking_list;
    ngx_queue_t conn_win_blocking_list;
    struct {
        ngx_uint_t header_table_size;
        ngx_uint_t max_conn_streams;
        ngx_uint_t init_window_size;
        ngx_uint_t max_frame_size;
        ngx_uint_t max_header_list_size;
    } settings;
    ngx_buf_t *read_buf;
    ngx_uint_t heartbeat;
    enum { parse_frame_header = 0, parse_frame_payload } parse_stat;
    size_t frame_size;
    ngx_uint_t frame_stream_id;
    u_char frame_type;
    u_char frame_flags;
    unsigned frame_start : 1;
    unsigned frame_end : 1;
};

enum ngx_nacos_payload_type {
    NONE__START = 0, /* unknown */
    ClientDetectionRequest,
    ConnectionSetupRequest,
    ConnectResetRequest,
    HealthCheckRequest,
    PushAckRequest,
    ServerCheckRequest,
    ServerLoaderInfoRequest,
    ServerReloadRequest,
    BatchInstanceRequest,
    NotifySubscriberRequest,
    ServiceListRequest,
    ServiceQueryRequest,
    SubscribeServiceRequest,
    ConfigBatchListenRequest,
    ConfigChangeNotifyRequest,
    ConfigQueryRequest,
    /* response */
    ClientDetectionResponse,
    ConnectResetResponse,
    ErrorResponse,
    HealthCheckResponse,
    ServerCheckResponse,
    ServerLoaderInfoResponse,
    ServerReloadResponse,
    BatchInstanceResponse,
    NotifySubscriberResponse,
    QueryServiceResponse,
    ServiceListResponse,
    SubscribeServiceResponse,
    ConfigChangeBatchListenResponse,
    ConfigChangeNotifyResponse,
    ConfigQueryResponse,
    NONE__END
};

static struct {
    ngx_str_t type_name;
    enum ngx_nacos_payload_type payload_type;
} payload_type_mapping[] = {
    {ngx_string("NONE__START"), NONE__START},
    {ngx_string("ClientDetectionRequest"), ClientDetectionRequest},
    {ngx_string("ConnectionSetupRequest"), ConnectionSetupRequest},
    {ngx_string("ConnectResetRequest"), ConnectResetRequest},
    {ngx_string("HealthCheckRequest"), HealthCheckRequest},
    {ngx_string("PushAckRequest"), PushAckRequest},
    {ngx_string("ServerCheckRequest"), ServerCheckRequest},
    {ngx_string("ServerLoaderInfoRequest"), ServerLoaderInfoRequest},
    {ngx_string("ServerReloadRequest"), ServerReloadRequest},
    {ngx_string("BatchInstanceRequest"), BatchInstanceRequest},
    {ngx_string("NotifySubscriberRequest"), NotifySubscriberRequest},
    {ngx_string("ServiceListRequest"), ServiceListRequest},
    {ngx_string("ServiceQueryRequest"), ServiceQueryRequest},
    {ngx_string("SubscribeServiceRequest"), SubscribeServiceRequest},
    {ngx_string("ConfigBatchListenRequest"), ConfigBatchListenRequest},
    {ngx_string("ConfigChangeNotifyRequest"), ConfigChangeNotifyRequest},
    {ngx_string("ConfigQueryRequest"), ConfigQueryRequest},
    /* response */
    {ngx_string("ClientDetectionResponse"), ClientDetectionResponse},
    {ngx_string("ConnectResetResponse"), ConnectResetResponse},
    {ngx_string("ErrorResponse"), ErrorResponse},
    {ngx_string("HealthCheckResponse"), HealthCheckResponse},
    {ngx_string("ServerCheckResponse"), ServerCheckResponse},
    {ngx_string("ServerLoaderInfoResponse"), ServerLoaderInfoResponse},
    {ngx_string("ServerReloadResponse"), ServerReloadResponse},
    {ngx_string("BatchInstanceResponse"), BatchInstanceResponse},
    {ngx_string("NotifySubscriberResponse"), NotifySubscriberResponse},
    {ngx_string("QueryServiceResponse"), QueryServiceResponse},
    {ngx_string("ServiceListResponse"), ServiceListResponse},
    {ngx_string("SubscribeServiceResponse"), SubscribeServiceResponse},
    {ngx_string("ConfigChangeBatchListenResponse"),
     ConfigChangeBatchListenResponse},
    {ngx_string("ConfigChangeNotifyResponse"), ConfigChangeNotifyResponse},
    {ngx_string("ConfigQueryResponse"), ConfigQueryResponse},
    {ngx_string("NONE__END"), NONE__END}};

typedef ngx_int_t (*stream_handler)(ngx_nacos_grpc_stream_t *st,
                                    enum ngx_nacos_payload_type type,
                                    yajl_val json);

struct ngx_nacos_grpc_stream_s {
    ngx_rbtree_node_t node;
    ngx_queue_t queue;
    ngx_pool_t *pool;
    ngx_nacos_grpc_conn_t *conn;
    size_t send_win;
    size_t recv_win;
    ngx_uint_t stream_id;
    stream_handler stream_handler;
    void *handler_ctx;
    ngx_nacos_grpc_bufs_t non_block_bufs;
    ngx_nacos_grpc_bufs_t block_bufs;
    ngx_queue_t io_blocking;
    ngx_queue_t conn_win_blocking;
    ngx_buf_t *tmp_buf;
    ngx_uint_t resp_status;
    ngx_uint_t grpc_status;
    size_t proto_len;
    u_char padding;
    u_char parsing_state;
    unsigned header_sent : 1;
    unsigned end_stream : 1;
    unsigned end_header : 1;
    unsigned resp_grpc_encode : 1;
    unsigned long_live : 1;
    unsigned send_buf_block : 1;
    unsigned send_buf_block_conn : 1;
};

#define NGX_NACOS_GRPC_DEFAULT_GRPC_STATUS 10000
#define NGX_NACOS_GRPC_DEFAULT_PING_INTERVAL 360000
#define NGX_NACOS_GRPC_CONFIG_BATCH_SIZE 100

struct ngx_nacos_grpc_ctx_s {
    ngx_nacos_grpc_conn_t *gc;
    ngx_nacos_main_conf_t *ncf;
    ngx_event_t ev;
    ngx_uint_t key_idx;
    ngx_uint_t conf_key_idx;
};

static ngx_nacos_grpc_ctx_t grpc_ctx;

static ngx_nacos_grpc_conn_t *ngx_nacos_open_grpc_conn(
    ngx_nacos_main_conf_t *conf);

static ngx_nacos_grpc_stream_t *ngx_nacos_grpc_create_stream(
    ngx_nacos_grpc_conn_t *gc);

static void ngx_nacos_grpc_close_stream(ngx_nacos_grpc_stream_t *st);

static ngx_nacos_grpc_buf_t *ngx_nacos_grpc_alloc_buf(
    ngx_nacos_grpc_stream_t *gc, size_t cap);

#define ngx_nacos_grpc_free_buf(st, buf) ngx_free(buf)

static ngx_int_t ngx_nacos_grpc_send_buf(ngx_nacos_grpc_buf_t *buf,
                                         ngx_flag_t can_block);

static ngx_int_t ngx_nacos_grpc_do_send(ngx_nacos_grpc_stream_t *st);

#define READ_BUF_CAP 65536

static u_char ngx_nacos_grpc_connection_start[] =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" /* connection preface */

    "\x00\x00\x12\x04\x00\x00\x00\x00\x00" /* settings frame */
    "\x00\x01\x00\x00\x00\x00"             /* header table size */
    "\x00\x02\x00\x00\x00\x00"             /* disable push */
    "\x00\x04\x7f\xff\xff\xff"             /* initial window */

    "\x00\x00\x04\x08\x00\x00\x00\x00\x00" /* window update frame */
    "\x7f\xff\x00\x00";

static void ngx_nacos_grpc_event_handler(ngx_event_t *ev);

static void ngx_nacos_grpc_ctl_handler(ngx_event_t *ev);

static ngx_int_t ngx_nacos_grpc_write_handler(ngx_nacos_grpc_conn_t *gc,
                                              ngx_event_t *ev);

static ngx_int_t ngx_nacos_grpc_read_handler(ngx_nacos_grpc_conn_t *gc,
                                             ngx_event_t *ev);

static ngx_int_t ngx_nacos_grpc_send_conn_start(ngx_nacos_grpc_stream_t *st);

static void ngx_nacos_grpc_close_connection(ngx_nacos_grpc_conn_t *gc,
                                            ngx_int_t status);

static ngx_int_t ngx_nacos_grpc_parse_frame(ngx_nacos_grpc_conn_t *gc);

typedef ngx_int_t (*ngx_nacos_grpc_frame_handler)(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_unknown_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_data_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_header_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_rst_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_settings_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_ping_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_goaway_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_parse_window_update_frame(
    ngx_nacos_grpc_conn_t *gc);

static ngx_nacos_grpc_stream_t *ngx_nacos_grpc_find_stream(
    ngx_nacos_grpc_conn_t *gc, ngx_uint_t st_id);

static ngx_int_t ngx_nacos_grpc_update_send_window(ngx_nacos_grpc_conn_t *gc,
                                                   ngx_uint_t st_id,
                                                   ngx_uint_t win_update);

static ngx_int_t ngx_nacos_grpc_send_blocking_buf(ngx_nacos_grpc_conn_t *gc,
                                                  ngx_nacos_grpc_stream_t *st);

static ngx_int_t ngx_nacos_grpc_parse_proto_msg(ngx_nacos_grpc_stream_t *st,
                                                ngx_str_t *proto_msg);

static ngx_nacos_grpc_buf_t *ngx_nacos_grpc_encode_request(
    ngx_nacos_grpc_stream_t *st, ngx_str_t *mtd);

static ngx_int_t ngx_nacos_grpc_send_subscribe_request(
    ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_decode_ule128(u_char **pp, const u_char *last,
                                              size_t *result);

static ngx_int_t ngx_nacos_mark_conn_subscribe(ngx_nacos_grpc_stream_t *st,
                                               ngx_int_t state);

static ngx_int_t ngx_nacos_grpc_mark_query_config(ngx_nacos_grpc_stream_t *st,
                                                  ngx_int_t state);

static ngx_int_t ngx_nacos_grpc_do_subscribe_service_items(
    ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_do_subscribe_config_items(
    ngx_nacos_grpc_conn_t *gc);

static ngx_int_t ngx_nacos_grpc_mark_subscribe_items(
    ngx_nacos_grpc_stream_t *st, ngx_int_t state);

static ngx_inline void ngx_nacos_grpc_encode_frame_header(
    ngx_nacos_grpc_stream_t *st, u_char *b, u_char type, u_char flags,
    size_t len) {
    b[0] = (len >> 16) & 0xFF;
    b[1] = (len >> 8) & 0xFF;
    b[2] = len & 0xFF;
    b[3] = type;
    b[4] = flags;
    b[5] = (st->stream_id >> 24) & 0x7F;
    b[6] = (st->stream_id >> 16) & 0xFF;
    b[7] = (st->stream_id >> 8) & 0xFF;
    b[8] = st->stream_id & 0xFF;
}

typedef struct {
    const char *type;
    ngx_str_t json;

    Metadata metadata;
    Payload payload;
    Google__Protobuf__Any any;
    size_t encoded_len;
    u_char *buf;
} ngx_nacos_grpc_payload_encode_t;

static void ngx_nacos_grpc_encode_payload_init(
    ngx_nacos_grpc_payload_encode_t *en);

static ngx_int_t ngx_nacos_grpc_encode_payload(
    ngx_nacos_grpc_payload_encode_t *en);

static ngx_nacos_grpc_buf_t *ngx_nacos_grpc_encode_data_msg(
    ngx_nacos_grpc_stream_t *st, ngx_nacos_grpc_payload_encode_t *en,
    ngx_flag_t end_stream);

typedef struct {
    ngx_str_t input;
    Payload *result;
    char *type;
    ngx_str_t out_json;
    yajl_val json;
    enum ngx_nacos_payload_type payload_type;
} ngx_nacos_grpc_payload_decode_t;

static ngx_int_t ngx_nacos_grpc_decode_payload(
    ngx_nacos_grpc_payload_decode_t *de);

static void ngx_nacos_grpc_decode_payload_destroy(
    ngx_nacos_grpc_payload_decode_t *de);

static ngx_flag_t ngx_nacos_grpc_payload_is_response_ok(
    enum ngx_nacos_payload_type payload_type, yajl_val root);

static ngx_int_t ngx_nacos_grpc_send_server_push_resp(
    ngx_nacos_grpc_stream_t *st, yajl_val json, const char *resp_type);

static ngx_int_t ngx_nacos_grpc_notify_address_shm(ngx_nacos_grpc_conn_t *gc,
                                                   yajl_val json);

static ngx_int_t ngx_nacos_grpc_notify_config_shm(ngx_nacos_grpc_stream_t *st,
                                                  yajl_val json);

static ngx_int_t ngx_nacos_grpc_config_change_notified(
    ngx_nacos_grpc_conn_t *gc, yajl_val root);

static ngx_int_t ngx_nacos_grpc_send_config_query_request(
    ngx_nacos_grpc_conn_t *gc, ngx_nacos_key_t *key);

static ngx_int_t ngx_nacos_grpc_subscribe_response_handler(
    ngx_nacos_grpc_stream_t *st, enum ngx_nacos_payload_type payload_type,
    yajl_val json);

static ngx_int_t ngx_nacos_grpc_service_sub_event_resp_handler(
    ngx_nacos_grpc_stream_t *st, enum ngx_nacos_payload_type type,
    yajl_val json);

static ngx_int_t ngx_nacos_grpc_config_change_deal(ngx_nacos_grpc_stream_t *st,
                                                   yajl_val root);
static ngx_int_t ngx_nacos_grpc_config_query_resp_handler(
    ngx_nacos_grpc_stream_t *st, enum ngx_nacos_payload_type type,
    yajl_val json);

static ngx_int_t ngx_nacos_grpc_realloc_tmp_buf(ngx_nacos_grpc_stream_t *st,
                                                size_t asize);

static ngx_int_t ngx_nacos_grpc_send_win_update_frame(
    ngx_nacos_grpc_stream_t *st, size_t win_update);

static ngx_int_t ngx_nacos_send_ping_frame(ngx_nacos_grpc_conn_t *gc);

static ngx_str_t http2_err[] = {
    ngx_string("NO_ERROR(0L)"),
    ngx_string("PROTOCOL_ERROR(1L)"),
    ngx_string("INTERNAL_ERROR(2L)"),
    ngx_string("FLOW_CONTROL_ERROR(3L)"),
    ngx_string("SETTINGS_TIMEOUT(4L)"),
    ngx_string("STREAM_CLOSED(5L)"),
    ngx_string("FRAME_SIZE_ERROR(6L)"),
    ngx_string("REFUSED_STREAM(7L)"),
    ngx_string("CANCEL(8L)"),
    ngx_string("COMPRESSION_ERROR(9L)"),
    ngx_string("CONNECT_ERROR(10L)"),
    ngx_string("ENHANCE_YOUR_CALM(11L)"),
    ngx_string("INADEQUATE_SECURITY(12L)"),
    ngx_string("HTTP_1_1_REQUIRED(13L)"),
    ngx_string("UNKNOWN_ERROR(MAX)"),
};

static const ngx_nacos_grpc_frame_handler frame_handlers[] = {
    ngx_nacos_grpc_parse_data_frame,
    ngx_nacos_grpc_parse_header_frame,
    ngx_nacos_grpc_parse_unknown_frame,
    ngx_nacos_grpc_parse_rst_frame,
    ngx_nacos_grpc_parse_settings_frame,
    ngx_nacos_grpc_parse_unknown_frame,
    ngx_nacos_grpc_parse_ping_frame,
    ngx_nacos_grpc_parse_goaway_frame,
    ngx_nacos_grpc_parse_window_update_frame,
    ngx_nacos_grpc_parse_header_frame};

ngx_nacos_grpc_ctx_t *ngx_nacos_open_grpc_ctx(ngx_nacos_main_conf_t *ncf) {
    grpc_ctx.gc = ngx_nacos_open_grpc_conn(ncf);
    if (grpc_ctx.gc == NULL) {
        return NULL;
    }

    grpc_ctx.ncf = ncf;
    grpc_ctx.ev.log = ncf->error_log;
    grpc_ctx.ev.handler = ngx_nacos_grpc_ctl_handler;
    return &grpc_ctx;
}

static ngx_nacos_grpc_conn_t *ngx_nacos_open_grpc_conn(
    ngx_nacos_main_conf_t *ncf) {
    ngx_connection_t *c;
    ngx_nacos_grpc_conn_t *gc;
    ngx_pool_t *pool;
    ngx_uint_t try;
    ngx_int_t rc;

    pool = ngx_create_pool(ncf->udp_pool_size, ncf->error_log);
    if (pool == NULL) {
        return NULL;
    }

    gc = ngx_pcalloc(pool, sizeof(*gc));
    if (gc == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    gc->read_buf = ngx_create_temp_buf(pool, READ_BUF_CAP);
    if (gc->read_buf == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_queue_init(&gc->io_blocking_list);
    ngx_queue_init(&gc->conn_win_blocking_list);
    ngx_queue_init(&gc->all_streams);
    ngx_rbtree_init(&gc->st_tree, &gc->st_sentinel, ngx_rbtree_insert_value);

    gc->peer.start_time = ngx_current_msec;
    gc->peer.log_error = NGX_ERROR_INFO;
    gc->peer.log = ncf->error_log;
    gc->peer.get = ngx_nacos_aux_get_addr;
    gc->peer.free = ngx_nacos_aux_free_addr;
    gc->peer.data = &ncf->grpc_server_list;

    try = 0;

connect:
    rc = ngx_event_connect_peer(&gc->peer);
    try++;
    if (rc == NGX_ERROR) {
        if (gc->peer.name) {
            ngx_log_error(NGX_LOG_WARN, gc->peer.log, 0,
                          "http connection connect to %V error", gc->peer.name);
        }
        if (gc->peer.sockaddr) {
            gc->peer.free(&gc->peer, gc->peer.data, NGX_ERROR);
        }
        if (try < ncf->server_list.nelts) {
            goto connect;
        }
        goto connect_failed;
    }

    c = gc->peer.connection;
    c->data = gc;
    c->pool = pool;
    c->write->handler = ngx_nacos_grpc_event_handler;
    c->read->handler = ngx_nacos_grpc_event_handler;
    c->requests = 0;
    gc->conn = c;
    gc->pool = pool;
    gc->settings.init_window_size = NGX_HTTP_V2_DEFAULT_WINDOW;
    gc->settings.max_frame_size = NGX_HTTP_V2_DEFAULT_FRAME_SIZE;

    if (rc == NGX_AGAIN) {
        // connecting
        gc->stat = connecting;
        c->log->action = "nacos http connection connecting";
        ngx_add_timer(c->write, 3000);  // set connect time out
        return gc;
    }

    gc->stat = prepare_conn;
    // rc == NGX_OK
    rc = ngx_nacos_grpc_write_handler(gc, c->write);
    if (rc == NGX_OK || rc == NGX_AGAIN) {
        return gc;
    }

connect_failed:
    ngx_log_error(NGX_LOG_WARN, gc->peer.log, 0,
                  "create http connection error after try %d", try);
    ngx_destroy_pool(pool);
    return NULL;
}

static void ngx_nacos_grpc_ctl_handler(ngx_event_t *ev) {
    grpc_ctx.gc = ngx_nacos_open_grpc_conn(grpc_ctx.ncf);
    grpc_ctx.key_idx = 0;
    if (grpc_ctx.gc == NULL) {
        ngx_add_timer(&grpc_ctx.ev, 5000);
    }
}

static void ngx_nacos_grpc_event_handler(ngx_event_t *ev) {
    ngx_connection_t *c;
    ngx_nacos_grpc_conn_t *gc;
    ngx_int_t rc;

    c = ev->data;
    gc = c->data;
    if (ev == c->read) {
        rc = ngx_nacos_grpc_read_handler(gc, ev);
    } else {
        rc = ngx_nacos_grpc_write_handler(gc, ev);
    }

    if (rc == NGX_ERROR || rc == NGX_DONE) {
        ngx_nacos_grpc_close_connection(
            gc, rc == NGX_ERROR ? NGX_NC_ERROR : NGX_NC_TIRED);
        ngx_add_timer(&grpc_ctx.ev, 3000);
    }
}

static ngx_int_t ngx_nacos_grpc_write_handler(ngx_nacos_grpc_conn_t *gc,
                                              ngx_event_t *ev) {
    ngx_queue_t *node;
    int err;
    socklen_t len;
    ngx_int_t rc;
    ngx_connection_t *c = gc->peer.connection;

    if (ev->timedout) {
        ev->timedout = 0;
        if (gc->stat == connecting) {
            return NGX_ERROR;
        }
        if (gc->stat == subscribing_config) {
            if (ngx_nacos_grpc_do_subscribe_config_items(gc) == NGX_ERROR) {
                return NGX_ERROR;
            }
        } else if (gc->stat == subscribing_service) {
            if (ngx_nacos_grpc_do_subscribe_service_items(gc) == NGX_ERROR) {
                return NGX_ERROR;
            }
        } else if (gc->stat == subscribed) {
            // send ping.
            if (ngx_nacos_send_ping_frame(gc) == NGX_ERROR) {
                return NGX_ERROR;
            }
        } else {
            return NGX_ERROR;
        }
    }

    if (gc->stat == connecting) {
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
        gc->stat = prepare_conn;
    }

    if (gc->stat == prepare_conn) {
        gc->m_stream = ngx_nacos_grpc_create_stream(gc);
        if (gc->m_stream == NULL) {
            return NGX_ERROR;
        }
        gc->stat = waiting_conn_prepared;
        return ngx_nacos_grpc_send_conn_start(gc->m_stream);
    }

    if (ev->ready) {
        while (!ngx_queue_empty(&gc->io_blocking_list)) {
            node = ngx_queue_head(&gc->io_blocking_list);
            rc = ngx_nacos_grpc_do_send(
                ngx_queue_data(node, ngx_nacos_grpc_stream_t, io_blocking));
            if (rc != NGX_OK) {
                return rc;
            }
        }
    }

    return NGX_OK;
}

static ngx_int_t ngx_nacos_grpc_read_handler(ngx_nacos_grpc_conn_t *gc,
                                             ngx_event_t *ev) {
    ssize_t rc;

    if (gc->stat < waiting_conn_prepared) {
        // write handler 处理
        return NGX_OK;
    }

    for (;;) {
        rc = gc->conn->recv(gc->conn, gc->read_buf->last,
                            (gc->read_buf->end - gc->read_buf->last));
        if (rc > 0) {
            gc->read_buf->last += rc;
            rc = ngx_nacos_grpc_parse_frame(gc);
            if (rc == NGX_DONE || rc == NGX_ERROR) {
                return rc;
            }
            continue;
        } else if (rc == 0) {
            return NGX_DONE;
        } else if (rc == NGX_AGAIN) {
            return NGX_OK;
        }
    }
}

static ngx_int_t ngx_nacos_grpc_parse_frame(ngx_nacos_grpc_conn_t *gc) {
    ngx_buf_t *b;
    size_t len;
    ngx_int_t rc;
    u_char *pp, *lp;

    b = gc->read_buf;

    for (;;) {
        len = b->last - b->pos;

        if (gc->parse_stat == parse_frame_header) {
            if (len < 9) {
                return NGX_AGAIN;
            }
            gc->frame_size =
                (((size_t) b->pos[0]) << 16) | (b->pos[1] << 8) | (b->pos[2]);
            gc->frame_type = b->pos[3];
            gc->frame_flags = b->pos[4];
            gc->frame_stream_id = (((ngx_uint_t) (b->pos[5] & 0x7f)) << 24) |
                                  (b->pos[6] << 16) | (b->pos[7] << 8) |
                                  b->pos[8];
            gc->parse_stat = parse_frame_payload;
            b->pos += 9;
            gc->frame_start = 1;
            gc->frame_end = len - 9 >= gc->frame_size ? 1 : 0;
        }

        if (gc->parse_stat == parse_frame_payload) {
            if (gc->frame_type >
                sizeof(frame_handlers) / sizeof(ngx_nacos_grpc_frame_handler)) {
                return NGX_ERROR;
            }
            pp = b->pos;
            lp = b->last;
            if ((size_t) (lp - pp) > gc->frame_size) {
                b->last = pp + gc->frame_size;
            }
            rc = frame_handlers[gc->frame_type](gc);
            if (b->pos == b->last) {
                gc->parse_stat = parse_frame_header;
            } else if (b->pos != pp) {
                gc->frame_start = 0;
            }
            b->last = lp;
            if (rc == NGX_ERROR || rc == NGX_DONE) {
                return rc;
            } else if (rc == NGX_AGAIN) {
                len = b->last - b->pos;
                if (len > 0 && len * 4 < (size_t) (b->end - b->start) &&
                    (size_t) (b->end - b->pos) * 2 <
                        (size_t) (b->end - b->start)) {
                    ngx_memcpy(b->start, b->pos, len);
                    b->pos = b->start;
                    b->last = b->pos + len;
                } else if (len == 0) {
                    b->pos = b->last = b->start;
                }
                return rc;
            }
        }
    }
}

static ngx_nacos_grpc_stream_t *ngx_nacos_grpc_create_stream(
    ngx_nacos_grpc_conn_t *gc) {
    ngx_pool_t *pool;
    ngx_nacos_grpc_stream_t *st;

    if (gc->next_stream_id == 0) {
        pool = gc->pool;
    } else {
        pool = ngx_create_pool(grpc_ctx.ncf->udp_pool_size,
                               grpc_ctx.ncf->error_log);
        if (pool == NULL) {
            return NULL;
        }
    }

    st = ngx_pcalloc(pool, sizeof(*st));
    if (st == NULL) {
        goto err;
    }

    st->pool = pool;
    st->conn = gc;
    st->send_win = gc->settings.init_window_size;
    st->recv_win = 0x7fffffff;  // 发送了 init_window 和 window_update
    st->grpc_status = NGX_NACOS_GRPC_DEFAULT_GRPC_STATUS;
    st->stream_id = st->node.key = gc->next_stream_id;
    if (gc->next_stream_id == 0) {
        gc->next_stream_id = 1;
    } else {
        gc->next_stream_id += 2;
    }

    ngx_queue_insert_tail(&gc->all_streams, &st->queue);
    ngx_rbtree_insert(&gc->st_tree, &st->node);
    return st;
err:
    if (gc->next_stream_id != 0) {
        ngx_destroy_pool(pool);
    }
    return NULL;
}

static void ngx_nacos_grpc_close_stream(ngx_nacos_grpc_stream_t *st) {
    ngx_nacos_grpc_conn_t *gc;
    ngx_nacos_grpc_buf_t *h, *t;

    gc = st->conn;
    if (st == gc->m_stream) {
        return;
    }

    if (st->header_sent && !st->end_stream) {
        // TODO send abort frame
        return;
    }

    if (st->send_buf_block) {
        ngx_queue_remove(&st->io_blocking);
    }

    if (st->send_buf_block_conn) {
        ngx_queue_remove(&st->conn_win_blocking);
    }

    h = st->block_bufs.head;
    while (h != NULL) {
        t = h->next;
        ngx_nacos_grpc_free_buf(h->stream, h);
        h = t;
    }
    h = st->non_block_bufs.head;
    while (h != NULL) {
        t = h->next;
        ngx_nacos_grpc_free_buf(h->stream, h);
        h = t;
    }

    ngx_queue_remove(&st->queue);
    ngx_rbtree_delete(&gc->st_tree, &st->node);
    ngx_destroy_pool(st->pool);
}

static ngx_int_t ngx_nacos_grpc_send_conn_start(ngx_nacos_grpc_stream_t *st) {
    ngx_nacos_grpc_buf_t *buf;

    buf = ngx_nacos_grpc_alloc_buf(st,
                                   sizeof(ngx_nacos_grpc_connection_start) - 1);
    if (buf == NULL) {
        return NGX_ERROR;
    }

    buf->stream = st;
    buf->len = sizeof(ngx_nacos_grpc_connection_start) - 1;
    ngx_memcpy(buf->b, ngx_nacos_grpc_connection_start, buf->len);

    return ngx_nacos_grpc_send_buf(buf, 0);
}

static ngx_nacos_grpc_buf_t *ngx_nacos_grpc_alloc_buf(
    ngx_nacos_grpc_stream_t *st, size_t cap) {
    ngx_nacos_grpc_buf_t *buf;
    u_char *b;

    buf = ngx_alloc(sizeof(ngx_nacos_grpc_buf_t) + cap, st->pool->log);
    if (buf == NULL) {
        return NULL;
    }
    b = ((u_char *) buf) + sizeof(ngx_nacos_grpc_buf_t);
    buf->b = b;
    buf->stream = st;
    buf->callback = NULL;
    buf->next = NULL;
    buf->cap = cap;
    buf->len = 0;
    buf->consume_win = 0;
    return buf;
}

static ngx_int_t ngx_nacos_grpc_send_buf(ngx_nacos_grpc_buf_t *buf,
                                         ngx_flag_t can_block) {
    ngx_nacos_grpc_stream_t *st;
    ngx_nacos_grpc_conn_t *gc;
    ngx_nacos_grpc_bufs_t *bufs;

    st = buf->stream;
    gc = st->conn;

    bufs = can_block ? &st->block_bufs : &st->non_block_bufs;
    if (bufs->tail) {
        bufs->tail->next = buf;
    } else {
        bufs->head = buf;
    }
    while (buf->next) {
        buf = buf->next;
    }
    bufs->tail = buf;

    if (ngx_nacos_grpc_send_blocking_buf(gc, st) == NGX_ERROR) {
        return NGX_ERROR;
    }
    return NGX_OK;
}

// return NGX_OK/NGX_AGAIN/NGX_ERROR
static ngx_int_t ngx_nacos_grpc_do_send(ngx_nacos_grpc_stream_t *st) {
    ngx_nacos_grpc_buf_t *h, *t;
    ssize_t r;
    ngx_nacos_grpc_conn_t *gc;
    ngx_connection_t *conn;

    h = st->non_block_bufs.head;
    gc = st->conn;
    conn = gc->conn;
    r = NGX_OK;

    if (h == NULL) {
        return r;
    }

    do {
        r = conn->send(gc->conn, h->b, h->len);
        if (r == (ssize_t) h->len) {
            if (h->callback) {
                r = h->callback(st, NGX_OK);
            } else {
                r = NGX_OK;
            }
            t = h->next;
            ngx_nacos_grpc_free_buf(h->stream, h);
            h = t;
        } else if (r > 0) {
            h->len -= r;
            h->b += r;
            r = NGX_AGAIN;
            break;
        } else if (r != NGX_AGAIN) {  // NGX_ERROR
            if (h->callback) {
                (void) h->callback(st, NGX_ERROR);
            }
            t = h->next;
            ngx_nacos_grpc_free_buf(h->stream, h);
            h = t;
            break;
        } else {
            // NGX_AGAIN
            break;
        }
    } while (h != NULL);
    st->non_block_bufs.head = h;
    if (h == NULL) {
        st->non_block_bufs.tail = NULL;
        if (st->send_buf_block) {
            st->send_buf_block = 0;
            ngx_queue_remove(&st->io_blocking);
        }
    } else if (r == NGX_AGAIN && !st->send_buf_block) {
        st->send_buf_block = 1;
        ngx_queue_insert_tail(&gc->io_blocking_list, &st->io_blocking);
    }
    return r;
}

static void ngx_nacos_grpc_close_connection(ngx_nacos_grpc_conn_t *gc,
                                            ngx_int_t status) {
    ngx_nacos_grpc_buf_t *h, *t;
    ngx_queue_t *stq;
    ngx_nacos_grpc_stream_t *st;

    if (gc->m_stream) {
        st = gc->m_stream;
        if (st->send_buf_block) {
            ngx_queue_remove(&st->io_blocking);
        }
        h = st->block_bufs.head;
        while (h != NULL) {
            t = h->next;
            ngx_nacos_grpc_free_buf(h->stream, h);
            h = t;
        }
        h = st->non_block_bufs.head;
        while (h != NULL) {
            t = h->next;
            ngx_nacos_grpc_free_buf(h->stream, h);
            h = t;
        }
        ngx_queue_remove(&st->queue);
        ngx_rbtree_delete(&gc->st_tree, &st->node);
    }

    for (;;) {
        if (ngx_queue_empty(&gc->all_streams)) {
            break;
        }
        stq = ngx_queue_last(&gc->all_streams);
        st = ngx_queue_data(stq, ngx_nacos_grpc_stream_t, queue);
        ngx_nacos_grpc_close_stream(st);
    }

    ngx_close_connection(gc->conn);
    gc->peer.free(&gc->peer, gc->peer.data, status);
    ngx_destroy_pool(gc->pool);
}

static ngx_int_t ngx_nacos_grpc_parse_unknown_frame(ngx_nacos_grpc_conn_t *gc) {
    ngx_log_debug2(NGX_LOG_DEBUG_CORE, gc->conn->log, 0,
                   "unknown frame type:%d, flags:%d", gc->frame_type,
                   gc->frame_flags);
    return NGX_ERROR;
}

static ngx_int_t ngx_nacos_grpc_parse_data_frame(ngx_nacos_grpc_conn_t *gc) {
    ngx_nacos_grpc_stream_t *st;
    ngx_buf_t *buf, *tb;
    u_char *p;
    ngx_int_t rc;
    size_t len, msg_size;
    ngx_str_t proto_msg;
    enum state { parsing_prefix, parsing_msg };

    st = ngx_nacos_grpc_find_stream(gc, gc->frame_stream_id);
    if (gc->frame_stream_id == 0 || st == NULL) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent data frame "
                      "zero stream id: %uz",
                      gc->frame_stream_id);
        return NGX_ERROR;
    }

    if (!st->end_header) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent data frame "
                      "header not complete: %uz",
                      gc->frame_stream_id);
        return NGX_ERROR;
    }

    rc = NGX_OK;
    tb = st->tmp_buf;
    buf = gc->read_buf;

    for (;;) {
        p = buf->pos;
        len = buf->last - p;

        if (gc->frame_start) {
            if (gc->frame_flags & NGX_HTTP_V2_PADDED_FLAG) {
                if (len < 1) {
                    return NGX_AGAIN;
                }
                st->padding = p[0];
                ++p;
                buf->pos = p;
                st->recv_win--;
            } else {
                st->padding = 0;
            }
        }

        if (st->parsing_state == parsing_prefix) {
            if (len < 5) {
                rc = NGX_OK;
                break;
            }
            if (p[0] != 0) {
                ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                              "nacos server sent data frame "
                              "send compressed msg: %uz",
                              gc->frame_stream_id);
                rc = NGX_ERROR;
                break;
            }
            msg_size = (p[1] << 24) | (p[2] << 16) | (p[3] << 8) | p[4];
            if (ngx_nacos_grpc_realloc_tmp_buf(st, msg_size) != NGX_OK) {
                rc = NGX_ERROR;
                break;
            }
            tb = st->tmp_buf;
            p += 5;
            len -= 5;
            buf->pos = p;
            st->parsing_state = parsing_msg;
            st->proto_len = msg_size;
            st->recv_win -= 5;
        }

        if (st->parsing_state == parsing_msg) {
            if (len > st->proto_len - (tb->last - tb->pos)) {
                len = st->proto_len - (tb->last - tb->pos);
            }
            ngx_memcpy(tb->last, p, len);
            tb->last += len;
            p += len;
            buf->pos = p;
            st->recv_win -= len;
            if (gc->frame_end && st->padding) {
                tb->last -= st->padding;
                st->padding = 0;
            }
            len = tb->last - tb->pos;
            if (len == st->proto_len) {
                proto_msg.len = len;
                proto_msg.data = tb->pos;
                rc = ngx_nacos_grpc_parse_proto_msg(st, &proto_msg);
                tb->pos = tb->last = tb->start;
                st->parsing_state = parsing_prefix;
                if (rc != NGX_OK) {
                    break;
                }
            }
        }
    }

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (gc->frame_end) {
        st->end_stream = gc->frame_flags & NGX_HTTP_V2_END_STREAM_FLAG ? 1 : 0;
    }

    if (rc == NGX_OK && gc->frame_end && !st->end_stream &&
        st->recv_win < 65536) {
        if (ngx_nacos_grpc_send_win_update_frame(st, 20 * 1024 * 1024) ==
            NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    if (gc->frame_end && gc->m_stream->recv_win < 65535) {
        if (ngx_nacos_grpc_send_win_update_frame(
                gc->m_stream, 20 * 1024 * 1024) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }
    if (st->end_stream) {
        ngx_nacos_grpc_close_stream(st);
    }
    return rc;
}

static ngx_int_t ngx_nacos_grpc_parse_proto_msg(ngx_nacos_grpc_stream_t *st,
                                                ngx_str_t *proto_msg) {
    ngx_nacos_grpc_payload_decode_t de;
    ngx_int_t rc;

    if (st->resp_status == 200) {
        rc = NGX_OK;
        if (st->resp_grpc_encode && st->stream_handler) {
            de.input = *proto_msg;

            rc = ngx_nacos_grpc_decode_payload(&de);

            if (rc == NGX_OK) {
                rc = st->stream_handler(st, de.payload_type, de.json);
                if (rc != NGX_OK) {
                    ngx_log_error(NGX_LOG_WARN, st->conn->conn->log, 0,
                                  "receive nacos proto msg [%s]:[%d]", de.type,
                                  rc);
                }
            } else {
                rc = st->stream_handler(st, NONE__END, NULL);
                ngx_log_error(NGX_LOG_WARN, st->conn->conn->log, 0,
                              "decode proto msg error", de.type, rc);
            }

            if (rc != NGX_ERROR) {
                rc = NGX_OK;
            }
            ngx_nacos_grpc_decode_payload_destroy(&de);
        }
        return rc;
    }
    return NGX_ERROR;
}

static ngx_int_t ngx_nacos_grpc_parse_header_frame(ngx_nacos_grpc_conn_t *gc) {
    ngx_nacos_grpc_stream_t *st;
    u_char flags;
    ngx_buf_t *b, *tb;
    size_t min, len, field_len;
    ngx_uint_t dep;
    ngx_flag_t huffmanEncoded;
    u_char ch, index, *p, *last, *tp, tmp[512];
    ngx_str_t key, value;
    ngx_int_t rc;

    enum {
        s_start = 0,
        s_indexed_header_name,
        s_literal_header_name_length_prefix,
        s_literal_header_name_length,
        s_literal_header_name,
        s_literal_header_value_length_prefix,
        s_literal_header_value_length,
        s_literal_header_value,
    } state;

    enum parsing_stat { p_start = 0, p_receiving, p_end_header };

    st = ngx_nacos_grpc_find_stream(gc, gc->frame_stream_id);
    if (st == NULL) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent header frame "
                      "unknown stream id: %uz",
                      gc->frame_stream_id);
        return NGX_ERROR;
    }

    b = gc->read_buf;
    len = b->last - b->pos;
    p = b->pos;

    flags = gc->frame_flags;

    if (gc->frame_type == NGX_HTTP_V2_HEADERS_FRAME) {
        if (st->parsing_state == s_start) {
            st->resp_grpc_encode = 0;
            st->grpc_status = NGX_NACOS_GRPC_DEFAULT_GRPC_STATUS;
            // 解析 padding length 和 PRIORITY
            min = (flags & NGX_HTTP_V2_PADDED_FLAG ? 1 : 0) +
                  (flags & NGX_HTTP_V2_PRIORITY_FLAG ? 5 : 0);
            if (gc->frame_size < min) {
                ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                              "nacos server sent headers frame "
                              "with invalid length: %uz",
                              gc->frame_size);
                return NGX_ERROR;
            }
            if (len < min) {
                return NGX_AGAIN;
            }

            if (flags & NGX_HTTP_V2_PADDED_FLAG) {
                st->padding = *p++;
            }
            if (gc->frame_size < st->padding + min) {
                ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                              "nacos server sent headers frame "
                              "with invalid length: %uz",
                              gc->frame_size);
                return NGX_ERROR;
            }

            if (flags & NGX_HTTP_V2_PRIORITY_FLAG) {
                dep = ((p[0] & 0x7F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                if (st->stream_id == dep) {
                    ngx_log_error(
                        NGX_LOG_ERR, gc->conn->log, 0,
                        "nacos server sent headers frame "
                        "with dependency stream id of the same as self: %uz",
                        gc->frame_size);
                    return NGX_ERROR;
                }
                p += 5;
            }
            b->pos = p;
            len = b->last - p;
            gc->frame_size -= min;
            gc->frame_flags &=
                ~(NGX_HTTP_V2_PADDED_FLAG | NGX_HTTP_V2_PRIORITY_FLAG);
            st->parsing_state = p_receiving;
        }
    }

    if (st->parsing_state == p_receiving) {
        if (ngx_nacos_grpc_realloc_tmp_buf(st, len) != NGX_OK) {
            return NGX_ERROR;
        }
        tb = st->tmp_buf;
        ngx_memcpy(tb->last, p, len);
        tb->last += len;
        b->pos += len;

        if (gc->frame_type == NGX_HTTP_V2_HEADERS_FRAME) {
            if (st->padding && gc->frame_end) {
                // remove padding
                tb->last -= st->padding;
                st->padding = 0;
            }
        }

        if (gc->frame_end &&
            (gc->frame_flags & NGX_HTTP_V2_END_HEADERS_FLAG) != 0) {
            st->end_header = 1;
            st->parsing_state = p_end_header;
            goto parse_header;
        }
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                  "nacos server sent headers frame "
                  "with unknown header status");
    return NGX_ERROR;

// parsing header real
parse_header:
    p = tb->pos;
    last = tb->last;
    state = 0;
    huffmanEncoded = 0;
    field_len = 0;
    index = 0;
    ngx_str_null(&key);
    ngx_str_null(&value);
    for (; p < last;) {
        switch (state) {
            case s_start:
                ch = *p++;
                if ((ch & 0x80) == 0x80) {
                    index = ch & ~0x80;
                    if (index == 0 || index > 61) {
                        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                      "nacos server sent invalid http2 "
                                      "table index: %ui",
                                      index);
                        return NGX_ERROR;
                    }
                    key = *ngx_http_v2_get_static_name(index);
                    value = *ngx_http_v2_get_static_value(index);
                    goto parse;
                } else if ((ch & 0xc0) == 0x40) {
                    index = ch & ~0xc0;
                    if (index > 61) {
                        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                      "nacos server sent invalid http2 "
                                      "table index: %ui",
                                      index);
                        return NGX_ERROR;
                    }
                    if (index == 0) {
                        state = s_literal_header_name_length_prefix;
                    } else if (index == 0x3F) {
                        state = s_indexed_header_name;
                    } else {
                        key = *ngx_http_v2_get_static_name(index);
                        state = s_literal_header_value_length_prefix;
                    }
                } else if ((ch & 0xe0) == 0x20) {
                    index = ch & 0x1F;
                    if (index > 0) {
                        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                      "nacos server sent invalid http2 "
                                      "dynamic table size update: %ui",
                                      index);
                        return NGX_ERROR;
                    }
                } else {
                    index = ch & 0x0F;
                    if (index == 0) {
                        state = s_literal_header_name_length_prefix;
                    } else if (index == 0x0F) {
                        state = s_indexed_header_name;
                    } else {
                        key = *ngx_http_v2_get_static_name(index);
                        state = s_literal_header_value_length_prefix;
                    }
                }
                break;
            case s_indexed_header_name:
                field_len = index;
                if (ngx_nacos_grpc_decode_ule128(&p, last, &field_len) !=
                    NGX_OK) {
                    return NGX_ERROR;
                }
                if (field_len == 0 || field_len > 61) {
                    ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                  "nacos server sent invalid http2 "
                                  "table index: %ui",
                                  index);
                    return NGX_ERROR;
                }
                index = field_len & 0xFF;
                key = *ngx_http_v2_get_static_name(index);
                state = s_literal_header_value_length_prefix;
                break;
            case s_literal_header_name_length_prefix:
                ch = *p++;
                huffmanEncoded = (ch & 0x80) == 0x80;
                index = ch & 0x7F;
                if (index == 0x7f) {
                    state = s_literal_header_name_length;
                } else {
                    field_len = index;
                    state = s_literal_header_name;
                }
                break;
            case s_literal_header_name_length:
                field_len = index;
                if (ngx_nacos_grpc_decode_ule128(&p, last, &field_len) !=
                    NGX_OK) {
                    return NGX_ERROR;
                }
                state = s_literal_header_name;
                break;
            case s_literal_header_name:
                if (field_len > (size_t) (last - p)) {
                    ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                  "nacos server sent invalid http2 "
                                  "header name length: %ui",
                                  field_len);
                    return NGX_ERROR;
                }
                if (huffmanEncoded) {
                    ch = 0;
                    tp = tmp;
                    if (ngx_http_v2_huff_decode(&ch, p, field_len, &tp, 1,
                                                gc->conn->log) != NGX_OK) {
                        ngx_log_error(
                            NGX_LOG_ERR, gc->conn->log, 0,
                            "nacos server sent invalid encoded header");
                        return NGX_ERROR;
                    }
                    key.data = tmp;
                    key.len = tp - key.data;
                } else {
                    key.data = p;
                    key.len = field_len;
                }
                p += field_len;
                state = s_literal_header_value_length_prefix;
                break;
            case s_literal_header_value_length_prefix:
                ch = *p++;
                huffmanEncoded = (ch & 0x80) == 0x80;
                index = ch & 0x7F;
                if (index == 0x7f) {
                    state = s_literal_header_value_length;
                } else if (index == 0) {
                    ngx_str_set(&value, "");
                    goto parse;
                } else {
                    field_len = index;
                    state = s_literal_header_value;
                }
                break;
            case s_literal_header_value_length:
                field_len = index;
                if (ngx_nacos_grpc_decode_ule128(&p, last, &field_len) !=
                    NGX_OK) {
                    return NGX_ERROR;
                }
                state = s_literal_header_value;
                break;
            case s_literal_header_value:
                if (field_len > (size_t) (last - p)) {
                    ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                  "nacos server sent invalid http2 "
                                  "header value length: %ui",
                                  field_len);
                    return NGX_ERROR;
                }
                if (huffmanEncoded) {
                    ch = 0;
                    tp = tmp;
                    if (ngx_http_v2_huff_decode(&ch, p, field_len, &tp, 1,
                                                gc->conn->log) != NGX_OK) {
                        ngx_log_error(
                            NGX_LOG_ERR, gc->conn->log, 0,
                            "nacos server sent invalid encoded header");
                        return NGX_ERROR;
                    }
                    value.data = tmp;
                    value.len = tp - value.data;
                } else {
                    value.data = p;
                    value.len = field_len;
                }
                p += field_len;
                goto parse;
        }
        continue;
    parse:
        if (key.len == sizeof(":status") - 1 &&
            ngx_strncmp(key.data, ":status", key.len) == 0) {
            if (value.len != 3) {
                ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                              "nacos server sent invalid :status \"%V\"",
                              &value);
                return NGX_ERROR;
            }
            st->resp_status = ngx_atoi(value.data, 3);
        } else if (key.len == sizeof("content-type") - 1 &&
                   ngx_strncmp(key.data, "content-type", key.len) == 0) {
            if (value.len == sizeof("application/grpc") - 1 &&
                ngx_strncmp(value.data, "application/grpc", value.len) == 0) {
                st->resp_grpc_encode = 1;
            } else {
                st->resp_grpc_encode = 0;
            }
        } else if (key.len == sizeof("grpc-status") - 1 &&
                   ngx_strncmp(key.data, "grpc-status", key.len) == 0) {
            if (value.len > 2 || value.len == 0) {
                ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                              "nacos server sent invalid grpc-status \"%V\"",
                              &value);
                return NGX_ERROR;
            }
            st->grpc_status = ngx_atoi(value.data, value.len);
        }
        state = s_start;
    }

    tb->pos = tb->last = tb->start;
    st->end_stream = flags & NGX_HTTP_V2_END_STREAM_FLAG ? 1 : 0;
    st->parsing_state = 0;
    rc = NGX_OK;
    if (st->end_stream) {
        if (st->long_live && st->stream_handler) {
            rc = st->stream_handler(st, NONE__END, NULL);
        }
        ngx_nacos_grpc_close_stream(st);
    }

    return rc;
}

static ngx_int_t ngx_nacos_grpc_parse_rst_frame(ngx_nacos_grpc_conn_t *gc) {
    return NGX_OK;
}

static ngx_int_t ngx_nacos_grpc_parse_settings_frame(
    ngx_nacos_grpc_conn_t *gc) {
    ngx_uint_t key, value, window_update;
    ngx_buf_t *b;
    size_t len;
    ngx_nacos_grpc_buf_t *buf;
    ngx_queue_t *node;
    ngx_nacos_grpc_stream_t *st;

    if (gc->frame_stream_id) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent settings frame "
                      "with non-zero stream id: %ui",
                      gc->frame_stream_id);
        return NGX_ERROR;
    }
    if (gc->frame_flags & NGX_HTTP_V2_ACK_FLAG) {
        if (gc->frame_size != 0) {
            ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                          "nacos server sent settings frame "
                          "with ack flag and non-zero length: %uz",
                          gc->frame_size);
            return NGX_ERROR;
        }
        if (gc->stat == waiting_conn_prepared) {
            gc->stat = prepare_subscribe;
            if (ngx_nacos_grpc_send_subscribe_request(gc) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }
        return NGX_OK;
    }

    if (gc->frame_size % 6 != 0) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent settings frame "
                      "with invalid length: %uz",
                      gc->frame_size);
        return NGX_ERROR;
    }
    b = gc->read_buf;
    len = 0;

    while (b->last - b->pos >= 6 && len < gc->frame_size) {
        key = (b->pos[0] << 8) | b->pos[1];
        value = ((ngx_uint_t) b->pos[2] << 24) |
                ((ngx_uint_t) b->pos[3] << 16) | ((ngx_uint_t) b->pos[4] << 8) |
                b->pos[5];
        b->pos += 6;
        len += 6;
        switch (key) {
            case 1:
                gc->settings.header_table_size = value;
                break;
            case 3:
                gc->settings.max_conn_streams = value;
                break;
            case 4:
                if (value > NGX_HTTP_V2_MAX_WINDOW) {
                    ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                                  "nacos server sent settings frame "
                                  "with too large initial window size: %ui",
                                  value);
                    return NGX_ERROR;
                }
                window_update = value - gc->settings.init_window_size;
                gc->settings.init_window_size = value;
                for (node = ngx_queue_head(&gc->all_streams);
                     node != ngx_queue_sentinel(&gc->all_streams);
                     node = ngx_queue_next(node)) {
                    st = ngx_queue_data(node, ngx_nacos_grpc_stream_t, queue);
                    if (st == gc->m_stream) {
                        continue;
                    }
                    if (ngx_nacos_grpc_update_send_window(
                            gc, st->stream_id, window_update) == NGX_ERROR) {
                        return NGX_ERROR;
                    }
                }

                break;
            case 5:
                gc->settings.max_frame_size = value;
                break;
            case 6:
                gc->settings.max_header_list_size = value;
                break;
            default:
                ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                              "nacos server sent settings frame "
                              "with invalid key: %uz value: %uz",
                              key, value);
                return NGX_ERROR;
        }
    }
    if (len < gc->frame_size) {
        gc->frame_size -= len;
        return NGX_AGAIN;
    }
    buf = ngx_nacos_grpc_alloc_buf(gc->m_stream, 9);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    ngx_memzero(buf->b, 9);
    buf->b[3] = NGX_HTTP_V2_SETTINGS_FRAME;
    buf->b[4] = NGX_HTTP_V2_ACK_FLAG;
    buf->len = 9;
    return ngx_nacos_grpc_send_buf(buf, 0);
}

static ngx_int_t ngx_nacos_grpc_parse_ping_frame(ngx_nacos_grpc_conn_t *gc) {
    u_char *p;
    uint64_t data;
    ngx_nacos_grpc_buf_t *buf;

    if (gc->frame_size != 8) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent ping frame "
                      "with invalid length: %uz",
                      gc->frame_size);
        return NGX_ERROR;
    }

    if (!gc->frame_end) {
        return NGX_AGAIN;
    }
    p = gc->read_buf->pos;
    gc->read_buf->pos = p + 8;
    data = ((uint64_t) p[0] << 56) | ((uint64_t) p[1] << 48) |
           ((uint64_t) p[2] << 40) | ((uint64_t) p[3] << 32) |
           ((uint64_t) p[4] << 24) | ((uint64_t) p[5] << 16) |
           ((uint64_t) p[6] << 8) | (uint64_t) p[7];
    if (gc->frame_flags & NGX_HTTP_V2_ACK_FLAG) {
        if (data != gc->heartbeat) {
            ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                          "nacos server sent ping frame "
                          "with invalid ack: %uz",
                          data);
            return NGX_ERROR;
        }
        ngx_add_timer(gc->conn->write, NGX_NACOS_GRPC_DEFAULT_PING_INTERVAL);
        return NGX_OK;
    }

    buf = ngx_nacos_grpc_alloc_buf(gc->m_stream, 9 + 8);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    ngx_nacos_grpc_encode_frame_header(
        gc->m_stream, buf->b, NGX_HTTP_V2_PING_FRAME, NGX_HTTP_V2_ACK_FLAG, 8);
    buf->len = 9 + 8;
    p = buf->b + 9;
    p[0] = (data >> 56) & 0xFF;
    p[1] = (data >> 48) & 0xFF;
    p[2] = (data >> 40) & 0xFF;
    p[3] = (data >> 32) & 0xFF;
    p[4] = (data >> 24) & 0xFF;
    p[5] = (data >> 16) & 0xFF;
    p[6] = (data >> 8) & 0xFF;
    p[7] = data & 0xFF;
    return ngx_nacos_grpc_send_buf(buf, 0);
}

static ngx_int_t ngx_nacos_grpc_parse_goaway_frame(ngx_nacos_grpc_conn_t *gc) {
    u_char *p;
    ngx_uint_t last_st_id, err_code;
    ngx_str_t err_msg, *err_name;
    if (!gc->frame_end) {
        return NGX_AGAIN;
    }

    p = gc->read_buf->pos;

    last_st_id = ((p[0] & 0x7F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    p += 4;
    err_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    p += 4;
    err_msg.data = p;
    err_msg.len = gc->frame_size - 8;
    gc->read_buf->pos = gc->read_buf->last;

    if (err_code < sizeof(http2_err) / sizeof(ngx_str_t)) {
        err_name = &http2_err[err_code];
    } else {
        err_name = &http2_err[sizeof(http2_err) / sizeof(ngx_str_t) - 1];
    }

    ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                  "nacos server sent goaway frame:\n"
                  "\tlast_stream_id:%ul\n"
                  "\terr_name:%V\n\terr_msg:%V",
                  last_st_id, err_name, &err_msg);
    return NGX_DONE;
}

static ngx_int_t ngx_nacos_grpc_parse_window_update_frame(
    ngx_nacos_grpc_conn_t *gc) {
    ngx_buf_t *b;
    ngx_uint_t win_update;
    if (gc->frame_size != 4) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent window update frame "
                      "with invalid length: %uz",
                      gc->frame_size);
        return NGX_ERROR;
    }

    b = gc->read_buf;
    if (b->last - b->pos < 4) {
        return NGX_AGAIN;
    }
    win_update =
        (b->pos[0] & 0x7f) << 24 | b->pos[1] << 16 | b->pos[2] << 8 | b->pos[3];
    b->pos += 4;

    return ngx_nacos_grpc_update_send_window(gc, gc->frame_stream_id,
                                             win_update);
}

static ngx_nacos_grpc_stream_t *ngx_nacos_grpc_find_stream(
    ngx_nacos_grpc_conn_t *gc, ngx_uint_t st_id) {
    ngx_rbtree_node_t *node, *sentinel;

    node = gc->st_tree.root;
    sentinel = gc->st_tree.sentinel;
    while (node != sentinel) {
        if (st_id != node->key) {
            node = st_id < node->key ? node->left : node->right;
            continue;
        }
        return (ngx_nacos_grpc_stream_t *) ((char *) node -
                                            offsetof(ngx_nacos_grpc_stream_t,
                                                     node));
    }
    return NULL;
}

static ngx_int_t ngx_nacos_grpc_update_send_window(ngx_nacos_grpc_conn_t *gc,
                                                   ngx_uint_t st_id,
                                                   ngx_uint_t win_update) {
    ngx_nacos_grpc_stream_t *st;
    ngx_queue_t *node;
    ngx_int_t rc;

    st = ngx_nacos_grpc_find_stream(gc, st_id);
    if (st == NULL) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server unknown frame id");
        return NGX_ERROR;
    }

    if (win_update > (size_t) NGX_HTTP_V2_MAX_WINDOW - st->send_win) {
        ngx_log_error(NGX_LOG_ERR, gc->conn->log, 0,
                      "nacos server sent too large window update");
        return NGX_ERROR;
    }

    st->send_win += win_update;

    if (st != gc->m_stream) {
        return ngx_nacos_grpc_send_blocking_buf(gc, st);
    }

    rc = NGX_OK;
    while (!ngx_queue_empty(&gc->conn_win_blocking_list)) {
        node = ngx_queue_head(&gc->conn_win_blocking_list);
        st = ngx_queue_data(node, ngx_nacos_grpc_stream_t, conn_win_blocking);
        rc = ngx_nacos_grpc_send_blocking_buf(gc, st);
        if (rc != NGX_OK) {
            break;
        }
    }
    if (rc == NGX_AGAIN) {
        rc = NGX_OK;
    }
    return rc;
}

static ngx_int_t ngx_nacos_grpc_send_blocking_buf(ngx_nacos_grpc_conn_t *gc,
                                                  ngx_nacos_grpc_stream_t *st) {
    ngx_nacos_grpc_buf_t *h, *t;
    ngx_nacos_grpc_stream_t *mst;
    int stream_block = 0, conn_block = 0;

    mst = gc->m_stream;

    h = st->block_bufs.head;
    if (h != NULL) {
        t = NULL;
        do {
            stream_block = st->send_win < h->consume_win;
            conn_block = mst->send_win < h->consume_win;
            if (stream_block == 0 && conn_block == 0) {
                st->send_win -= h->consume_win;
                mst->send_win -= h->consume_win;
            } else {
                break;
            }
            t = h;
            h = h->next;
        } while (h != NULL);

        if (h != st->block_bufs.head) {
            if (st->non_block_bufs.tail) {
                st->non_block_bufs.tail->next = st->block_bufs.head;
            } else {
                st->non_block_bufs.head = st->block_bufs.head;
            }
            st->non_block_bufs.tail = t;

            t->next = NULL;
            st->block_bufs.head = h;
            if (h == NULL) {
                st->block_bufs.tail = NULL;
            }
        }
    }
    if (ngx_nacos_grpc_do_send(st) == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (conn_block == 0) {
        if (st->send_buf_block_conn) {
            st->send_buf_block_conn = 0;
            ngx_queue_remove(&st->conn_win_blocking);
        }
        return NGX_OK;
    } else {
        if (st->send_buf_block_conn == 0) {
            ngx_queue_insert_tail(&gc->conn_win_blocking_list,
                                  &st->conn_win_blocking);
        }
        return NGX_AGAIN;
    }
}

static ngx_nacos_grpc_buf_t *ngx_nacos_grpc_encode_request(
    ngx_nacos_grpc_stream_t *st, ngx_str_t *mtd) {
    ngx_nacos_grpc_buf_t *buf;
    size_t len;
    u_char *b;
    u_char tmp[128];

    buf = ngx_nacos_grpc_alloc_buf(st, 1024);
    if (buf == NULL) {
        return NULL;
    }

    b = buf->b + 9;
    // :method: POST
    *b++ = ngx_http_v2_indexed(NGX_HTTP_V2_METHOD_POST_INDEX);
    // :schema: http
    *b++ = ngx_http_v2_indexed(NGX_HTTP_V2_SCHEME_HTTP_INDEX);
    // path:
    *b++ = ngx_http_v2_inc_indexed(NGX_HTTP_V2_PATH_INDEX);
    b = ngx_http_v2_write_value(b, mtd->data, mtd->len, tmp);
    // AUTHORITY
    *b++ = ngx_http_v2_inc_indexed(NGX_HTTP_V2_AUTHORITY_INDEX);
    b = ngx_http_v2_write_value(b, (u_char *) "nacos-server",
                                sizeof("nacos-server") - 1, tmp);
    // user-agent
    *b++ = ngx_http_v2_inc_indexed(NGX_HTTP_V2_USER_AGENT_INDEX);
    b = ngx_http_v2_write_value(b, (u_char *) "nginx-nacos-grpc-client",
                                sizeof("nginx-nacos-grpc-client") - 1, tmp);
    // content-type
    *b++ = ngx_http_v2_inc_indexed(NGX_HTTP_V2_CONTENT_TYPE_INDEX);
    b = ngx_http_v2_write_value(b, (u_char *) "application/grpc",
                                sizeof("application/grpc") - 1, tmp);
    // te: trailers
    *b++ = 0;
    b = ngx_http_v2_write_name(b, (u_char *) "te", sizeof("te") - 1, tmp);
    b = ngx_http_v2_write_value(b, (u_char *) "trailers",
                                sizeof("trailers") - 1, tmp);
    // grpc-accept-encoding: identity
    *b++ = 0;
    b = ngx_http_v2_write_name(b, (u_char *) "grpc-accept-encoding",
                               sizeof("grpc-accept-encoding") - 1, tmp);
    b = ngx_http_v2_write_value(b, (u_char *) "identity",
                                sizeof("identity") - 1, tmp);
    buf->len = len = b - buf->b;
    ngx_nacos_grpc_encode_frame_header(st, buf->b, NGX_HTTP_V2_HEADERS_FRAME,
                                       NGX_HTTP_V2_END_HEADERS_FLAG, len - 9);
    return buf;
}

#define SUB_SETUP_JSON                \
    "{\"clientVersion\":\"v2.10.0\"," \
    "\"abilities\":{},"               \
    "\"labels\":{\"source\":\"sdk\",\"module\":\"naming\"}}"

static ngx_int_t ngx_nacos_grpc_send_subscribe_request(
    ngx_nacos_grpc_conn_t *gc) {
    ngx_nacos_grpc_stream_t *st;
    ngx_nacos_grpc_buf_t *hbuf, *bbuf;
    ngx_str_t service;
    ngx_nacos_grpc_payload_encode_t en;

    st = NULL;
    hbuf = NULL;
    bbuf = NULL;

    st = ngx_nacos_grpc_create_stream(gc);
    if (st == NULL) {
        goto err;
    }
    st->long_live = 1;

    ngx_str_set(&service, "/BiRequestStream/requestBiStream");
    hbuf = ngx_nacos_grpc_encode_request(st, &service);
    if (hbuf == NULL) {
        goto err;
    }

    en.type = "ConnectionSetupRequest";
    ngx_str_set(&en.json, SUB_SETUP_JSON);
    bbuf = ngx_nacos_grpc_encode_data_msg(st, &en, 0);
    if (bbuf == NULL) {
        goto err;
    }

    hbuf->next = bbuf;
    bbuf->callback = ngx_nacos_mark_conn_subscribe;
    bbuf->consume_win = en.encoded_len + 5;
    return ngx_nacos_grpc_send_buf(hbuf, 1);
err:
    if (bbuf != NULL) {
        ngx_nacos_grpc_free_buf(gc, bbuf);
    }
    if (hbuf != NULL) {
        ngx_nacos_grpc_free_buf(gc, hbuf);
    }
    if (st != NULL) {
        ngx_nacos_grpc_close_stream(st);
    }
    return NGX_ERROR;
}

static ngx_int_t ngx_nacos_mark_conn_subscribe(ngx_nacos_grpc_stream_t *st,
                                               ngx_int_t state) {
    ngx_nacos_grpc_conn_t *gc;
    if (state == NGX_OK) {
        st->stream_handler = ngx_nacos_grpc_subscribe_response_handler;
        gc = st->conn;
        if (gc->stat == prepare_subscribe) {
            gc->stat = subscribing_config;
            ngx_add_timer(gc->conn->write, 1000);
        }
    }
    return state;
}

#define SUBSCRIBE_JSON_FMT    \
    "{\"headers\":{},"        \
    "\"namespace\":\"%V\","   \
    "\"serviceName\":\"%V\"," \
    "\"groupName\":\"%V\","   \
    "\"subscribe\":true,"     \
    "\"clusters\":\"\"}"

static ngx_int_t ngx_nacos_grpc_do_subscribe_service_items(
    ngx_nacos_grpc_conn_t *gc) {
    ngx_str_t req;
    ngx_nacos_main_conf_t *ncf;
    ngx_uint_t idx, len;
    ngx_nacos_key_t **key;
    ngx_nacos_grpc_stream_t *st;
    ngx_nacos_grpc_buf_t *hbuf, *bbuf;
    size_t b_len;
    ngx_nacos_grpc_payload_encode_t en;
    static u_char tmp[512];

    ncf = grpc_ctx.ncf;
    idx = grpc_ctx.key_idx;
    key = ncf->keys.elts;
    len = ncf->keys.nelts;

    if (idx >= len) {
        grpc_ctx.key_idx = 0;
        gc->stat = subscribed;
        ngx_log_error(NGX_LOG_INFO, gc->conn->log, 0, "all keys subscribed");
        // may be all key is subscribed
        ngx_add_timer(gc->conn->write, NGX_NACOS_GRPC_DEFAULT_PING_INTERVAL);
        return NGX_OK;
    }
    st = NULL;
    hbuf = NULL;
    bbuf = NULL;

    st = ngx_nacos_grpc_create_stream(gc);
    if (st == NULL) {
        goto err;
    }

    ngx_str_set(&req, "/Request/request");
    hbuf = ngx_nacos_grpc_encode_request(st, &req);
    if (hbuf == NULL) {
        goto err;
    }

    b_len = ngx_snprintf(tmp, sizeof(tmp), SUBSCRIBE_JSON_FMT,
                         &ncf->service_namespace, &key[idx]->data_id,
                         &key[idx]->group) -
            (u_char *) tmp;

    en.type = "SubscribeServiceRequest";
    en.json.len = b_len;
    en.json.data = tmp;
    bbuf = ngx_nacos_grpc_encode_data_msg(st, &en, 1);
    if (bbuf == NULL) {
        goto err;
    }

    hbuf->next = bbuf;
    bbuf->callback = ngx_nacos_grpc_mark_subscribe_items;
    bbuf->consume_win = en.encoded_len + 5;
    return ngx_nacos_grpc_send_buf(hbuf, 1);

err:
    if (bbuf != NULL) {
        ngx_nacos_grpc_free_buf(st, bbuf);
    }
    if (hbuf != NULL) {
        ngx_nacos_grpc_free_buf(st, hbuf);
    }
    if (st != NULL) {
        ngx_nacos_grpc_close_stream(st);
    }
    return NGX_ERROR;
}

static ngx_int_t ngx_nacos_grpc_do_subscribe_config_items(
    ngx_nacos_grpc_conn_t *gc) {
    ngx_str_t req, tmp;
    ngx_nacos_main_conf_t *ncf;
    ngx_uint_t idx, len;
    ngx_nacos_key_t **key;
    ngx_nacos_grpc_stream_t *st;
    ngx_nacos_grpc_buf_t *hbuf, *bbuf;
    ngx_nacos_grpc_payload_encode_t en;
    yajl_gen gen;
    u_char tmp_buf[128];
    // ngx_uint_t i, l;
    // yajl_gen_status gen_status;

    ncf = grpc_ctx.ncf;
    idx = grpc_ctx.conf_key_idx;
    key = ncf->config_keys.elts;
    len = ncf->config_keys.nelts;

    if (idx >= len) {
        grpc_ctx.conf_key_idx = 0;
        gc->stat = subscribing_service;
        ngx_log_error(NGX_LOG_INFO, gc->conn->log, 0,
                      "all config_keys subscribed");
        ngx_add_timer(gc->conn->write, 5);
        return NGX_OK;
    }

    st = NULL;
    hbuf = NULL;
    bbuf = NULL;
    gen = NULL;

    st = ngx_nacos_grpc_create_stream(gc);
    if (st == NULL) {
        goto err;
    }

    ngx_str_set(&req, "/Request/request");
    hbuf = ngx_nacos_grpc_encode_request(st, &req);
    if (hbuf == NULL) {
        goto err;
    }

    gen = yajl_gen_alloc(NULL);
    if (gen == NULL) {
        goto err;
    }

    // {"listen":true, "configListenContexts": [
    if (yajl_gen_map_open(gen) != yajl_gen_status_ok ||  // {
        yajl_gen_string(gen, (u_char *) "listen", sizeof("listen") - 1) !=
            yajl_gen_status_ok ||                       // "listen"
        yajl_gen_bool(gen, 1) != yajl_gen_status_ok ||  // "true

        yajl_gen_string(gen, (u_char *) "configListenContexts",
                        sizeof("configListenContexts") - 1) !=
            yajl_gen_status_ok ||
        yajl_gen_array_open(gen) !=
            yajl_gen_status_ok  // "configListenContexts":[
    ) {
        goto err;
    }

    len = ngx_min(len, idx + NGX_NACOS_GRPC_CONFIG_BATCH_SIZE);
    for (; idx < len; ++idx) {
        tmp.data = tmp_buf;
        tmp.len = sizeof(tmp_buf);
        if (ngx_nacos_get_config_md5(key[idx], &tmp) != NGX_OK) {
            goto err;
        }

        if (yajl_gen_map_open(gen) != yajl_gen_status_ok ||  // {
            yajl_gen_string(gen, (u_char *) "group", sizeof("group") - 1) !=
                yajl_gen_status_ok ||  // "group": group
            yajl_gen_string(gen, key[idx]->group.data, key[idx]->group.len) !=
                yajl_gen_status_ok ||

            yajl_gen_string(gen, (u_char *) "dataId", sizeof("dataId") - 1) !=
                yajl_gen_status_ok ||  // "dataId": dataId
            yajl_gen_string(gen, key[idx]->data_id.data,
                            key[idx]->data_id.len) != yajl_gen_status_ok ||

            yajl_gen_string(gen, (u_char *) "md5", sizeof("md5") - 1) !=
                yajl_gen_status_ok ||  // "md5": group
            yajl_gen_string(gen, tmp.data, tmp.len) != yajl_gen_status_ok ||

            yajl_gen_string(gen, (u_char *) "tenant", sizeof("tenant") - 1) !=
                yajl_gen_status_ok ||  // "tenant": tenant
            yajl_gen_string(gen, ncf->config_tenant.data,
                            ncf->config_tenant.len) != yajl_gen_status_ok ||

            yajl_gen_map_close(gen) != yajl_gen_status_ok) {
            goto err;
        }
    }

    // ], }
    if (yajl_gen_array_close(gen) != yajl_gen_status_ok ||
        yajl_gen_map_close(gen) != yajl_gen_status_ok) {
        goto err;
    }

    if (yajl_gen_get_buf(gen, (const unsigned char **) &en.json.data,
                         &en.json.len) != yajl_gen_status_ok) {
        goto err;
    }

    en.type = "ConfigBatchListenRequest";
    bbuf = ngx_nacos_grpc_encode_data_msg(st, &en, 1);
    if (bbuf == NULL) {
        goto err;
    }
    st->handler_ctx = (void *) len;

    hbuf->next = bbuf;
    bbuf->callback = ngx_nacos_grpc_mark_subscribe_items;
    bbuf->consume_win = en.encoded_len + 5;
    return ngx_nacos_grpc_send_buf(hbuf, 1);

err:
    if (bbuf != NULL) {
        ngx_nacos_grpc_free_buf(st, bbuf);
    }
    if (hbuf != NULL) {
        ngx_nacos_grpc_free_buf(st, hbuf);
    }
    if (st != NULL) {
        ngx_nacos_grpc_close_stream(st);
    }
    if (gen != NULL) {
        yajl_gen_free(gen);
    }
    return NGX_ERROR;
}

static ngx_int_t ngx_nacos_grpc_mark_subscribe_items(
    ngx_nacos_grpc_stream_t *st, ngx_int_t state) {
    if (state == NGX_OK) {
        st->stream_handler = ngx_nacos_grpc_service_sub_event_resp_handler;
    }
    return state;
}

static ngx_int_t ngx_nacos_grpc_decode_ule128(u_char **pp, const u_char *last,
                                              size_t *result) {
    ngx_flag_t resultStartedAtZero;
    size_t shift;
    u_char b, *p = *pp;
    size_t r = *result;
    for (resultStartedAtZero = r == 0, shift = 0; p < last; ++p, shift += 7) {
        b = *p;
        if (shift == 56 &&
            ((b & 0x80) != 0 || (b == 0x7F && !resultStartedAtZero))) {
            return NGX_ERROR;
        }
        if ((b & 0x80) == 0) {
            r += ((b & 0x7FL) << shift);
            if (r > 0x7FFFFFFF) {
                return NGX_ERROR;
            }
            *pp = p + 1;
            *result = r;
            return NGX_OK;
        }
        r += (b & 0x7FL) << shift;
    }
    return NGX_ERROR;
}

static void ngx_nacos_grpc_encode_payload_init(
    ngx_nacos_grpc_payload_encode_t *en) {
    google__protobuf__any__init(&en->any);
    metadata__init(&en->metadata);
    payload__init(&en->payload);

    en->metadata.n_headers = 0;
    en->metadata.headers = NULL;
    en->metadata.type = (char *) en->type;
    en->metadata.clientip = "";
    en->payload.metadata = &en->metadata;
    en->payload.body = &en->any;
    en->any.value.data = en->json.data;
    en->any.value.len = en->json.len;
    en->buf = NULL;
    en->encoded_len = payload__get_packed_size(&en->payload);
}

static ngx_int_t ngx_nacos_grpc_encode_payload(
    ngx_nacos_grpc_payload_encode_t *en) {
    if (en->buf != NULL) {
        if (en->encoded_len != payload__pack(&en->payload, en->buf)) {
            return NGX_ERROR;
        }
    }
    return NGX_OK;
}

static ngx_nacos_grpc_buf_t *ngx_nacos_grpc_encode_data_msg(
    ngx_nacos_grpc_stream_t *st, ngx_nacos_grpc_payload_encode_t *en,
    ngx_flag_t end_stream) {
    size_t b_len;
    u_char *b;
    ngx_nacos_grpc_buf_t *buf;

    buf = NULL;
    ngx_nacos_grpc_encode_payload_init(en);
    b_len = en->encoded_len;
    if (b_len + 5 > st->conn->settings.max_frame_size) {
        // FIXME protobuf is too long. split to multi data frames
        ngx_log_error(NGX_LOG_ERR, st->conn->conn->log, 0,
                      "protobuf encode not match length");
        goto err;
    }

    buf = ngx_nacos_grpc_alloc_buf(st, b_len + 5 + 9);
    if (buf == NULL) {
        goto err;
    }
    b = buf->b;
    ngx_nacos_grpc_encode_frame_header(
        st, b, NGX_HTTP_V2_DATA_FRAME,
        end_stream ? NGX_HTTP_V2_END_STREAM_FLAG : 0, 5 + b_len);
    b[9] = 0;
    b[10] = (b_len >> 24) & 0xFF;
    b[11] = (b_len >> 16) & 0xFF;
    b[12] = (b_len >> 8) & 0xFF;
    b[13] = b_len & 0xFF;

    en->buf = b + 14;
    if (ngx_nacos_grpc_encode_payload(en) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, st->conn->conn->log, 0,
                      "protobuf encode not match length");
        goto err;
    }
    buf->len = 9 + 5 + b_len;
    return buf;

err:
    if (buf != NULL) {
        ngx_nacos_grpc_free_buf(st, buf);
    }
    return NULL;
}

static ngx_int_t ngx_nacos_grpc_subscribe_response_handler(
    ngx_nacos_grpc_stream_t *st, enum ngx_nacos_payload_type payload_type,
    yajl_val json) {
    ngx_int_t rc;
    if (st->long_live) {
        if (payload_type == ClientDetectionRequest) {
            rc = ngx_nacos_grpc_send_server_push_resp(
                st, json, "ClientDetectionResponse");
            ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                          "ClientDetectionRequest received:[%d]", rc);
        } else if (payload_type == NotifySubscriberRequest) {
            rc = ngx_nacos_grpc_notify_address_shm(st->conn, json);
            ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                          "NotifySubscriberRequest received:[%d]", rc);
            rc = ngx_nacos_grpc_send_server_push_resp(
                st, json, "NotifySubscriberResponse");
        } else if (payload_type == ConfigChangeNotifyRequest) {
            rc = ngx_nacos_grpc_config_change_notified(st->conn, json);
            ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                          "ConfigChangeNotifyRequest received:[%d]", rc);
            rc = ngx_nacos_grpc_send_server_push_resp(
                st, json, "ConfigChangeNotifyResponse");
        } else {
            ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                          "ngx_nacos_grpc_subscribe_response_handler call "
                          "in no_push_stream");
            rc = NGX_ERROR;
        }
    } else {
        //
        ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                      "ngx_nacos_grpc_subscribe_response_handler call "
                      "in no_push_stream");
        rc = NGX_ERROR;
    }

    return rc;
}

static ngx_int_t ngx_nacos_grpc_service_sub_event_resp_handler(
    ngx_nacos_grpc_stream_t *st, enum ngx_nacos_payload_type type,
    yajl_val json) {
    ngx_int_t rc;
    if (type == SubscribeServiceResponse) {
        if (ngx_nacos_grpc_payload_is_response_ok(type, json)) {
            grpc_ctx.key_idx++;
            rc = ngx_nacos_grpc_do_subscribe_service_items(st->conn);
        } else {
            rc = NGX_ERROR;
        }
        ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                      "SubscribeServiceResponse received:[%d]", rc);
    } else if (type == ConfigChangeBatchListenResponse) {
        if (ngx_nacos_grpc_payload_is_response_ok(type, json)) {
            (void) ngx_nacos_grpc_config_change_deal(st, json);
            grpc_ctx.conf_key_idx = (ngx_uint_t) st->handler_ctx;
            rc = ngx_nacos_grpc_do_subscribe_config_items(st->conn);
        } else {
            rc = NGX_ERROR;
        }
        ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                      "ConfigChangeBatchListenResponse received:[%d]", rc);
    } else {
        rc = NGX_ERROR;
    }

    return rc;
}

static ngx_int_t ngx_nacos_grpc_config_change_deal(ngx_nacos_grpc_stream_t *st,
                                                   yajl_val root) {
    yajl_val *it, *et, changedConfigs;
    ngx_int_t rc;

    rc = NGX_ERROR;

    if (root == NULL) {
        goto end;
    }

    changedConfigs = yajl_tree_get_field(root, "changedConfigs", yajl_t_array);
    if (changedConfigs == NULL) {
        goto end;
    }
    rc = NGX_OK;
    it = YAJL_GET_ARRAY(changedConfigs)->values;
    et = it + YAJL_GET_ARRAY(changedConfigs)->len;
    for (; it < et; ++it) {
        if (!YAJL_IS_OBJECT(*it)) {
            ngx_log_error(NGX_LOG_WARN, st->conn->conn->log, 0,
                          "nacos ConfigChangeBatchListenResponse "
                          "changedConfigs is not object");
            continue;
        }
        rc = ngx_nacos_grpc_config_change_notified(st->conn, *it);
        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_WARN, st->conn->conn->log, 0,
                          "nacos ConfigChangeBatchListenResponse "
                          "send_config_query failed");
            break;
        }
        ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                      "nacos ConfigChangeBatchListenResponse "
                      "send_config_query [%d]",
                      rc);
    }
end:
    return rc;
}

static ngx_int_t ngx_nacos_grpc_realloc_tmp_buf(ngx_nacos_grpc_stream_t *st,
                                                size_t asize) {
    size_t s, last_len;
    ngx_buf_t *buf;

    buf = st->tmp_buf;

    if (buf && asize <= (size_t) (buf->end - buf->last)) {
        return NGX_OK;
    }

    last_len = buf ? (buf->last - buf->pos) : 0;
    asize += last_len;
    s = 512;
    while (s < asize) {
        s <<= 1;
    }
    // s >= asize > last_len;

    st->tmp_buf = ngx_create_temp_buf(st->pool, s);
    if (st->tmp_buf == NULL) {
        return NGX_ERROR;
    }

    if (last_len) {
        ngx_memcpy(st->tmp_buf->last, buf->pos, last_len);
        st->tmp_buf->last += last_len;
    }
    return NGX_OK;
}

static ngx_int_t ngx_nacos_grpc_send_win_update_frame(
    ngx_nacos_grpc_stream_t *st, size_t win_update) {
    ngx_nacos_grpc_buf_t *buf;
    u_char *p;
    ngx_int_t rc;

    buf = ngx_nacos_grpc_alloc_buf(st, 9 + 4);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    ngx_nacos_grpc_encode_frame_header(st, buf->b,
                                       NGX_HTTP_V2_WINDOW_UPDATE_FRAME, 0, 4);
    buf->len = 9 + 4;
    p = buf->b + 9;
    p[0] = (win_update >> 24) & 0x7F;
    p[1] = (win_update >> 16) & 0xFF;
    p[2] = (win_update >> 8) & 0xFF;
    p[3] = win_update & 0xFF;
    buf->stream->handler_ctx = (void *) win_update;
    rc = ngx_nacos_grpc_send_buf(buf, 0);
    if (rc == NGX_OK) {
        st->recv_win += win_update;
    }
    return rc;
}

static ngx_int_t ngx_nacos_send_ping_frame(ngx_nacos_grpc_conn_t *gc) {
    ngx_nacos_grpc_buf_t *buf;
    u_char *p;
    ngx_uint_t data;
    buf = ngx_nacos_grpc_alloc_buf(gc->m_stream, 9 + 8);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    ngx_nacos_grpc_encode_frame_header(gc->m_stream, buf->b,
                                       NGX_HTTP_V2_PING_FRAME, 0, 8);
    p = buf->b + 9;
    data = ++gc->heartbeat;
    p[0] = (data >> 56) & 0xFF;
    p[1] = (data >> 48) & 0xFF;
    p[2] = (data >> 40) & 0xFF;
    p[3] = (data >> 32) & 0xFF;
    p[4] = (data >> 24) & 0xFF;
    p[5] = (data >> 16) & 0xFF;
    p[6] = (data >> 8) & 0xFF;
    p[7] = data & 0xFF;
    buf->len = 9 + 8;
    return ngx_nacos_grpc_send_buf(buf, 0);
}

static ngx_int_t ngx_nacos_grpc_decode_payload(
    ngx_nacos_grpc_payload_decode_t *de) {
    size_t i, len;

    de->type = "";
    de->json = NULL;
    de->result = payload__unpack(NULL, de->input.len, de->input.data);
    if (de->result == NULL) {
        return NGX_ERROR;
    }
    if (de->result->metadata == NULL) {
        payload__free_unpacked(de->result, NULL);
        de->result = NULL;
        return NGX_ERROR;
    }
    if (de->result->metadata->type == NULL) {
        payload__free_unpacked(de->result, NULL);
        de->result = NULL;
        return NGX_ERROR;
    }
    if (de->result->body == NULL) {
        payload__free_unpacked(de->result, NULL);
        de->result = NULL;
        return NGX_ERROR;
    }
    de->type = de->result->metadata->type;
    de->out_json.data = de->result->body->value.data;
    de->out_json.len = de->result->body->value.len;
    de->payload_type = 0;

    for (i = 0,
        len = sizeof(payload_type_mapping) / sizeof(payload_type_mapping[0]);
         i < len; ++i) {
        if (strlen(de->type) == payload_type_mapping[i].type_name.len &&
            ngx_strncmp(de->type, payload_type_mapping[i].type_name.data,
                        payload_type_mapping[i].type_name.len) == 0) {
            de->payload_type = payload_type_mapping[i].payload_type;
            break;
        }
    }
    if (i < len) {
        de->json = yajl_tree_parse_with_len((const char *) de->out_json.data,
                                            de->out_json.len);
        if (de->json == NULL) {
            ngx_nacos_grpc_decode_payload_destroy(de);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

static void ngx_nacos_grpc_decode_payload_destroy(
    ngx_nacos_grpc_payload_decode_t *de) {
    if (de->json != NULL) {
        yajl_tree_free(de->json);
        de->json = NULL;
    }
    if (de->result != NULL) {
        payload__free_unpacked(de->result, NULL);
        de->result = NULL;
    }
}

static ngx_flag_t ngx_nacos_grpc_payload_is_response_ok(
    enum ngx_nacos_payload_type payload_type, yajl_val root) {
    yajl_val val;
    ngx_flag_t r;
    if (payload_type < ClientDetectionResponse || payload_type >= NONE__END) {
        return 0;
    }
    if (root == NULL) {
        return 0;
    }
    val = yajl_tree_get_field(root, "resultCode", yajl_t_number);
    if (val != NULL && YAJL_GET_INTEGER(val) == 200) {
        r = 1;
    } else {
        r = 0;
    }
    return r;
}

#define NACOS_COMMON_RESPONSE_FMT "{\"resultCode\":200,\"requestId\":\"%s\"}"

static ngx_int_t ngx_nacos_grpc_send_server_push_resp(
    ngx_nacos_grpc_stream_t *st, yajl_val json, const char *resp_type) {
    ngx_nacos_grpc_payload_encode_t en;
    ngx_nacos_grpc_buf_t *buf;
    yajl_val req_id;
    u_char tmp[128];

    if (json == NULL) {
        return NGX_ERROR;
    }
    req_id = yajl_tree_get_field(json, "requestId", yajl_t_string);
    if (req_id == NULL) {
        ngx_log_error(NGX_LOG_WARN, st->conn->conn->log, 0,
                      "nacos server sent request without request id");
        return NGX_ERROR;
    }

    en.type = resp_type;
    en.json.len = ngx_snprintf(tmp, sizeof(tmp), NACOS_COMMON_RESPONSE_FMT,
                               YAJL_GET_STRING(req_id)) -
                  tmp;
    en.json.data = tmp;

    buf = ngx_nacos_grpc_encode_data_msg(st, &en, 0);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    buf->consume_win = en.encoded_len + 5;
    return ngx_nacos_grpc_send_buf(buf, 1);
}

static ngx_int_t ngx_nacos_grpc_notify_address_shm(ngx_nacos_grpc_conn_t *gc,
                                                   yajl_val json) {
    yajl_val s_name, g_name;
    ngx_nacos_resp_json_parser_t parser;
    ngx_nacos_key_t *key;
    ngx_pool_t *pool;
    ngx_int_t rc;
    char *adr;
    u_char tmp[256];
    ngx_nacos_data_t cache;
    size_t len;

    pool = NULL;
    rc = NGX_ERROR;

    if (json == NULL) {
        goto end;
    }
    ngx_memzero(&parser, sizeof(parser));
    parser.json = yajl_tree_get_field(json, "serviceInfo", yajl_t_object);
    if (parser.json == NULL) {
        goto end;
    }
    s_name = yajl_tree_get_field(parser.json, "name", yajl_t_string);
    if (s_name == NULL) {
        goto end;
    }
    g_name = yajl_tree_get_field(parser.json, "groupName", yajl_t_string);
    if (g_name == NULL) {
        goto end;
    }

    len = ngx_snprintf(tmp, sizeof(tmp) - 1, "%s@@%s", YAJL_GET_STRING(g_name),
                       YAJL_GET_STRING(s_name)) -
          tmp;
    tmp[len] = 0;

    key = ngx_nacos_hash_find_key(grpc_ctx.ncf->key_hash, tmp);
    if (key == NULL) {
        ngx_log_error(NGX_LOG_WARN, gc->conn->log, 0,
                      "nacos server sent address with unknown server:%s", tmp);
        goto end;
    }

    pool = ngx_create_pool(512, gc->conn->log);
    if (pool == NULL) {
        goto end;
    }

    parser.pool = pool;
    parser.log = pool->log;
    parser.prev_version = ngx_nacos_shmem_version(key);
    adr = ngx_nacos_parse_addrs_from_json(&parser);
    if (adr == NULL) {
        goto end;
    }
    rc = ngx_nacos_update_shm(grpc_ctx.ncf, key, adr, pool->log);
    if (rc == NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, pool->log, 0,
                      "nacos service %V@@%V is updated!!!", &key->group,
                      &key->data_id);
    }
    cache.pool = pool;
    cache.data_id = key->data_id;
    cache.group = key->group;
    cache.version = parser.current_version;
    cache.adr = adr;
    rc = ngx_nacos_write_disk_data(grpc_ctx.ncf, &cache);

end:
    if (pool != NULL) {
        ngx_destroy_pool(pool);
    }

    return rc;
}

static ngx_int_t ngx_nacos_grpc_config_change_notified(
    ngx_nacos_grpc_conn_t *gc, yajl_val root) {
    yajl_val s_name, g_name, t_name;
    ngx_nacos_key_t *key;
    ngx_int_t rc;
    u_char tmp[512];
    size_t len;

    rc = NGX_ERROR;

    if (root == NULL) {
        goto end;
    }
    s_name = yajl_tree_get_field(root, "dataId", yajl_t_string);
    if (s_name == NULL) {
        goto end;
    }
    g_name = yajl_tree_get_field(root, "group", yajl_t_string);
    if (g_name == NULL) {
        goto end;
    }
    t_name = yajl_tree_get_field(root, "tenant", yajl_t_string);

    if (grpc_ctx.ncf->config_tenant.len > 0 &&
        (ngx_strlen(YAJL_GET_STRING(t_name)) !=
             grpc_ctx.ncf->config_tenant.len ||
         ngx_strcmp(grpc_ctx.ncf->config_tenant.data,
                    YAJL_GET_STRING(t_name)) != 0)) {
        ngx_log_error(NGX_LOG_WARN, gc->conn->log, 0,
                      "nacos server sent config change with unknown tenant:%s",
                      YAJL_GET_STRING(t_name));
        goto end;
    }

    len = ngx_snprintf(tmp, sizeof(tmp) - 1, "%s@@%s", YAJL_GET_STRING(g_name),
                       YAJL_GET_STRING(s_name)) -
          tmp;
    tmp[len] = 0;

    key = ngx_nacos_hash_find_key(grpc_ctx.ncf->config_key_hash, tmp);
    if (key == NULL) {
        ngx_log_error(
            NGX_LOG_WARN, gc->conn->log, 0,
            "nacos server sent config change with unknown data group:%s", tmp);
        goto end;
    }

    rc = ngx_nacos_grpc_send_config_query_request(gc, key);
end:

    return rc;
}

#define CONFIG_QUERY_JSON \
    "{\"dataId\":\"%V\"," \
    "\"group\":\"%V\",\"tenant\":\"%V\"}"

static ngx_int_t ngx_nacos_grpc_send_config_query_request(
    ngx_nacos_grpc_conn_t *gc, ngx_nacos_key_t *key) {
    ngx_str_t req;
    ngx_nacos_main_conf_t *ncf;
    ngx_nacos_grpc_stream_t *st;
    ngx_nacos_grpc_buf_t *hbuf, *bbuf;
    size_t b_len;
    ngx_nacos_grpc_payload_encode_t en;
    static u_char tmp[1024];

    st = NULL;
    hbuf = NULL;
    bbuf = NULL;

    ncf = grpc_ctx.ncf;

    st = ngx_nacos_grpc_create_stream(gc);
    if (st == NULL) {
        goto err;
    }
    st->handler_ctx = key;

    ngx_str_set(&req, "/Request/request");
    hbuf = ngx_nacos_grpc_encode_request(st, &req);
    if (hbuf == NULL) {
        goto err;
    }

    b_len = ngx_snprintf(tmp, sizeof(tmp), CONFIG_QUERY_JSON, &key->data_id,
                         &key->group, &ncf->config_tenant) -
            (u_char *) tmp;

    en.type = "ConfigQueryRequest";
    en.json.len = b_len;
    en.json.data = tmp;
    bbuf = ngx_nacos_grpc_encode_data_msg(st, &en, 1);
    if (bbuf == NULL) {
        goto err;
    }

    hbuf->next = bbuf;
    bbuf->callback = ngx_nacos_grpc_mark_query_config;
    bbuf->consume_win = en.encoded_len + 5;
    return ngx_nacos_grpc_send_buf(hbuf, 1);

err:
    if (bbuf != NULL) {
        ngx_nacos_grpc_free_buf(st, bbuf);
    }
    if (hbuf != NULL) {
        ngx_nacos_grpc_free_buf(st, hbuf);
    }
    if (st != NULL) {
        ngx_nacos_grpc_close_stream(st);
    }
    return NGX_ERROR;
}

static ngx_int_t ngx_nacos_grpc_mark_query_config(ngx_nacos_grpc_stream_t *st,
                                                  ngx_int_t state) {
    if (state == NGX_OK) {
        st->stream_handler = ngx_nacos_grpc_config_query_resp_handler;
    }
    return state;
}

static ngx_int_t ngx_nacos_grpc_config_query_resp_handler(
    ngx_nacos_grpc_stream_t *st, enum ngx_nacos_payload_type type,
    yajl_val json) {
    ngx_int_t rc;

    if (type == ConfigQueryResponse) {
        if (ngx_nacos_grpc_payload_is_response_ok(type, json)) {
            rc = ngx_nacos_grpc_notify_config_shm(st, json);
        } else {
            rc = NGX_ERROR;
        }
        ngx_log_error(NGX_LOG_INFO, st->conn->conn->log, 0,
                      "ConfigQueryResponse received:[%d]", rc);
        rc = NGX_OK;
    } else {
        rc = NGX_ERROR;
    }
    return rc;
}

static ngx_int_t ngx_nacos_grpc_notify_config_shm(ngx_nacos_grpc_stream_t *st,
                                                  yajl_val json) {
    ngx_nacos_key_t *key;
    ngx_nacos_resp_json_parser_t parser;
    ngx_int_t rc;
    char *adr;
    ngx_nacos_data_t cache;

    key = st->handler_ctx;
    rc = NGX_ERROR;

    ngx_memzero(&parser, sizeof(parser));
    if (json == NULL) {
        goto end;
    }

    parser.json = json;
    parser.pool = st->pool;
    parser.log = st->pool->log;
    parser.prev_version = ngx_nacos_shmem_version(key);
    adr = ngx_nacos_parse_config_from_json(&parser);
    if (adr == NULL) {
        ngx_log_error(NGX_LOG_WARN, parser.pool->log, 0,
                      "nacos config %V@@%V is updated failed!!!", &key->group,
                      &key->data_id);
        goto end;
    }
    rc = ngx_nacos_update_shm(grpc_ctx.ncf, key, adr, parser.pool->log);
    if (rc == NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, parser.pool->log, 0,
                      "nacos config %V@@%V is updated!!!", &key->group,
                      &key->data_id);
    }
    cache.pool = st->pool;
    cache.data_id = key->data_id;
    cache.group = key->group;
    cache.version = parser.current_version;
    cache.adr = adr;
    rc = ngx_nacos_write_disk_data(grpc_ctx.ncf, &cache);

end:

    return rc;
}