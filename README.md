
# NGNIX NACOS 模块（插件）

nginx 订阅 nacos，实现 服务发现 和 配置 动态更新。 nacos 1.x 使用 udp 协议，nacos 2.x 使用 grpc 协议，本项目都支持
- nginx 订阅 nacos 获取后端服务地址，不用在 upstream 中配置 ip 地址，后端服务发布下线自动更新。
- nginx 订阅 nacos 获取配置，写入 nginx 标准变量。（配置功能，只支持 grpc 协议）。

### 配置示例
基于 NGINX 1.20.2 和 NACOS 2.x 版本开发。

```
nacos {
    server_list localhost:8848; # nacos 服务器列表，空格隔开

    # 可以使用 grpc 订阅。 nacos 2.x 版本才支持，推荐使用。
    grpc_server_list localhost:9848; # nacos grpc服务器列表，空格隔开。一般是 nacos 端口 + 1000
    
    # 也可使用udp。都配置的情况下使用 udp。
    #udp_port 19999; #udp 端口号
    #udp_ip 127.0.0.1; #udp ip 地址。
    #udp_bind 0.0.0.0:19999; # 绑定udp 地址
    error_log logs/nacos.log info;
    default_group DEFAULT_GROUP; # 默认的nacos group name
    cache_dir cmake-build-debug/nacos/;
}

http {
    upstream s {
        # 不需要写死 后端 ip。配置 nacos 服务名，nginx 自己订阅
        # server 127.0.0.1:8080;
        # 如果provider使用的spring，service_name 要和 spring.application.name一致
        # 不知道 provider 端怎么写请参考 https://github.com/zhwaaaaaa/springmvc-nacos-registry
        nacos_subscribe_service service_name=springmvc-nacos-demo group=DEFAULT_GROUP;
    }
    
    # 订阅 nacos 的配置。$n_var = "content"  $dd = "md5"
    nacos_config_var $n_var data_id=aaabbbbccc group=DEFAULT_GROUP md5_var=$dd default=123456;
    
    server {
        # ... other config s
        location ^~ / {
            # 把 从 nacos 拿到的配置加入 header
            add_header X-Var-Nacos "$n_var" always;
            # 反向代理 到 s 服务。s服务是 从 nacos 动态订阅的。
            proxy_pass http://s;
        }
        location ^~ /echo {
            nacos_config_var $n_var data_id=ccdd;
            # 把 从 nacos 拿到的配置加入 header 和 body
            add_header X-Var-Nacos "$n_var";
            return 200 "hear .... $n_var .... $dd";
        }
    }
}
```

### 编译
本项目包含 nginx 1.20 的代码。由于使用了 grpc 连接 nacos，所以除了需要安装 nginx 本身的依赖以，外还需要安装 protobuf 和 protobuf-c 库。
ubuntu 下安装方式为

```bash
 sudo apt install libprotobuf-dev libprotobuf-c-dev
 sudo apt install build-essential libpcre3 libpcre3-dev zlib1g zlib1g-dev libssl-dev
 ```

grpc 使用的是 http2 传输，所以 nacos 模块需要和 http2 模块一起安装：

```bash
./configure --add-module=modules/auxiliary --add-module=modules/nacos --with-http_ssl_module --with-http_v2_module && make
```

### 原理
 - 新增加一个 auxiliary 模块, 启动一个单独辅助进程，用于订阅和接受 nacos 的 grpc 或者 udp 消息推送，不影响 worker 进程的工作。
 - 收到消息推送后更新到共享内存，便于 worker 进程可以拿到最新的推送。 推送的数据也会缓存到磁盘，下次启动时候首先从磁盘读取。

# 详细配置
### nacos block
nacos {} 必需放在 http {} 的前面。

#### server_list
配置 nacos 的 http 地址，这个地址必需要配置。
```
 server_list ip1:8848 ip2:8848 ip3:8848;
```
#### grpc_server_list
配置 nacos 的 grpc 地址，nacos server 需要升级 到 2.x 版本才能支持。（推荐）
```
 grpc_server_list ip1:9848 ip2:9848 ip3:9848;
```

