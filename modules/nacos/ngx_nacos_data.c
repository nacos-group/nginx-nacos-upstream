//
// Created by dear on 22-5-30.
//
#include <ngx_nacos_data.h>
#include <ngx_nacos_http_parse.h>

#define NACOS_SUB_RESP_BUF_SIZE  (64 * 1024)

#define NACOS_REQ_FMT "GET /nacos/v1/ns/instance/list?serviceName=%V%%40%%40%V" \
    "&udpPort=%V&clientIP=%V HTTP/1.0\r\n"                                      \
    "Host: %V\r\n"                                                              \
    "User-Agent: Nacos-Java-Client:v2.10.0\r\n"                                 \
    "Connection: close\r\n\r\n"


ngx_int_t ngx_nacos_write_disk_data(ngx_nacos_main_conf_t *mcf, ngx_nacos_data_t *cache) {
    u_char tmp[512];
    ngx_str_t filename;
    ngx_pool_t *pool;
    size_t file_size, fsize;
    ssize_t rd;
    ngx_fd_t fd;
    ngx_err_t err;
    char *c;

    pool = cache->pool;

    filename.data = tmp;
    filename.len = ngx_snprintf(tmp, sizeof(tmp) - 1, "%V@@%V", &cache->group, &cache->data_id) - tmp;
    if (ngx_get_full_name(pool, &mcf->cache_dir, &filename) != NGX_OK) {
        return NGX_ERROR;
    }

    fd = ngx_open_file(filename.data, NGX_FILE_WRONLY, NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);
    if (fd == NGX_INVALID_FILE) {
        err = ngx_errno;
        ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s cache file to write \"%V\" failed", ngx_open_file_n,
                      &filename);
        return NGX_ERROR;
    }

    c = cache->adr;
    fsize = 0;
    file_size = *(size_t *) c;
    do {
        rd = ngx_write_fd(fd, c + fsize, file_size - fsize);
        if (rd == 0) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0, "nacos %s cache file to write \"%V\" EOF", ngx_write_fd_n,
                          &filename);
            goto fail;
        } else if (rd == -1) {
            err = ngx_errno;
            if (err == NGX_EINTR) {
                continue;
            }
            ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s cache file to write \"%V\" error", ngx_write_fd_n,
                          &filename);
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

ngx_int_t ngx_nacos_fetch_disk_data(ngx_nacos_main_conf_t *mcf, ngx_nacos_data_t *cache) {
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
    filename.len = ngx_snprintf(tmp, sizeof(tmp) - 1, "%V@@%V", &cache->group, &cache->data_id) - tmp;

    if (ngx_get_full_name(pool, &mcf->cache_dir, &filename) != NGX_OK) {
        return NGX_ERROR;
    }
    if (ngx_file_info(filename.data, &fi) == -1) {
        err = ngx_errno;
        if (err != NGX_ENOPATH) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s \"%V\"", ngx_file_info_n, &filename);
            return NGX_ERROR;
        }
        return NGX_DECLINED;
    }
    file_size = fi.st_size;
    if (file_size > 64 * 1024) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0, "nacos cache file \"%V\" too big", &filename);
        return NGX_ERROR;
    }
    if ((fi.st_mode & 0600) != 0600) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0, "nacos cache file \"%V\" no permission", &filename);
        return NGX_ERROR;
    }

    if (file_size <= sizeof(size_t) + sizeof(ngx_uint_t) * 2) {
        return NGX_DECLINED;
    }

    fd = ngx_open_file(filename.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        err = ngx_errno;
        ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s cache file \"%V\" failed", ngx_open_file_n,
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
            ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s cache file \"%V\" ERROR", ngx_read_fd_n,
                          &filename);
            goto err_read;
        } else if (rd == 0) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s cache file \"%V\" EOF", ngx_read_fd_n,
                          &filename);
            goto err_read;
        }
        fsize += rd;
    } while (fsize < file_size);

    fsize = *(size_t *) buf;
    if (fsize != file_size) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, err, "nacos %s cache file \"%V\" length not match", ngx_read_fd_n,
                      &filename);
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


