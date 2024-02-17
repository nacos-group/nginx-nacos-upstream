//
// Created by dear on 22-5-30.
//
#include <ngx_nacos_data.h>
#include <ngx_nacos_http_parse.h>

#define NACOS_SUB_RESP_BUF_SIZE (64 * 1024)

#define NACOS_ADDR_REQ_FMT                                    \
    "GET /nacos/v1/ns/instance/list?serviceName=%V%%40%%40%V" \
    "&udpPort=%V&clientIP=%V&namespaceId=%V HTTP/1.0\r\n"     \
    "Host: %V\r\n"                                            \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n"               \
    "Connection: close\r\n\r\n"

#define NACOS_CONFIG_REQ_FMT                    \
    "GET /nacos/v1/cs/configs?&group=%V&"       \
    "dataId=%V&tenant=%V&show=all HTTP/1.0\r\n"          \
    "Host: %V\r\n"                              \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n" \
    "Connection: close\r\n\r\n"

typedef char *(*parse_from_json_func)(ngx_nacos_resp_json_parser_t *parser);

static ngx_int_t ngx_nacos_fetch_net_data(ngx_nacos_main_conf_t *mcf,
                                          ngx_nacos_data_t *cache,
                                          ngx_str_t *req,
                                          parse_from_json_func fn);

static ngx_int_t ngx_nacos_fetch_net_data(ngx_nacos_main_conf_t *mcf,
                                          ngx_nacos_data_t *cache,
                                          ngx_str_t *req,
                                          parse_from_json_func fn) {
    ngx_uint_t tries;
    ngx_addr_t *addrs;
    ngx_fd_t s;
    ngx_pool_t *pool;
    ngx_err_t err;
    ngx_nacos_http_parse_t parser;
    ngx_nacos_resp_json_parser_t resp_parser;
    ngx_uint_t i, n;
    size_t sd;
    ssize_t rd;

    pool = cache->pool;
    addrs = mcf->server_list.elts;

    tries = 0;
    s = -1;

    ngx_memzero(&parser, sizeof(parser));
    parser.log = pool->log;
    parser.buf = (u_char *) ngx_alloc(NACOS_SUB_RESP_BUF_SIZE, pool->log);
    if (parser.buf == NULL) {
        goto fetch_failed;
    }

retry:
    if (++tries > mcf->server_list.nelts) {
        goto fetch_failed;
    }

    n = mcf->server_list.nelts;
    i = (mcf->cur_srv_index++) % n;
    if (mcf->cur_srv_index >= n) {
        mcf->cur_srv_index = 0;
    }

    if (parser.json_parser != NULL) {
        yajl_tree_free_parser(parser.json_parser);
        parser.json_parser = NULL;
    }
    if (s > 0) {
        close(s);
    }
    s = ngx_socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        return NGX_ERROR;
    }

    if (connect(s, addrs[i].sockaddr, addrs[i].socklen) != 0) {
        err = ngx_socket_errno;
        ngx_log_error(NGX_LOG_WARN, pool->log, err,
                      "nacos connect() to %V failed", &addrs[i].name);
        goto retry;
    }

    sd = 0;
    do {
        rd = ngx_write_fd(s, req->data + sd, req->len - sd);
        if (rd > 0) {
            sd += rd;
        } else if (rd == 0) {
            ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                          "write request to %V failed, because of EOF occur",
                          &addrs[i].name);
            goto retry;
        } else {
            err = ngx_socket_errno;
            if (err == NGX_EINTR) {
                continue;
            }
            ngx_log_error(NGX_LOG_WARN, pool->log, err,
                          "write request to %V failed", &addrs[i].name);
            goto retry;
        }
    } while ((ngx_uint_t) sd < req->len);

    do {
        rd = ngx_read_fd(s, parser.buf + parser.limit,
                         NACOS_SUB_RESP_BUF_SIZE - parser.limit);
        if (rd >= 0) {
            parser.limit += rd;
            parser.conn_eof = rd == 0;
            rd = ngx_nacos_http_parse(&parser);
            if (rd == NGX_ERROR) {
                goto fetch_failed;
            } else if (rd == NGX_OK) {
                goto fetch_success;
            } else {  // ngx_decline
                if (parser.conn_eof) {
                    // close premature
                    goto retry;
                }
                rd = 1;
            }
        } else if (rd == -1) {
            err = ngx_socket_errno;
            ngx_log_error(NGX_LOG_WARN, pool->log, err,
                          "read response from %V failed", &addrs[i].name);
            goto retry;
        }
    } while (rd);

