//
// Created by dear on 22-5-30.
//
#include <backup.pb.h>
#include <ngx_nacos_data.h>
#include <ngx_nacos_http_parse.h>
#include <pb/pb_decode.h>
#include <pb/pb_encode.h>

#define NACOS_SUB_RESP_BUF_SIZE (64 * 1024)

#define NACOS_ADDR_REQ_FMT                                    \
    "GET /nacos/v1/ns/instance/list?serviceName=%V%%40%%40%V" \
    "&namespaceId=%V HTTP/1.0\r\n"                            \
    "Host: %V\r\n"                                            \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n"               \
    "Connection: close\r\n"

#define NACOS_CONFIG_REQ_FMT                    \
    "GET /nacos/v1/cs/configs?&group=%V&"       \
    "dataId=%V&tenant=%V&show=all HTTP/1.0\r\n" \
    "Host: %V\r\n"                              \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n" \
    "Connection: close\r\n"

typedef char *(*parse_from_json_func)(ngx_nacos_resp_json_parser_t *parser);

typedef struct {
    ngx_nacos_data_t *cache;
    parse_from_json_func fn;
} ngx_nacos_sync_fetch_ctx;

static ngx_int_t ngx_nacos_fetch_net_data_processor(
    ngx_nacos_http_parse_t *parser, void *ctx);
static bool ngx_nacos_data_pb_encode_str(pb_ostream_t *stream,
                                         const pb_field_t *field,
                                         void *const *arg);
static ngx_int_t ngx_nacos_fetch_net_data_processor(
    ngx_nacos_http_parse_t *parser, void *ctx) {
    ngx_nacos_resp_json_parser_t resp_parser;
    ngx_nacos_sync_fetch_ctx *fetch_ctx;
    ngx_nacos_data_t *cache;

    fetch_ctx = ctx;
    cache = fetch_ctx->cache;

    if (parser->json_parser != NULL && (resp_parser.json = yajl_tree_finish_get(
                                            parser->json_parser)) != NULL) {
        resp_parser.pool = cache->pool;
        resp_parser.prev_version = 0;
        resp_parser.current_version = 0;
        resp_parser.out_buf_len = 0;
        resp_parser.out_buf = NULL;
        resp_parser.log = cache->pool->log;
        cache->adr = fetch_ctx->fn(&resp_parser);
        if (cache->adr != NULL) {
            cache->version = resp_parser.current_version;
            return NGX_OK;
        }
    } else if (parser->real_body_len == 0 || parser->status == 404) {
        cache->adr = NULL;
        cache->version = 0;
        return NGX_OK;
    }
    return NGX_ERROR;
}

ngx_int_t ngx_nacos_write_disk_data(ngx_nacos_main_conf_t *mcf,
                                    ngx_nacos_data_t *cache) {
    u_char tmp[512];
    ngx_str_t filename;
    ngx_pool_t *pool;
    size_t file_size, fsize;
    ssize_t rd;
    ngx_fd_t fd;
    ngx_err_t err;
    char *c;

    if (cache->adr == NULL) {
        return NGX_DECLINED;
    }

    pool = cache->pool;

    filename.data = tmp;
    filename.len = ngx_snprintf(tmp, sizeof(tmp) - 1, "%V@@%V", &cache->group,
                                &cache->data_id) -
                   tmp;
    if (ngx_get_full_name(pool, &mcf->cache_dir, &filename) != NGX_OK) {
        return NGX_ERROR;
    }

    fd = ngx_open_file(filename.data, NGX_FILE_WRONLY, NGX_FILE_TRUNCATE,
                       NGX_FILE_DEFAULT_ACCESS);
    if (fd == NGX_INVALID_FILE) {
        err = ngx_errno;
        ngx_log_error(NGX_LOG_EMERG, pool->log, err,
                      "nacos %s cache file to write \"%V\" failed",
                      ngx_open_file_n, &filename);
        return NGX_ERROR;
    }

    c = cache->adr;
    fsize = 0;
    file_size = *(size_t *) c;
    do {
        rd = ngx_write_fd(fd, c + fsize, file_size - fsize);
        if (rd == 0) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                          "nacos %s cache file to write \"%V\" EOF",
                          ngx_write_fd_n, &filename);
            goto fail;
        } else if (rd == -1) {
            err = ngx_errno;
            if (err == NGX_EINTR) {
                continue;
            }
            ngx_log_error(NGX_LOG_EMERG, pool->log, err,
                          "nacos %s cache file to write \"%V\" error",
                          ngx_write_fd_n, &filename);
            goto fail;
        }
        fsize += rd;
    } while (fsize < file_size);
    close(fd);
    return NGX_OK;
