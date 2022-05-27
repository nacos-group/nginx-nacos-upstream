
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
}
http {
    upstream s {
        use_nacos_address data_id=cc group=aaa; # 使用nacos ip
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
```./configure --add-module=modules/nacos && make ```

### 原理
nginx 启动的时候， 向nacos 服务器发送一个 http 请求。

```GET /nacos/v1/ns/instance/list?serviceNames=...&udpPort=udp_port&clientIp=udp_ip```

然后打开一个udp 端口，接受nacos 的 ip 地址推送。
收到推送信息后，更新到共享内存。以便于每个worker 都能获取到最新的ip，进行反向代理
