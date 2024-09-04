#!/bin/bash
set -e

rm -rf nginx
rm -rf objs

nginx_version=1.20.2
curl -sSL https://nginx.org/download/nginx-${nginx_version}.tar.gz -o nginx.tar.gz

tar zxvf nginx.tar.gz
mv nginx-${nginx_version} nginx
cp -r modules nginx/modules
cd nginx
patch -p1 < ../patch/nginx.patch


./configure \
  --with-http_v2_module \
  --with-http_ssl_module \
  --add-module=modules/auxiliary \
  --add-module=modules/nacos \
  --prefix=.. \
  --conf-path=conf/my.conf \
  --error-log-path=objs/logs/error.log \
  --pid-path=objs/logs/nginx.pid \
  --lock-path=objs/logs/nginx.lock \
  --http-log-path=objs/logs/access.log \
  --http-client-body-temp-path=objs/client_body_temp \
  --http-proxy-temp-path=objs/proxy_temp \
  --http-fastcgi-temp-path=objs/fastcgi_temp \
  --http-uwsgi-temp-path=objs/uwsgi_temp \
  --http-scgi-temp-path=objs/scgi_temp

cd ..

mkdir -p objs/logs
rm -f nginx.tar.gz