fetch_success:

    if (parser.json_parser != NULL &&
        (resp_parser.json = yajl_tree_finish_get(parser.json_parser)) != NULL) {
        resp_parser.pool = cache->pool;
        resp_parser.prev_version = 0;
        resp_parser.current_version = 0;
        resp_parser.out_buf_len = 0;
        resp_parser.out_buf = NULL;
        resp_parser.log = cache->pool->log;
        cache->adr = fn(&resp_parser);
        if (cache->adr != NULL) {
            cache->version = resp_parser.current_version;
            rd = NGX_OK;
            goto free;
        }
    } else if (parser.real_body_len == 0) {
        cache->adr = NULL;
        cache->version = 0;
        rd = NGX_OK;
        goto free;
    }

fetch_failed:
    ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                  "fetch config data from nacos server failed");
    rd = NGX_ERROR;

free:
    if (s > 0) {
        close(s);
    }
    if (parser.json_parser != NULL) {
        yajl_tree_free_parser(parser.json_parser);
        parser.json_parser = NULL;
    }
    if (parser.buf != NULL) {
        ngx_free(parser.buf);
    }
    return rd;
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
    size_t file_size, fsize;
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

    fsize = *(size_t *) buf;
    if (fsize != file_size) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                      "nacos %s cache file \"%V\" length not match",
                      ngx_read_fd_n, &filename);
        goto err_read;
    }

    cache->version = *(ngx_uint_t *) (buf + sizeof(size_t));
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

    req_buf.data = data;
    req_buf.len = ngx_snprintf(req_buf.data, NACOS_SUB_RESP_BUF_SIZE - 1,
                               NACOS_ADDR_REQ_FMT, &cache->group,
                               &cache->data_id, &mcf->udp_port, &mcf->udp_ip,
                               &mcf->tenant_namespace, &mcf->server_host) -
                  req_buf.data;

    if (req_buf.len >= NACOS_SUB_RESP_BUF_SIZE - 1) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }
    return ngx_nacos_fetch_net_data(mcf, cache, &req_buf,
                                    ngx_nacos_parse_addrs_from_json);
}