fail:
    close(fd);
    return NGX_ERROR;
}

ngx_int_t ngx_nacos_fetch_disk_data(ngx_nacos_main_conf_t *mcf,
                                    ngx_nacos_data_t *cache) {
    ngx_fd_t fd;
    ngx_str_t filename;
    ngx_file_info_t fi;
    uint64_t file_size, fsize;
    ngx_err_t err;
    ssize_t rd;
    char *buf;
    u_char tmp[512];
    ngx_pool_t *pool;
    pool = cache->pool;

    filename.data = tmp;
    filename.len = ngx_snprintf(tmp, sizeof(tmp), "%V@@%V", &cache->group,
                                &cache->data_id) -
                   tmp;

    if (ngx_get_full_name(pool, &mcf->cache_dir, &filename) != NGX_OK) {
        return NGX_ERROR;
    }
    if (ngx_file_info(filename.data, &fi) == -1) {
        err = ngx_errno;
        if (err != NGX_ENOPATH) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s \"%V\"",
                          ngx_file_info_n, &filename);
            return NGX_ERROR;
        }
        return NGX_DECLINED;
    }
    file_size = fi.st_size;
    if (file_size > 64 * 1024 * 1024) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                      "nacos cache file \"%V\" too big", &filename);
        return NGX_ERROR;
    }
    if ((fi.st_mode & 0600) != 0600) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                      "nacos cache file \"%V\" no permission", &filename);
        return NGX_ERROR;
    }

    if (file_size <= sizeof(size_t) + sizeof(ngx_uint_t) * 2) {
        return NGX_DECLINED;
    }

    fd = ngx_open_file(filename.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        err = ngx_errno;
        ngx_log_error(NGX_LOG_EMERG, pool->log, err,
                      "nacos %s cache file \"%V\" failed", ngx_open_file_n,
                      &filename);
        return NGX_ERROR;
    }

    buf = ngx_palloc(pool, file_size);
    if (buf == NULL) {
        close(fd);
        return NGX_ERROR;
    }

    fsize = 0;
    do {
        rd = ngx_read_fd(fd, buf + fsize, file_size - fsize);
        if (rd == -1) {
            err = ngx_errno;
            if (err == NGX_EINTR) {
                continue;
            }
            ngx_log_error(NGX_LOG_EMERG, pool->log, err,
                          "nacos %s cache file \"%V\" ERROR", ngx_read_fd_n,
                          &filename);
            goto err_read;
        } else if (rd == 0) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                          "nacos %s cache file \"%V\" EOF", ngx_read_fd_n,
                          &filename);
            goto err_read;
        }
        fsize += rd;
    } while (fsize < file_size);

    fsize = *(uint64_t *) buf;
    if (fsize != file_size) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                      "nacos %s cache file \"%V\" length not match",
                      ngx_read_fd_n, &filename);
        goto err_read;
    }

    cache->version = *(uint64_t *) (buf + sizeof(uint64_t));
    cache->adr = buf;
    return NGX_OK;

err_read:
    ngx_pfree(pool, buf);
    close(fd);
    return NGX_ERROR;
}

ngx_int_t ngx_nacos_fetch_addrs_net_data(ngx_nacos_main_conf_t *mcf,
                                         ngx_nacos_data_t *cache) {
    ngx_str_t req_buf;
    u_char data[512];
    ngx_nacos_sync_fetch_ctx ctx;
    u_char *p;

    req_buf.data = data;
    req_buf.len =
        ngx_snprintf(req_buf.data, NACOS_SUB_RESP_BUF_SIZE - 4,
                     NACOS_ADDR_REQ_FMT, &cache->group, &cache->data_id,
                     &mcf->service_namespace, &mcf->server_host) -
        req_buf.data;

    if (ngx_nacos_http_append_user_pass_header(mcf, &req_buf, sizeof(data)) !=
        NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }

    p = req_buf.data + req_buf.len;
    *p = '\r';
    *(p + 1) = '\n';
    req_buf.len += 2;
    if (req_buf.len >= NACOS_SUB_RESP_BUF_SIZE - 1) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }
    ctx.cache = cache;
    ctx.fn = ngx_nacos_parse_addrs_from_json;
    return ngx_nacos_http_req_json_sync(
        mcf, &req_buf, ngx_nacos_fetch_net_data_processor, &ctx);
}

