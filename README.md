
# NGNIX NACOS 模块
nginx 通过订阅 nacos  ip 端口， 进行反向代理。不用 reload 重启进程

### 配置示例
基于 NGINX 1.20.2 和 NACOS 2.10 版本开发。

```
nacos {
    server_list localhost:8848; # nacos 服务器列表，空格隔开
    udp_port 19999; #udp 端口号
    udp_ip 127.0.0.1; #udp ip 地址。
    udp_bind 0.0.0.0:19999; # 绑定udp 地址
    error_log logs/nacos.log info;
    default_group DEFAULT_GROUP; # 默认的nacos group name
    cache_dir cmake-build-debug/nacos/;
}

http {
    upstream s {
        # 如果provider使用的spring，data_id 要和 spring.application.name一致
        # 不知道 provider 端怎么写请参考 https://github.com/zhwaaaaaa/springmvc-nacos-registry
        use_nacos_address data_id=springmvc-nacos-demo;
    }
    server {
        # ... other config
        location ^~ / {
            proxy_pass http://s;
        }
    }
}
```

### 编译
```./configure --add-module=modules/auxiliary --add-module=modules/nacos && make ```

### 原理
 - 新增加一个 auxiliary 模块, 启动一个单独辅助进程，用于订阅和接受 nacos 的 udp 消息推送，不影响 worker 进程的工作。
 - 收到消息推送后更新到共享内存，便于 worker 进程可以拿到最新的推送。 推送的数据也会缓存到磁盘，下次启动时候首先从磁盘读取。

