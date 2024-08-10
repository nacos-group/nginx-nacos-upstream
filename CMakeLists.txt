cmake_minimum_required(VERSION 3.16)
project(nginx_nacos C)

set(CMAKE_C_STANDARD 90)

set(CMAKE_C_FLAGS "-W -Wall -Wpointer-arith -Wno-unused-parameter -Werror=uninitialized -Werror -Wreturn-type -Wno-deprecated-declarations")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -W -Wall -Wno-unused-parameter -Werror=uninitialized")

set(NGX_C_FILES nginx/src/core/nginx.c
        nginx/src/core/ngx_log.c
        nginx/src/core/ngx_palloc.c
        nginx/src/core/ngx_array.c
        nginx/src/core/ngx_list.c
        nginx/src/core/ngx_hash.c
        nginx/src/core/ngx_buf.c
        nginx/src/core/ngx_queue.c
        nginx/src/core/ngx_output_chain.c
        nginx/src/core/ngx_string.c
        nginx/src/core/ngx_parse.c
        nginx/src/core/ngx_parse_time.c
        nginx/src/core/ngx_inet.c
        nginx/src/core/ngx_file.c
        nginx/src/core/ngx_crc32.c
        nginx/src/core/ngx_murmurhash.c
        nginx/src/core/ngx_md5.c
        nginx/src/core/ngx_sha1.c
        nginx/src/core/ngx_rbtree.c
        nginx/src/core/ngx_radix_tree.c
        nginx/src/core/ngx_slab.c
        nginx/src/core/ngx_times.c
        nginx/src/core/ngx_shmtx.c
        nginx/src/core/ngx_connection.c
        nginx/src/core/ngx_cycle.c
        nginx/src/core/ngx_spinlock.c
        nginx/src/core/ngx_rwlock.c
        nginx/src/core/ngx_cpuinfo.c
        nginx/src/core/ngx_conf_file.c
        nginx/src/core/ngx_module.c
        nginx/src/core/ngx_resolver.c
        nginx/src/core/ngx_open_file_cache.c
        nginx/src/core/ngx_crypt.c
        nginx/src/core/ngx_proxy_protocol.c
        nginx/src/core/ngx_syslog.c
        nginx/src/event/ngx_event.c
        nginx/src/event/ngx_event_timer.c
        nginx/src/event/ngx_event_posted.c
        nginx/src/event/ngx_event_accept.c
        nginx/src/event/ngx_event_udp.c
        nginx/src/event/ngx_event_connect.c
        nginx/src/event/ngx_event_pipe.c
        nginx/src/os/unix/ngx_time.c
        nginx/src/os/unix/ngx_errno.c
        nginx/src/os/unix/ngx_alloc.c
        nginx/src/os/unix/ngx_files.c
        nginx/src/os/unix/ngx_socket.c
        nginx/src/os/unix/ngx_recv.c
        nginx/src/os/unix/ngx_readv_chain.c
        nginx/src/os/unix/ngx_udp_recv.c
        nginx/src/os/unix/ngx_send.c
        nginx/src/os/unix/ngx_writev_chain.c
        nginx/src/os/unix/ngx_udp_send.c
        nginx/src/os/unix/ngx_udp_sendmsg_chain.c
        nginx/src/os/unix/ngx_channel.c
        nginx/src/os/unix/ngx_shmem.c
        nginx/src/os/unix/ngx_process.c
        nginx/src/os/unix/ngx_daemon.c
        nginx/src/os/unix/ngx_setaffinity.c
        nginx/src/os/unix/ngx_setproctitle.c
        nginx/src/os/unix/ngx_posix_init.c
        nginx/src/os/unix/ngx_user.c
        nginx/src/os/unix/ngx_dlopen.c
        nginx/src/os/unix/ngx_process_cycle.c
        nginx/src/event/ngx_event_openssl.c
        nginx/src/event/ngx_event_openssl_stapling.c
        nginx/src/core/ngx_regex.c
        nginx/src/http/ngx_http.c
        nginx/src/http/ngx_http_core_module.c
        nginx/src/http/ngx_http_special_response.c
        nginx/src/http/ngx_http_request.c
        nginx/src/http/ngx_http_parse.c
        nginx/src/http/modules/ngx_http_log_module.c
        nginx/src/http/ngx_http_request_body.c
        nginx/src/http/ngx_http_variables.c
        nginx/src/http/ngx_http_script.c
        nginx/src/http/ngx_http_upstream.c
        nginx/src/http/ngx_http_upstream_round_robin.c
        nginx/src/http/ngx_http_file_cache.c
        nginx/src/http/ngx_http_write_filter_module.c
        nginx/src/http/ngx_http_header_filter_module.c
        nginx/src/http/modules/ngx_http_chunked_filter_module.c
        nginx/src/http/v2/ngx_http_v2_filter_module.c
        nginx/src/http/modules/ngx_http_range_filter_module.c
        nginx/src/http/modules/ngx_http_gzip_filter_module.c
        nginx/src/http/ngx_http_postpone_filter_module.c
        nginx/src/http/modules/ngx_http_ssi_filter_module.c
        nginx/src/http/modules/ngx_http_charset_filter_module.c
        nginx/src/http/modules/ngx_http_userid_filter_module.c
        nginx/src/http/modules/ngx_http_headers_filter_module.c
        nginx/src/http/ngx_http_copy_filter_module.c
        nginx/src/http/modules/ngx_http_not_modified_filter_module.c
        nginx/src/http/v2/ngx_http_v2.c
        nginx/src/http/v2/ngx_http_v2_table.c
        nginx/src/http/v2/ngx_http_v2_encode.c
        nginx/src/http/v2/ngx_http_v2_huff_decode.c
        nginx/src/http/v2/ngx_http_v2_huff_encode.c
        nginx/src/http/v2/ngx_http_v2_module.c
        nginx/src/http/modules/ngx_http_static_module.c
        nginx/src/http/modules/ngx_http_autoindex_module.c
        nginx/src/http/modules/ngx_http_index_module.c
        nginx/src/http/modules/ngx_http_mirror_module.c
        nginx/src/http/modules/ngx_http_try_files_module.c
        nginx/src/http/modules/ngx_http_auth_basic_module.c
        nginx/src/http/modules/ngx_http_access_module.c
        nginx/src/http/modules/ngx_http_limit_conn_module.c
        nginx/src/http/modules/ngx_http_limit_req_module.c
        nginx/src/http/modules/ngx_http_geo_module.c
        nginx/src/http/modules/ngx_http_map_module.c
        nginx/src/http/modules/ngx_http_split_clients_module.c
        nginx/src/http/modules/ngx_http_referer_module.c
        nginx/src/http/modules/ngx_http_rewrite_module.c
        nginx/src/http/modules/ngx_http_ssl_module.c
        nginx/src/http/modules/ngx_http_proxy_module.c
        nginx/src/http/modules/ngx_http_fastcgi_module.c
        nginx/src/http/modules/ngx_http_uwsgi_module.c
        nginx/src/http/modules/ngx_http_scgi_module.c
        nginx/src/http/modules/ngx_http_grpc_module.c
        nginx/src/http/modules/ngx_http_memcached_module.c
        nginx/src/http/modules/ngx_http_empty_gif_module.c
        nginx/src/http/modules/ngx_http_browser_module.c
        nginx/src/http/modules/ngx_http_upstream_hash_module.c
        nginx/src/http/modules/ngx_http_upstream_ip_hash_module.c
        nginx/src/http/modules/ngx_http_upstream_least_conn_module.c
        nginx/src/http/modules/ngx_http_upstream_random_module.c
        nginx/src/http/modules/ngx_http_upstream_keepalive_module.c
        nginx/src/http/modules/ngx_http_upstream_zone_module.c
        )