ngx_int_t ngx_nacos_fetch_config_net_data(ngx_nacos_main_conf_t *mcf,
                                          ngx_nacos_data_t *cache) {
    ngx_str_t req_buf;
    u_char data[512];
    ngx_nacos_sync_fetch_ctx ctx;
    u_char *p;

    req_buf.data = data;
    req_buf.len =
        ngx_snprintf(req_buf.data, NACOS_SUB_RESP_BUF_SIZE - 1,
                     NACOS_CONFIG_REQ_FMT, &cache->group, &cache->data_id,
                     &mcf->config_tenant, &mcf->server_host) -
        req_buf.data;

    if (req_buf.len >= NACOS_SUB_RESP_BUF_SIZE - 1) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }
    if (ngx_nacos_http_append_user_pass_header(mcf, &req_buf, sizeof(data)) !=
        NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }

    p = req_buf.data + req_buf.len;
    *p = '\r';
    *(p + 1) = '\n';
    req_buf.len += 2;
    if (req_buf.len >= NACOS_SUB_RESP_BUF_SIZE - 1) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }
    ctx.cache = cache;
    ctx.fn = ngx_nacos_parse_config_from_json;
    return ngx_nacos_http_req_json_sync(
        mcf, &req_buf, ngx_nacos_fetch_net_data_processor, &ctx);
}

static bool ngx_nacos_data_pb_encode_str(pb_ostream_t *stream,
                                         const pb_field_t *field,
                                         void *const *arg) {
    ngx_str_t *s = *arg;
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }

    if (s == NULL || s->len == 0) {
        return pb_encode_string(stream, NULL, 0);
    }
    return pb_encode_string(stream, s->data, s->len);
}

char *ngx_nacos_parse_config_from_json(ngx_nacos_resp_json_parser_t *parser) {
    yajl_val json, ref;
    ngx_log_t *log;
    char *content, *md5, *result, *c;
    size_t out_len;
    ngx_uint_t md5_len, ct_len;

    json = parser->json;
    log = parser->log;

    ref = yajl_tree_get_field(json, "modifyTime", yajl_t_number);
    if (ref == NULL) {
        ref = yajl_tree_get_field(json, "lastModified", yajl_t_number);
    }
    if (ref == NULL) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "nacos config json not contains valid modifyTime");
        return NULL;
    }
    parser->current_version = (ngx_uint_t) YAJL_GET_INTEGER(ref);
    if (parser->prev_version == parser->current_version) {
        return NULL;
    }

    ref = yajl_tree_get_field(json, "md5", yajl_t_string);
    if (ref == NULL) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "nacos config json not contains valid md5");
        return NULL;
    }
    md5 = YAJL_GET_STRING(ref);

    ref = yajl_tree_get_field(json, "content", yajl_t_string);
    if (ref == NULL) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "nacos config json not contains valid content");
        return NULL;
    }
    content = YAJL_GET_STRING(ref);

    md5_len = ngx_strlen(md5);
    ct_len = ngx_strlen(content);

    out_len = sizeof(size_t) + sizeof(parser->current_version) +
              sizeof(md5_len) + md5_len + sizeof(ct_len) + ct_len;

    if (parser->out_buf != NULL &&
        parser->out_buf_len >= out_len + sizeof(size_t)) {
        result = parser->out_buf;
    } else {
        result = ngx_palloc(parser->pool, out_len + sizeof(size_t));
        if (result == NULL) {
            return NULL;
        }
    }

    c = result;
    *((size_t *) c) = out_len;
    c += sizeof(size_t);

    *((ngx_uint_t *) c) = parser->current_version;
    c += sizeof(ngx_uint_t);

    *((ngx_uint_t *) c) = md5_len;
    c += sizeof(ngx_uint_t);
    memcpy(c, md5, md5_len);
    c += md5_len;

    *((ngx_uint_t *) c) = ct_len;
    c += sizeof(ngx_uint_t);
    memcpy(c, content, ct_len);
    return result;
}

static bool ngx_nacos_encode_hosts(pb_ostream_t *stream,
                                   const pb_field_t *field, void *const *arg) {
    ngx_array_t *addrs = *arg;
    ngx_nacos_service_addr_t *addr;
    ngx_uint_t i, n;
    Instance instance;

    addr = addrs->elts;
    n = addrs->nelts;
    for (i = 0; i < n; i++) {
        if (!pb_encode_tag_for_field(stream, field)) {
            return false;
        }

        instance.host.arg = &addr[i].host;
        instance.host.funcs.encode = ngx_nacos_data_pb_encode_str;
        instance.port = addr->port;
        instance.weight = addr->weight;
        if (!pb_encode_submessage(stream, Instance_fields, &instance)) {
            return false;
        }
    }
    return true;
}

