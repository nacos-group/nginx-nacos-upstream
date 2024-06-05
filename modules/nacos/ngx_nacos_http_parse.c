//
// Created by dear on 22-6-11.
//

#include <ngx_nacos_http_parse.h>
#include <string.h>

static u_char *n_strchr(u_char *s, const u_char *e, int c) {
    /* find first occurrence of c in char s[] */
    const u_char ch = c;

    for (; s < e && *s != ch; ++s)
        ;

    return s == e ? NULL : s;
}

static u_char *n_strnstr(u_char *s1, u_char *e, const char *s2) {
    /* find first occurrence of s2[] in s1[] */
    const u_char *sc1;
    const char *sc2;
    if (*s2 == '\0') return s1;
    for (; (s1 = n_strchr(s1, e, *s2)) != NULL;
         ++s1) { /* match rest of prefix */
        for (sc1 = s1, sc2 = s2;;)
            if (*++sc2 == '\0')
                return s1;
            else if (++sc1 == e)
                return NULL;
            else if (*sc1 != *sc2)
                break;
    }
    return (NULL);
}

static u_char *n_hex2int(u_char *s, ngx_int_t *out_n) {
    ngx_int_t n = 0, i = 0;
    u_char c;

    for (;; ++i) {
        c = s[i];
        if (c >= '0' && c <= '9') {
            n = (n << 4) + c - '0';
            continue;
        }
        c |= 0x20;
        if (c >= 'a' && c <= 'f') {
            n = (n << 4) + c - 'a' + 10;
            continue;
        }
        break;
    }
    if (i) {
        *out_n = n;
        return &s[i];
    }
    return NULL;
}