set(NGX_INC_DIR nginx/src/core
        nginx/src/event
        nginx/src/event/modules
        nginx/src/os/unix
        nginx/src/http
        nginx/src/http/modules
        nginx/src/http/v2
        nginx/objs)

set(NGX_LIBS dl pthread pcre z)
set(OPENSSL YES)

set(NACOS YES)
if (OPENSSL)
    list(APPEND NGX_C_FILES nginx/src/event/ngx_event_openssl.c
            nginx/src/event/ngx_event_openssl_stapling.c
            nginx/src/http/modules/ngx_http_ssl_module.c)
    find_package(OpenSSL REQUIRED)
    list(APPEND NGX_INC_DIR ${OPENSSL_INCLUDE_DIR})
    list(APPEND NGX_LIBS ${OPENSSL_LIBRARIES})

endif ()

set(BACKUP_PROTO modules/nacos/backup.pb.c)

if (NACOS)
    list(APPEND NGX_INC_DIR modules/auxiliary /usr/local/include)
    list(APPEND NGX_C_FILES modules/auxiliary/ngx_auxiliary_module.c)
    list(APPEND NGX_INC_DIR modules/nacos)
    list(APPEND NGX_LINK_DIR /usr/local/lib)
    list(APPEND NGX_C_FILES
            modules/nacos/ngx_nacos_module.c
            modules/nacos/ngx_nacos_aux.c
            modules/nacos/ngx_nacos_http_parse.c
            modules/nacos/ngx_nacos_data.c
            modules/nacos/ngx_http_nacos_upstream_module.c
            modules/nacos/ngx_http_nacos_config_module.c
            modules/nacos/ngx_nacos_udp.c
            modules/nacos/ngx_nacos_grpc.c
            modules/nacos/nacos_grpc_service.pb.c
            ${BACKUP_PROTO}
            )

    list(APPEND NGX_C_FILES
            modules/nacos/yaij/yajl.c
            modules/nacos/yaij/yajl_alloc.c
            modules/nacos/yaij/yajl_buf.c
            modules/nacos/yaij/yajl_encode.c
            modules/nacos/yaij/yajl_gen.c
            modules/nacos/yaij/yajl_lex.c
            modules/nacos/yaij/yajl_parser.c
            modules/nacos/yaij/yajl_tree.c
            modules/nacos/yaij/yajl_version.c
            modules/nacos/pb/pb_common.c
            modules/nacos/pb/pb_decode.c
            modules/nacos/pb/pb_encode.c
            )
endif ()


if (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    list(APPEND NGX_C_FILES
            nginx/src/os/unix/ngx_linux_init.c
            nginx/src/event/modules/ngx_epoll_module.c
            nginx/src/os/unix/ngx_linux_sendfile_chain.c)
    list(APPEND NGX_LIBS crypt)
elseif (CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    list(APPEND NGX_C_FILES
            nginx/src/os/unix/ngx_darwin_init.c
            nginx/src/event/modules/ngx_kqueue_module.c
            nginx/src/os/unix/ngx_darwin_sendfile_chain.c)
else ()
endif ()

list(APPEND NGX_C_FILES nginx/objs/ngx_modules.c)

message(STATUS ${NGX_LIBS})

add_executable(nginx_nacos ${NGX_C_FILES} ${NGX_H_FILES})
target_include_directories(nginx_nacos PRIVATE ${NGX_INC_DIR})
target_link_directories(nginx_nacos PRIVATE ${NGX_LINK_DIR})
target_link_libraries(nginx_nacos ${NGX_LIBS})