char *ngx_nacos_parse_addrs_from_json(ngx_nacos_resp_json_parser_t *parser) {
    yajl_val json, arr, item, ip, port, ref, weight, enable, healthy;
    size_t i, n;
    int is;
    ngx_log_t *log;
    char *ts, *c;
    Service service;
    ngx_nacos_service_addrs_t addrs;
    ngx_nacos_service_addr_t *addr;
    double w;
    pb_ostream_t buffer;

    json = parser->json;
    log = parser->log;

    ref = yajl_tree_get_field(json, "lastRefTime", yajl_t_number);
    if (!ref) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "nacos response json not contains valid lastRefTime");
        return NULL;
    }
    parser->current_version = (ngx_uint_t) YAJL_GET_INTEGER(ref);
    if (parser->prev_version == parser->current_version) {
        return NULL;
    }

    arr = yajl_tree_get_field(json, "hosts", yajl_t_array);
    if (!arr) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "nacos response json hosts is not array");
        return NULL;
    }

    n = arr->u.array.len;

    if (ngx_array_init(&addrs.addrs, parser->pool, n,
                       sizeof(ngx_nacos_service_addr_t)) != NGX_OK) {
        return NULL;
    }

    for (i = 0; i < n; ++i) {
        item = arr->u.array.values[i];
        if (!YAJL_IS_OBJECT(item)) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts item is not object");
            return NULL;
        }
        enable = yajl_tree_get_field(item, "enabled", yajl_t_true);
        healthy = yajl_tree_get_field(item, "healthy", yajl_t_true);
        if (!enable || !healthy) {
            continue;
        }

        ip = yajl_tree_get_field(item, "ip", yajl_t_string);
        if (!ip) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts ip is not string");
            return NULL;
        }
        port = yajl_tree_get_field(item, "port", yajl_t_number);
        if (!port) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts port is not number");
            return NULL;
        }
        ts = YAJL_GET_STRING(ip);
        is = (int) YAJL_GET_INTEGER(port);

        weight = yajl_tree_get_field(item, "weight", yajl_t_number);
        if (!weight) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts weight is not number");
            return NULL;
        }
        w = YAJL_GET_DOUBLE(weight);

        if (is <= 0 || is > 65535) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts port is invalid: %d", is);
            return NULL;
        }
        addr = ngx_array_push(&addrs.addrs);
        if (addr == NULL) {
            return NULL;
        }
        addr->host.data = (u_char *) ts;
        addr->host.len = strlen(ts);
        addr->port = is;
        addr->weight = (int32_t) w * 100;
    }

    addrs.version = parser->current_version;
    service.version = addrs.version;
    service.instances.arg = &addrs.addrs;
    service.instances.funcs.encode = ngx_nacos_encode_hosts;

    if (!pb_get_encoded_size(&n, Service_fields, &service)) {
        return NULL;
    }
    c = ngx_palloc(parser->pool, n + sizeof(uint64_t) * 2);
    if (c == NULL) {
        return NULL;
    }

    buffer = pb_ostream_from_buffer((u_char *) c + sizeof(uint64_t) * 2, n);
    if (!pb_encode(&buffer, Service_fields, &service) ||
        buffer.bytes_written != n) {
        return NULL;
    }

    *((uint64_t *) c) = n + sizeof(uint64_t) * 2;
    *((uint64_t *) (c + sizeof(uint64_t))) = addrs.version;
    return c;
}

struct ngx_pb_decode_ctx_t {
    void *arg;
    ngx_pool_t *pool;
};

static bool ngx_nacos_pb_decode_str(pb_istream_t *stream,
                                    const pb_field_t *field, void **arg) {
    struct ngx_pb_decode_ctx_t *ctx;
    ngx_str_t *t;

    ctx = *arg;
    t = ctx->arg;
    t->len = stream->bytes_left;
    t->data = ngx_palloc(ctx->pool, t->len);
    if (t->data == NULL) {
        return false;
    }
    ngx_memcpy(t->data, stream->state, t->len);
    stream->state = (char *) stream->state + t->len;
    stream->bytes_left -= t->len;
    return true;
}

static bool ngx_nacos_decode_instances(pb_istream_t *stream,
                                       const pb_field_t *field, void **arg) {
    struct ngx_pb_decode_ctx_t *ctx;
    ngx_array_t *addrs;
    ngx_nacos_service_addr_t *addr;
    Instance instance;

    ctx = *arg;
    addrs = ctx->arg;
    instance.host.arg = ctx;
    instance.host.funcs.decode = ngx_nacos_pb_decode_str;

    addr = ngx_array_push(addrs);
    if (addr == NULL) {
        return false;
    }
    instance.port = 0;
    instance.weight = 0;
    ctx->arg = &addr->host;
    if (!pb_decode(stream, Instance_fields, &instance)) {
        return false;
    }
    addr->weight = instance.weight;
    addr->port = instance.port;
    ctx->arg = addrs;
    return true;
}

