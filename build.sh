#!/bin/bash
set -e

wget https://nginx.org/download/nginx-1.22.2.tar.gz

tar zxvf nginx-1.22.2.tar.gz
cd nginx-1.22.2
cp -rf ../modules modules
cp -f ../CMakeLists.txt .
patch -p1 < ../patch/nginx.patch


./configure \
  --with-http_v2_module \
  --with-http_ssl_module \
  --add-module=modules/auxiliary \
  --add-module=modules/nacos \
  --prefix=.. \
  --conf-path=conf/my.conf \
  --error-log-path=cmake-build-debug/logs/error.log \
  --pid-path=cmake-build-debug/logs/nginx.pid \
  --lock-path=cmake-build-debug/logs/nginx.lock \
  --http-log-path=cmake-build-debug/logs/access.log \
  --http-client-body-temp-path=cmake-build-debug/client_body_temp \
  --http-proxy-temp-path=cmake-build-debug/proxy_temp \
  --http-fastcgi-temp-path=cmake-build-debug/fastcgi_temp \
  --http-uwsgi-temp-path=cmake-build-debug/uwsgi_temp \
  --http-scgi-temp-path=cmake-build-debug/scgi_temp

cp -f objs/ngx_auto_config.h cobjs
cp -f objs/ngx_auto_headers.h cobjs
cp -f objs/ngx_modules.c cobjs