ngx_int_t ngx_nacos_fetch_config_net_data(ngx_nacos_main_conf_t *mcf,
                                          ngx_nacos_data_t *cache) {
    ngx_str_t req_buf;
    u_char data[512];

    req_buf.data = data;
    req_buf.len =
        ngx_snprintf(req_buf.data, NACOS_SUB_RESP_BUF_SIZE - 1,
                     NACOS_CONFIG_REQ_FMT, &cache->group, &cache->data_id,
                     &mcf->tenant_namespace, &mcf->server_host) -
        req_buf.data;

    if (req_buf.len >= NACOS_SUB_RESP_BUF_SIZE - 1) {
        ngx_log_error(NGX_LOG_EMERG, cache->pool->log, 0,
                      "nacos dataId or group is to long: GROUP|DATA_ID=%V|%V",
                      &cache->group, &cache->data_id);
        return NGX_ERROR;
    }
    req_buf.data[req_buf.len] = 0;
    return ngx_nacos_fetch_net_data(mcf, cache, &req_buf,
                                    ngx_nacos_parse_config_from_json);
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

char *ngx_nacos_parse_addrs_from_json(ngx_nacos_resp_json_parser_t *parser) {
    yajl_val json, arr, item, ip, port, ref;
    size_t i, n;
    int is;
    ngx_uint_t j, m;
    ngx_url_t u;
    ngx_log_t *log;
    char *ts, *c;
    static char buf[65536];

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
    c = buf + sizeof(size_t) + sizeof(ngx_uint_t) * 2;
    m = 0;
    for (i = 0; i < n; ++i) {
        item = arr->u.array.values[i];
        if (!YAJL_IS_OBJECT(item)) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts item is not object");
            return NULL;
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

        if (is <= 0 || is > 65535) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "nacos response json hosts port is invalid: %d", is);
            return NULL;
        }

        memset(&u, 0, sizeof(u));
        u.url.len = strlen(ts);
        u.url.data = (u_char *) ts;
        u.default_port = is;
        if (ngx_parse_url(parser->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_log_error(NGX_LOG_EMERG, log, 0,
                              "%s in nacos server_list \"%V\"", u.err, &u.url);
            }
            return NULL;
        }
        for (j = 0; j < u.naddrs; ++j) {
            *(ngx_uint_t *) c = u.addrs[j].name.len;
            c += sizeof(ngx_uint_t);
            memcpy(c, u.addrs[j].name.data, u.addrs[j].name.len);
            c += u.addrs[j].name.len;
            *(ngx_uint_t *) c = u.addrs[j].socklen;
            c += sizeof(ngx_uint_t);
            memcpy(c, u.addrs[j].sockaddr, u.addrs[j].socklen);
            c += u.addrs[j].socklen;
        }
        m += u.naddrs;
    }

    n = (int) (c - (char *) buf);
    c = buf;
    *(size_t *) c = n;  // byte len
    c += sizeof(size_t);
    *(ngx_uint_t *) c = parser->current_version;  // addr num
    c += sizeof(ngx_uint_t);
    *(ngx_uint_t *) c = m;  // addr num
    if (parser->out_buf != NULL && parser->out_buf_len >= n) {
        c = parser->out_buf;
    } else {
        c = ngx_palloc(parser->pool, n);
        if (c == NULL) {
            return NULL;
        }
    }

    memcpy(c, buf, n);
    return c;
}

ngx_int_t ngx_nacos_deep_copy_addrs(char *src, ngx_array_t *dist) {
    ngx_uint_t i, n;
    char *c;
    ngx_addr_t *v;

    if (src == NULL) {
        return NGX_DECLINED;
    }

    c = src;
    c += sizeof(n) + sizeof(ngx_uint_t);  // bytes len + version len
    n = *(ngx_uint_t *) c;
    c += sizeof(n);
    for (i = 0; i < n; ++i) {
        v = ngx_array_push(dist);
        if (v == NULL) {
            return NGX_ERROR;
        }
        v->name.len = *(ngx_uint_t *) c;
        c += sizeof(n);
        v->name.data = ngx_palloc(dist->pool, v->name.len);
        if (v->name.data == NULL) {
            return NGX_ERROR;
        }
        memcpy(v->name.data, c, v->name.len);
        c += v->name.len;

        v->socklen = *(ngx_uint_t *) c;
        c += sizeof(n);
        v->sockaddr = ngx_palloc(dist->pool, v->socklen);
        if (v->sockaddr == NULL) {
            return NGX_ERROR;
        }
        memcpy(v->sockaddr, c, v->socklen);
        c += v->socklen;
    }
    return NGX_OK;
}

ngx_int_t ngx_nacos_update_shm(ngx_nacos_main_conf_t *mcf, ngx_nacos_key_t *key,
                               const char *adr, ngx_log_t *log) {
    size_t len;
    ngx_uint_t version;
    char *oldAddr, *nAddr;

    len = *(size_t *) adr;
    version = *(ngx_uint_t *) (adr + sizeof(size_t));

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