ngx_int_t ngx_nacos_http_parse(ngx_nacos_http_parse_t *parse) {
    u_char *c, *e, *t, *ch;
    size_t len;
    yajl_status j_status;

    c = parse->buf + parse->offset;
    e = parse->buf + parse->limit;

    if (parse->parse_state == line) {
        if (parse->limit - parse->offset <
            14) {  // HTTP/1.1 200 <status_text>\r\n
            goto move_again;
        }
        t = n_strnstr(c, e, "\r\n");
        if (t == NULL) {
            goto move_again;
        }
        if (ngx_strncmp(c, "HTTP/", 5) != 0) {
            ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                          "parse nacos resp. protocol error in version");
            return NGX_ERROR;
        }
        c += 5;
        if (ngx_strncmp(c, "1.1", 3) == 0) {
            parse->http_version = v_11;
        } else if (ngx_strncmp(c, "1.0", 3) == 0) {
            parse->http_version = v_10;
        } else if (ngx_strncmp(c, "0.9", 3) == 0) {
            parse->http_version = v_09;
        } else {
            ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                          "parse nacos resp. protocol error in version");
            return NGX_ERROR;
        }

        c += 3;
        while (*c == ' ') {
            ++c;
        }
        parse->status = ngx_atoi((u_char *) c, 3);
        if (parse->status == NGX_ERROR) {
            ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                          "parse nacos resp. protocol error in status");
            return NGX_ERROR;
        }
        if (parse->status < 100 || parse->status > 900) {
            ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                          "parse nacos resp. status invalid:%d", parse->status);
            return NGX_ERROR;
        }
        c = t + 2;
        parse->offset = c - parse->buf;
        parse->parse_state = head;
    }

    while (parse->parse_state == head) {
        t = n_strnstr(c, e, "\r\n");
        if (t == NULL) {
            goto move_again;
        }
        if (t == c) {
            if (parse->close_conn == 0) {
                parse->close_conn = parse->http_version == v_11 ? -1 : 1;
            }
            if (parse->body_type == none) {
                if (parse->close_conn == 1) {
                    parse->real_body_len = 0;
                    parse->body_type = oef;
                } else {
                    ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                                  "parse nacos resp. unknown body length");
                    return NGX_ERROR;
                }
            } else if (parse->body_type == chunk) {
                parse->chunk_size = -2;  // init -2; \r\n
            }
            c += 2;
            parse->offset = c - parse->buf;
            parse->parse_state = body;
            if (parse->json_body == 1 && parse->status == 200) {
                parse->json_parser = yajl_tree_alloc_parser();
                if (parse->json_parser == NULL) {
                    return NGX_ERROR;
                }
            }

            break;
        }

        if (parse->json_body == 0 &&
            ngx_strncasecmp(c, (u_char *) "Content-Type:", 13) == 0) {
            c += 13;
            while (*c == ' ') {
                ++c;
            }
            if (ngx_strncasecmp((u_char *) c, (u_char *) "application/json",
                                16) == 0) {
                parse->json_body = 1;
            } else {
                parse->json_body = -1;
            }
        } else if (parse->body_type == none &&
                   ngx_strncasecmp((u_char *) c,
                                   (u_char *) "Transfer-Encoding:", 18) == 0) {
            c += 18;
            while (*c == ' ') {
                ++c;
            }
            if (ngx_strncasecmp((u_char *) c, (u_char *) "chunked", 7) == 0) {
                parse->body_type = chunk;
                parse->real_body_len = 0;
            } else {
                ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                              "parse nacos resp. unknown Transfer-Encoding");
                return NGX_ERROR;
            }
        } else if (parse->body_type == none &&
                   ngx_strncasecmp((u_char *) c,
                                   (u_char *) "Content-Length:", 15) == 0) {
            c += 15;
            while (*c == ' ') {
                ++c;
            }

            parse->content_len = ngx_atoi((u_char *) c, t - c);
            if (parse->content_len == NGX_ERROR) {
                ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                              "parse nacos resp. unknown Content-Length");
                return NGX_ERROR;
            }
            parse->real_body_len = parse->content_len;
            parse->body_type = cont_len;
        } else if (parse->close_conn == 0 &&
                   ngx_strncasecmp((u_char *) c,
                                   (u_char *) "Connection:", 11) == 0) {
            c += 11;
            while (*c == ' ') {
                ++c;
            }
            if (ngx_strncasecmp((u_char *) c, (u_char *) "close", 5) == 0) {
                parse->close_conn = 1;
            } else if (ngx_strncasecmp((u_char *) c, (u_char *) "keep-alive",
                                       10) == 0) {
                parse->close_conn = -1;
            }
        }

        c = t + 2;
        parse->offset = c - parse->buf;
    }

    if (parse->parse_state == body) {
        t = NULL;
        if (parse->body_type == cont_len) {
            t = c;
            len = ngx_min((ngx_int_t) (parse->limit - parse->offset),
                          parse->content_len);
            if (len > 0 && parse->json_parser) {
                j_status = yajl_tree_parse_incrementally(parse->json_parser,
                                                         (const char *) t, len);
                if (j_status != yajl_status_ok) {
                    ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                                  "parse nacos resp. invalid json[0]");
                    yajl_tree_free_parser(parse->json_parser);
                    parse->json_parser = NULL;
                }
            }

            parse->offset += len;
            parse->content_len -= (ngx_int_t) len;
            if (parse->content_len == 0) {
                parse->parse_state = ended;
                return NGX_OK;
            }
            goto move_again;
        } else if (parse->body_type == oef) {
            t = c;
            len = (ngx_int_t) parse->limit - parse->offset;
            parse->real_body_len += len;
            if (len > 0 && parse->json_parser) {
                j_status = yajl_tree_parse_incrementally(parse->json_parser,
                                                         (const char *) t, len);
                if (j_status != yajl_status_ok) {
                    ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                                  "parse nacos resp. invalid json[1]");
                    yajl_tree_free_parser(parse->json_parser);
                    parse->json_parser = NULL;
                }
            }
            parse->offset += len;
            if (parse->conn_eof) {
                parse->parse_state = ended;
                return NGX_OK;
            }
            goto move_again;
        } else {  // chunked
            while (parse->parse_state == body) {
                c = parse->buf + parse->offset;

                if (parse->chunk_size == -2) {  // end chunk
                    t = n_strnstr(c, e, "\r\n");
                    if (t == NULL) {
                        goto move_again;
                    }
                    ch = n_hex2int(c, &parse->chunk_size);
                    if (ch != t) {
                        ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                                      "parse nacos resp. invalid chunk size");
                        return NGX_ERROR;
                    }
                    parse->real_body_len += parse->chunk_size;
                    c = t + 2;
                    parse->offset = c - parse->buf;
                    if (parse->chunk_size == 0) {
                        parse->parse_state = ended;
                        return NGX_OK;
                    }
                }

                if (parse->chunk_size > 0) {
                    len = ngx_min(parse->chunk_size,
                                  (ngx_int_t) (parse->limit - parse->offset));
                    if (len > 0 && parse->json_parser) {
                        j_status = yajl_tree_parse_incrementally(
                            parse->json_parser, (const char *) c, len);
                        if (j_status != yajl_status_ok) {
                            ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                                          "parse nacos resp. invalid json[2]");
                            yajl_tree_free_parser(parse->json_parser);
                            parse->json_parser = NULL;
                        }
                    }
                    c += len;
                    parse->offset = c - parse->buf;
                    parse->chunk_size -= len;
                    if (parse->chunk_size > 0) {
                        goto move_again;
                    }
                }

                // chunk_size <= 0
                if (parse->chunk_size == 0) {
                    if (parse->limit - parse->offset >= 2) {
                        if (n_strnstr(c, e, "\r\n") != c) {
                            ngx_log_error(
                                NGX_LOG_WARN, parse->log, 0,
                                "parse nacos resp. absent chunk end line");
                            return NGX_ERROR;
                        }
                        c += 2;
                        parse->offset = c - parse->buf;
                        parse->chunk_size = -2;
                    } else {
                        goto move_again;
                    }
                } else {
                    // not hit
                    ngx_log_error(NGX_LOG_WARN, parse->log, 0,
                                  "parse nacos resp. chunk bug???");
                    return NGX_ERROR;
                }
            }
        }
    }

    if (parse->parse_state == ended) {
        return NGX_OK;
    }