ngx_int_t ngx_nacos_fetch_net_data(ngx_nacos_main_conf_t *mcf, ngx_nacos_data_t *cache) {
    ngx_uint_t tries;
    char *tbuf;
    ngx_addr_t *addrs;
    ngx_fd_t s;
    ngx_pool_t *pool;
    pool = cache->pool;
    ngx_err_t err;
    ngx_nacos_http_parse_t parser;
    ngx_nacos_addr_resp_parser_t resp_parser;
    ngx_uint_t i, n;
    size_t req_len, sd;
    ssize_t rd;

    addrs = mcf->server_list.elts;

    tbuf = NULL;
    tries = 0;
    s = -1;

    tbuf = ngx_alloc(NACOS_SUB_RESP_BUF_SIZE, pool->log);
    if (tbuf == NULL) {
        goto fetch_failed;
    }
    memset(&parser, 0, sizeof(parser));

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
        ngx_log_error(NGX_LOG_WARN, pool->log, err, "nacos connect() to %V failed",
                      &addrs[i].name);
        goto retry;
    }


    req_len = ngx_snprintf((u_char *) tbuf, NACOS_SUB_RESP_BUF_SIZE - 1, NACOS_REQ_FMT,
                           &cache->group,
                           &cache->data_id,
                           &mcf->udp_port,
                           &mcf->udp_ip,
                           &addrs[i].name) - (u_char *) tbuf;

    sd = 0;
    do {
        rd = ngx_write_fd(s, tbuf + sd, req_len - sd);
        if (rd > 0) {
            sd += rd;
        } else if (rd == 0) {
            ngx_log_error(NGX_LOG_WARN, pool->log, 0, "write request to %V failed, because of EOF occur",
                          &addrs[i].name);
            goto retry;
        } else {
            err = ngx_socket_errno;
            if (err == NGX_EINTR) {
                continue;
            }
            ngx_log_error(NGX_LOG_WARN, pool->log, err, "write request to %V failed",
                          &addrs[i].name);
            goto retry;
        }
    } while ((ngx_uint_t) sd < req_len);

    memset(&parser, 0, sizeof(parser));
    parser.buf = (u_char *) tbuf;
    parser.log = pool->log;

    do {
        rd = ngx_read_fd(s, tbuf + parser.limit, NACOS_SUB_RESP_BUF_SIZE - parser.limit);
        if (rd >= 0) {
            parser.limit += rd;
            parser.conn_eof = rd == 0;
            rd = ngx_nacos_http_parse(&parser);
            if (rd == NGX_ERROR) {
                goto fetch_failed;
            } else if (rd == NGX_OK) {
                goto fetch_success;
            } else {// ngx_decline
                if (parser.conn_eof) {
                    // close premature
                    goto retry;
                }
                rd = 1;
            }
        } else if (rd == -1) {
            err = ngx_socket_errno;
            ngx_log_error(NGX_LOG_WARN, pool->log, err, "read response from %V failed",
                          &addrs[i].name);
            goto retry;
        }
    } while (rd);

    fetch_success:
    resp_parser.pool = cache->pool;
    resp_parser.prev_version = 0;
    resp_parser.current_version = 0;
    resp_parser.out_buf_len = 0;
    resp_parser.out_buf = NULL;
    resp_parser.log = cache->pool->log;
    resp_parser.json = yajl_tree_finish_get(parser.json_parser);
    if (resp_parser.json == NULL) {
        goto fetch_failed;
    }
    if ((cache->adr = ngx_nacos_parse_addrs_from_json(&resp_parser)) != NULL) {
        cache->version = resp_parser.current_version;
        rd = NGX_OK;
        goto free;
    }

    fetch_failed:
    ngx_log_error(NGX_LOG_WARN, pool->log, 0, "fetch data from nacos servers failed");
    rd = NGX_ERROR;

    free:
    if (s > 0) {
        close(s);
    }
    if (parser.json_parser != NULL) {
        yajl_tree_free_parser(parser.json_parser);
        parser.json_parser = NULL;
    }
    if (tbuf) {
        ngx_free(tbuf);
    }
    return rd;
}

char *ngx_nacos_parse_addrs_from_json(ngx_nacos_addr_resp_parser_t *parser) {
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
        ngx_log_error(NGX_LOG_WARN, log, 0, "nacos response json not contains valid lastRefTime");
        return NULL;
    }
    parser->current_version = (ngx_uint_t) YAJL_GET_INTEGER(ref);
    if (parser->prev_version == parser->current_version) {
        return NULL;
    }

    arr = yajl_tree_get_field(json, "hosts", yajl_t_array);
    if (!arr) {
        ngx_log_error(NGX_LOG_WARN, log, 0, "nacos response json hosts is not array");
        return NULL;
    }

    n = arr->u.array.len;
    c = buf + sizeof(size_t) + sizeof(ngx_uint_t) * 2;
    m = 0;
    for (i = 0; i < n; ++i) {
        item = arr->u.array.values[i];
        if (!YAJL_IS_OBJECT(item)) {
            ngx_log_error(NGX_LOG_WARN, log, 0, "nacos response json hosts item is not object");
            return NULL;
        }
        ip = yajl_tree_get_field(item, "ip", yajl_t_string);
        if (!ip) {
            ngx_log_error(NGX_LOG_WARN, log, 0, "nacos response json hosts ip is not string");
            return NULL;
        }
        port = yajl_tree_get_field(item, "port", yajl_t_number);
        if (!port) {
            ngx_log_error(NGX_LOG_WARN, log, 0, "nacos response json hosts port is not number");
            return NULL;
        }
        ts = YAJL_GET_STRING(ip);
        is = (int) YAJL_GET_INTEGER(port);

        if (is <= 0 || is > 65535) {
            ngx_log_error(NGX_LOG_WARN, log, 0, "nacos response json hosts port is invalid: %d", is);
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
    *(size_t *) c = n;// byte len
    c += sizeof(size_t);
    *(ngx_uint_t *) c = parser->current_version;// addr num
    c += sizeof(ngx_uint_t);
    *(ngx_uint_t *) c = m;// addr num
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
    c = src;
    c += sizeof(n) + sizeof(ngx_uint_t);// bytes len + version len
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

ngx_int_t ngx_nacos_update_addrs(ngx_nacos_key_t *key, const char *adr, ngx_log_t *log) {
    size_t len;
    ngx_uint_t version;
    char *oldAddr, *nAddr;

    len = *(size_t *) adr;
    version = *(ngx_uint_t *) (adr + sizeof(size_t));

    if (key->sh == NULL) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "nacos no shared mem  %V@@%V", &key->group,
                      key->data_id);
        return NGX_ERROR;
    }

    nAddr = ngx_slab_alloc(key->sh, len);
    if (nAddr == NULL) {
        ngx_log_error(NGX_LOG_WARN, log, 0, "nacos no shared mem to available %V@@%V", &key->group,
                      key->data_id);
        return NGX_ERROR;
    }
    memcpy(nAddr, adr, len);

    ngx_rwlock_wlock(&key->ctx->wrlock);
    oldAddr = key->ctx->addrs;
    key->ctx->version = version;
    key->ctx->addrs = nAddr;
    ngx_rwlock_unlock(&key->ctx->wrlock);
    ngx_slab_free(key->sh, oldAddr);
    return NGX_OK;
}