ngx_int_t ngx_nacos_deep_copy_addrs(char *src,
                                    ngx_nacos_service_addrs_t *dist) {
    uint64_t len, version;
    pb_istream_t buffer;
    Service service;
    struct ngx_pb_decode_ctx_t ctx;
    ctx.pool = dist->addrs.pool;
    ctx.arg = &dist->addrs;

    len = *(uint64_t *) src;
    version = *(uint64_t *) (src + sizeof(uint64_t));

    service.version = 0;
    service.instances.arg = &ctx;
    service.instances.funcs.decode = ngx_nacos_decode_instances;

    buffer = pb_istream_from_buffer((u_char *) src + 2 * sizeof(uint64_t),
                                    len - 2 * sizeof(uint64_t));
    if (!pb_decode(&buffer, Service_fields, &service)) {
        ngx_log_error(NGX_LOG_WARN, dist->addrs.pool->log, 0,
                      "nacos decode service error");
        return NGX_ERROR;
    }
    if (version != service.version) {
        ngx_log_error(NGX_LOG_WARN, dist->addrs.pool->log, 0,
                      "nacos decode service version not match");
        return NGX_ERROR;
    }

    return NGX_OK;
}

ngx_int_t ngx_nacos_update_shm(ngx_nacos_main_conf_t *mcf, ngx_nacos_key_t *key,
                               const char *adr, ngx_log_t *log) {
    uint64_t len;
    uint64_t version;
    char *oldAddr, *nAddr;

    len = *(uint64_t *) adr;
    version = *(uint64_t *) (adr + sizeof(size_t));

    if (mcf->sh == NULL) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "nacos no shared mem  %V@@%V",
                      &key->group, key->data_id);
        return NGX_ERROR;
    }

    nAddr = ngx_slab_alloc(mcf->sh, len);
    if (nAddr == NULL) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "nacos no shared mem to available %V@@%V", &key->group,
                      key->data_id);
        return NGX_ERROR;
    }
    memcpy(nAddr, adr, len);

    ngx_rwlock_wlock(&key->ctx->wrlock);
    oldAddr = key->ctx->data;
    key->ctx->version = version;
    key->ctx->data = nAddr;
    ngx_rwlock_unlock(&key->ctx->wrlock);
    ngx_slab_free(mcf->sh, oldAddr);
    return NGX_OK;
}

ngx_nacos_key_t *ngx_nacos_hash_find_key(ngx_hash_t *key_hash, u_char *k) {
    u_char *p, c;
    ngx_flag_t valid;
    ngx_uint_t hash;

    valid = 0;
    hash = 0;
    for (p = k; (c = *p); ++p) {
        if (c == '@' && *(p + 1) == '@') {
            valid = 1;
        }
        *p = c = ngx_tolower(c);
        hash = ngx_hash(hash, c);
    }
    if (!valid) {
        return NULL;
    }

    return ngx_hash_find(key_hash, hash, (u_char *) k, p - k);
}

ngx_int_t ngx_nacos_get_config_md5(ngx_nacos_key_t *key, ngx_str_t *buf) {
    ngx_nacos_key_ctx_t *ctx;
    char *data;
    ngx_uint_t md5_len;
    ngx_int_t rc;
    size_t len;
    // length version md5_len md5
    size_t md5_len_off = sizeof(size_t) + sizeof(ngx_uint_t);
    size_t md5_off = sizeof(size_t) + sizeof(ngx_uint_t) + sizeof(ngx_uint_t);

    ctx = key->ctx;
    rc = NGX_OK;
    if (key->use_shared) {
        ngx_rwlock_rlock(&ctx->wrlock);
    }
    data = ctx->data;
    if (data != NULL) {
        len = *(size_t *) data;
        md5_len = *(ngx_uint_t *) (data + md5_len_off);

        if (md5_off + md5_len >= len || md5_len >= buf->len) {
            rc = NGX_ERROR;
            goto unlock;
        }
        memcpy(buf->data, data + md5_off, md5_len);
        buf->len = md5_len;
    } else {
        buf->len = 0;
    }
unlock:
    if (key->use_shared) {
        ngx_rwlock_unlock(&ctx->wrlock);
    }

    return rc;
}