move_again:
    if (parse->offset == parse->limit) {
        parse->offset = 0;
        parse->limit = 0;
        return NGX_AGAIN;
    }

    len = parse->limit - parse->offset;
    if (parse->offset > 1024 && len < 4096) {
        c = ngx_movemem(parse->buf, parse->buf + parse->offset, len);
        parse->offset = 0;
        parse->limit = len;
    }
    return NGX_AGAIN;
}

#define NACOS_SUB_RESP_BUF_SIZE 65536

ngx_int_t ngx_nacos_http_req_json_sync(
    ngx_nacos_main_conf_t *mcf, ngx_str_t *req,
    ngx_nacos_http_result_processor processor, void *ctx) {
    ngx_uint_t tries;
    ngx_addr_t *addrs;
    ngx_fd_t s;
    ngx_err_t err;
    ngx_nacos_http_parse_t parser;
    ngx_uint_t i, n;
    size_t sd;
    ssize_t rd;
    void *re_tmp;
    int domain;
    addrs = mcf->server_list.elts;

    tries = 0;
    s = -1;

    ngx_memzero(&parser, sizeof(parser));
    parser.log = mcf->error_log;
    parser.capacity = NACOS_SUB_RESP_BUF_SIZE;
    parser.buf = (u_char *) ngx_alloc(parser.capacity, parser.log);
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
    switch (addrs[i].sockaddr->sa_family) {
        case AF_INET6:
            domain = PF_INET6;
            break;
        case AF_INET:
            domain = PF_INET;
            break;
        case AF_UNIX:
            domain = PF_UNIX;
            break;
        default:
            ngx_log_error(NGX_LOG_WARN, parser.log, 0,
                          "nacos connect() unknown domain type",
                          &addrs[i].name);
            goto retry;
    }
    s = ngx_socket(domain, SOCK_STREAM, 0);
    if (s == -1) {
        return NGX_ERROR;
    }
    if (connect(s, addrs[i].sockaddr, addrs[i].socklen) != 0) {
        err = ngx_socket_errno;
        ngx_log_error(NGX_LOG_WARN, parser.log, err,
                      "nacos connect() to %V failed", &addrs[i].name);
        goto retry;
    }

    sd = 0;
    do {
        rd = ngx_write_fd(s, req->data + sd, req->len - sd);
        if (rd > 0) {
            sd += rd;
        } else if (rd == 0) {
            ngx_log_error(NGX_LOG_WARN, parser.log, 0,
                          "write request to %V failed, because of EOF occur",
                          &addrs[i].name);
            goto retry;
        } else {
            err = ngx_socket_errno;
            if (err == NGX_EINTR) {
                continue;
            }
            ngx_log_error(NGX_LOG_WARN, parser.log, err,
                          "write request to %V failed", &addrs[i].name);
            goto retry;
        }
    } while ((ngx_uint_t) sd < req->len);

    do {
        rd = ngx_read_fd(s, parser.buf + parser.limit,
                         parser.capacity - parser.limit);
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
            ngx_log_error(NGX_LOG_WARN, parser.log, err,
                          "read response from %V failed", &addrs[i].name);
            goto retry;
        }

        if (parser.limit == parser.capacity) {
            parser.capacity += NACOS_SUB_RESP_BUF_SIZE;
            re_tmp = realloc(parser.buf, parser.capacity);
            if (re_tmp == NULL) {
                goto fetch_failed;
            }
            parser.buf = re_tmp;
        }

    } while (rd);

fetch_success:
    rd = processor(&parser, ctx);
    if (rd == NGX_DECLINED) {
        goto retry;
    } else if (rd != NGX_ERROR) {
        goto free;
    }
fetch_failed:
    ngx_log_error(NGX_LOG_WARN, parser.log, 0,
                  "sync request to nacos server failed");
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

ngx_int_t ngx_nacos_http_append_user_pass_header(ngx_nacos_main_conf_t *mcf,
                                                 ngx_str_t *req,
                                                 size_t capacity) {
    u_char *p;
    p = req->data + req->len;
    if (mcf->username.len > 0 && mcf->password.len > 0) {
        // username: xxx\r\n
        // password: xxx\r\n
        if (mcf->username.len + mcf->password.len + req->len + 24 + 2 >=
            capacity) {
            return NGX_ERROR;
        }
        ngx_memcpy(p, "username: ", sizeof("username: ") - 1);
        p += sizeof("username: ") - 1;
        ngx_memcpy(p, mcf->username.data, mcf->username.len);
        p += mcf->username.len;
        *(p++) = '\r';
        *(p++) = '\n';
        ngx_memcpy(p, "password: ", sizeof("password: ") - 1);
        p += sizeof("password: ") - 1;
        ngx_memcpy(p, mcf->password.data, mcf->password.len);
        p += mcf->password.len;
        *(p++) = '\r';
        *(p++) = '\n';

        req->len = p - req->data;
    }
    return NGX_OK;
}