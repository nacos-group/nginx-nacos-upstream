
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
        # 如果provider使用的spring，data_id 要和 spring.application.name一致
        # 不知道 provider 端怎么写请参考 https://github.com/zhwaaaaaa/springmvc-nacos-registry
        nacos_subscribe_service service_name=springmvc-nacos-demo;
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
本项目包含 nginx 1.20 的代码。由于使用了 grpc 连接 nacos，所以需要安装 protobuf 和 protobuf-c 库。
ubuntu 下安装方式为

```sudo apt install libprotobuf-dev libprotobuf-c-dev```

grpc 使用的是 http2 传输，所以 nacos 模块需要和 http2 模块一起安装（ps: 可能还需提前安装 libopenssl，除非使用不加密的 http2）：

```./configure --add-module=modules/auxiliary --add-module=modules/nacos --with-http_ssl_module --with-http_v2_module && make```

### 原理
 - 新增加一个 auxiliary 模块, 启动一个单独辅助进程，用于订阅和接受 nacos 的 grpc 或者 udp 消息推送，不影响 worker 进程的工作。
 - 收到消息推送后更新到共享内存，便于 worker 进程可以拿到最新的推送。 推送的数据也会缓存到磁盘，下次启动时候首先从磁盘读取。

# 开发计划
 * 通过 UDP 协议订阅 nacos 服务。（✅）
 * 通过 GRPC 协议订阅 nacos 服务。（✅）
 * nginx 通过 GRPC 协议订阅 nacos 配置。（✅）
 * 发布 1.0 版本，可以基本使用。
 * 删除 nginx 原有代码，对 nginx 原有代码的修改通过 patch 支持各个 nginx 版本。
 * 支持集成 openresty 

# 致谢
感谢 [JetBrains](https://www.jetbrains.com.cn) 公司赠送激活码，作者使用 [JetBrains Clion](https://www.jetbrains.com.cn/clion) 开发本项目过程中大大提升了开发效率。
