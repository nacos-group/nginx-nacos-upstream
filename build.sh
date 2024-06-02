#!/bin/bash
set -e

rm -rf nginx
rm -rf cmake-build-debug

curl -sSL https://nginx.org/download/nginx-1.20.2.tar.gz -o nginx.tar.gz

tar zxvf nginx.tar.gz
mv nginx-1.20.2 nginx
cd nginx
patch -p1 < ../patch/nginx.patch


./configure \
  --with-http_v2_module \
  --with-http_ssl_module \
  --add-module=../modules/auxiliary \
  --add-module=../modules/nacos \
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

cd ..

mkdir -p cmake-build-debug
rm -f nginx.tar.gz