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
