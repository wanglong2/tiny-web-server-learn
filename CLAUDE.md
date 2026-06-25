# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
sh ./build.sh                         # 编译（默认 Debug），等价于 cmake -B build && cmake --build build
sh ./build.sh build Release           # Release 编译
./build/server                        # 运行（默认端口9006）
rm -rf build                          # 清理
```

**编译依赖**: cmake (>=3.10), g++, libmysqlclient-dev, libpthread。

**启动参数**:

```
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

| 参数 | 含义 | 默认值 |
|------|------|--------|
| `-p` | 端口号 | 9006 |
| `-l` | 日志写入方式 (0=同步, 1=异步) | 0 |
| `-m` | 触发模式 (0=LT+LT, 1=LT+ET, 2=ET+LT, 3=ET+ET) | 0 |
| `-o` | 优雅关闭连接 (0=不使用, 1=使用) | 0 |
| `-s` | 数据库连接池数量 | 8 |
| `-t` | 线程池线程数 | 8 |
| `-c` | 关闭日志 (0=打开, 1=关闭) | 0 |
| `-a` | 并发模型 (0=Proactor, 1=Reactor) | 0 |

运行前需要 MySQL: 创建 `yourdb` 库，建 `user(username, passwd)` 表，修改 `main.cpp` 中的数据库用户名/密码/库名。

## 压测

```bash
cd test_pressure/webbench-1.5 && make && ./webbench -c 10500 -t 5 http://127.0.0.1:9006/
```

## 架构总览

单进程、多线程的 Linux C++ Web 服务器，并发模型为 **epoll + 半同步/半反应堆线程池**。

### 核心数据流

```
main()                    // 解析参数 → init → log_write → sql_pool → thread_pool → trig_mode → eventListen → eventLoop
  └─ eventLoop()          // epoll_wait 循环
       ├─ listenfd 事件   → dealclientdata() → accept + timer()
       ├─ pipefd[0] 事件  → dealwithsignal()  (SIGALRM/SIGTERM)
       ├─ EPOLLIN 事件    → dealwithread()
       │    ├─ Reactor:   append(request, 0) 到线程池 → 线程池做 read_once + process
       │    └─ Proactor:  read_once() 完成 I/O → append_p(request) 到线程池 → 线程池做 process
       └─ EPOLLOUT 事件   → dealwithwrite()
            ├─ Reactor:   append(request, 1) 到线程池 → 线程池做 write
            └─ Proactor:  write() 完成 I/O
```

### 关键类的职责

- **`WebServer`** (`webserver.h/cpp`): 总控。持有 epollfd、线程池、连接池、定时器列表、所有 `http_conn` 对象数组。`eventLoop()` 是主循环。
- **`http_conn`** (`http/http_conn.h/cpp`): HTTP 连接处理。读缓冲 `m_read_buf[2048]`，写缓冲 `m_write_buf[1024]`。用**状态机**解析请求行→请求头→请求体，支持 GET 和 POST(cgi)。响应使用 `writev` 分散写。`process()` 是业务入口（调用 `process_read` → `do_request` → `process_write`）。
- **`threadpool<http_conn>`** (`threadpool/threadpool.h`): 半同步/半反应堆线程池。`append(T*, state)` 为 Reactor 接口（带 state 标记读写），`append_p(T*)` 为 Proactor 接口。工作线程阻塞在 `sem.wait()`，通过 `std::list` 维护请求队列。
- **`Utils` + `sort_timer_lst`** (`timer/lst_timer.h/cpp`): 升序双向链表定时器，每个 `http_conn` 绑定一个定时器，超时则回调 `cb_func` 关闭连接。使用 `SIGALRM` 周期性触发 tick，通过 `pipefd` 统一事件源。
- **`Log`** (`log/log.h/cpp`): 单例。同步模式直接 `fputs`；异步模式启动 `flush_log_thread` 线程，日志先 `push` 到 `block_queue<string>`，线程异步取出写入。
- **`connection_pool`** (`CGImysql/sql_connection_pool.h/cpp`): MySQL 连接池单例，基于 `list<MYSQL*>` + semaphore。`connectionRAII` 用 RAII 封装获取/释放。
- **`block_queue<T>`** (`log/block_queue.h`): 循环数组实现的线程安全阻塞队列，用于异步日志。
- **`locker / sem / cond`** (`lock/locker.h`): pthread 同步原语的 RAII 封装。

### Reactor vs Proactor

- **Proactor** (`actor_model=0`): 主线程通过 `read_once()`/`write()` 完成 I/O，然后将 `http_conn*` 放入线程池，线程池只做 `process()` 业务处理。
- **Reactor** (`actor_model=1`): 主线程只监听事件，将 `http_conn*` + 读写状态放入线程池，线程池完成 `read_once()`/`write()` + `process()`，通过 `improv` 字段通知主线程完成。

### ET vs LT

`m_TRIGMode` 控制 listenfd 和 connfd 的触发模式组合。LT 模式每次 accept 一个连接；ET 模式循环 accept 直到 EAGAIN。connfd 的 ET/LT 影响 `addfd()` 中对 EPOLLET 标志的设置。

### 定时器与信号处理

使用 `socketpair` 创建双向管道 `m_pipefd`，`SIGALRM` 和 `SIGTERM` 信号处理函数向管道写入信号值，从而将信号事件统一到 epoll 事件循环中处理。`SIGALRM` 触发 `timer_handler()` 调用 `sort_timer_lst::tick()` 清理超时连接。