#### udp_port  （不推荐）
配置 nginx 的 udp 端口号。nacos 1.x 使用的是 udp 推送，nginx 会开启 udp 端口接受 nacos server 推送。不支持 配置
```
 udp_port 19999;
```
#### udp_ip  （不推荐）
nginx 自己的 ip。nginx 会把自己的 udp ip 和端口告诉 nacos, nacos 会给 nginx 发送 udp 消息。
```
 udp_ip 127.0.0.1;
```
#### udp_bind（不推荐）
nginx 监听的 udp ip 和端口。原则上和 上边的 ip+port 一致。如果使用的是 docker。上面配置的则 可能是主机的 ip 和 映射的端口
```
udp_bind 0.0.0.0:19999;
```

#### error_log 
nacos 日志文件 和 级别.
```
error_log logs/nacos.log info;
```
#### default_group
nacos 使用的是默认的 group。订阅的时候 可能只是制定了 data_id 或 service_name,没有指定 group= 则使用这个值
```
default_group DEFAULT_GROUP;
```
#### cache_dir
nacos 的文件 缓存目录，下次启动 会优先从 这个目录读取数据，加快启动时间，否则从 http 地址拉取。"/"结尾
```
cache_dir nacos_cache/;
```

#### server_host （可省略，有默认值）
nacos http 请求所使用的 host
```
server_host nacos;
```
#### server_host （可省略，默认值：nacos）
nacos http 请求所使用的 host
```
server_host nacos;
```
#### key_zone_size （默认 4M）
共享内存大小，订阅 nacos 的所有数据都会缓存到共享内存。以便于 work 进程能够获取。订阅的配置多需要调大
```
key_zone_size 16M;
```
#### keys_hash_max_size （默认 128）
nacos service 的hash 表大小
```
keys_hash_max_size 128;
```
#### keys_bucket_size （默认 128）
nacos service 的 hash 表桶大小
```
keys_bucket_size 128;
```
#### config_keys_hash_max_size （默认 128）
nacos config 的hash 表大小
```
config_keys_hash_max_size 128;
```
#### config_keys_bucket_size （默认 128）
nacos config 的 hash 表桶大小
```
config_keys_bucket_size 128;
```
#### udp_pool_size （默认 8192）
连接的 pool 大小
```
udp_pool_size 8192;
```

### nacos_subscribe_service
在 upstream 中，不需要配置 后端 ip 和 端口。
通过 nacos_subscribe_service 指定服务名，nginx 会订阅 nacos 中的服务，自动填充。
服务发布下线 也会 自动更新。
```
upstream backend {
    # 不需要写死 后端 ip。配置 nacos 服务名，nginx 自己订阅
    # server 127.0.0.1:8080;
    # 如果provider使用的spring，service_name 要和 spring.application.name一致
    # 不知道 provider 端怎么写请参考 https://github.com/zhwaaaaaa/springmvc-nacos-registry
    nacos_subscribe_service service_name=springmvc-nacos-demo group=DEFAULT_GROUP;
}
```

### nacos_config_var
订阅 nacos 的配置，nginx把它写到 http 变量中。这个配置项可以出现在 http server location if {} 块中。
```
nacos_config_var $var_name md5_var=$md5_var_name data_id=xxxx group=xxx default=def_value;
```
通过 $var_name 获取到 配置的内容。$md5_var_name 可以获取到配置的 md5。
如果nacos中指定的 data_id 和 group 不存在，使用 默认值 def_value

# 开发计划
 * 通过 UDP 协议订阅 nacos 服务。（✅）
 * 通过 GRPC 协议订阅 nacos 服务。（✅）
 * nginx 通过 GRPC 协议订阅 nacos 配置。（✅）
 * 发布 1.0 版本，可以基本使用。
 * 删除 nginx 原有代码，对 nginx 原有代码的修改通过 patch 支持各个 nginx 版本。
 * 支持集成 openresty 

# 致谢
感谢 [JetBrains](https://www.jetbrains.com.cn) 公司赠送激活码，作者使用 [JetBrains Clion](https://www.jetbrains.com.cn/clion) 开发本项目过程中大大提升了开发效率。
