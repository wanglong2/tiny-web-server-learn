#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP 请求方法枚举（支持常见方法）
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        // 解析请求时的状态机：先解析请求行，再解析头部，最后解析内容
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化一个新的连接对象，绑定 socket 与客户端地址等
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭连接（立即或延迟），释放资源
    void close_conn(bool real_close = true);
    // 主处理入口：根据读写事件解析请求并生成响应
    void process();
    // 非阻塞读操作，读取 socket 到读缓冲区
    bool read_once();
    // 将写缓冲区的数据发送到 socket（可能分多次）
    bool write();
    // 返回关联的客户端地址
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 从数据库连接池初始化用户认证信息（m_users）
    void initmysql_result(connection_pool *connPool);
    // 定时器标记：用于判断是否超时
    int timer_flag;
    // 用于记录是否可改进（React/Proactor 模型用到的标志）
    int improv;


private:
    void init();
    // 内部初始化，重置连接状态
    void init();
    // 读取并解析请求，返回解析结果（HTTP_CODE）
    HTTP_CODE process_read();
    // 根据解析结果构建响应并写入写缓冲区
    bool process_write(HTTP_CODE ret);
    // 解析请求行（例如：GET /index.html HTTP/1.1）
    HTTP_CODE parse_request_line(char *text);
    // 解析请求头部（Host, Content-Length, Connection 等）
    HTTP_CODE parse_headers(char *text);
    // 解析请求体（通常用于 POST）
    HTTP_CODE parse_content(char *text);
    // 根据请求构造要返回的资源（打开文件或生成动态内容）
    HTTP_CODE do_request();
    // 从读缓冲区获取当前行指针
    char *get_line() { return m_read_buf + m_start_line; };
    // 检查一行是否完整（用于状态机行解析）
    LINE_STATUS parse_line();
    // 解除内存映射（当使用 mmap 映射文件时）
    void unmap();
    // 向写缓冲区追加格式化数据（类似 printf）
    bool add_response(const char *format, ...);
    // 向写缓冲区追加普通内容字符串
    bool add_content(const char *content);
    // 添加状态行（如 HTTP/1.1 200 OK）
    bool add_status_line(int status, const char *title);
    // 添加通用头部（Content-Length, Connection 等）
    bool add_headers(int content_length);
    // 添加 Content-Type 头部
    bool add_content_type();
    // 添加 Content-Length 头部
    bool add_content_length(int content_length);
    // 添加 Connection: keep-alive/close 头部
    bool add_linger();
    // 添加空行（头部结束标志）
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    // 读写状态：0=读, 1=写（用于区分当前处理的事件类型）
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    // socket 文件描述符
    int m_sockfd;
    // 客户端地址
    sockaddr_in m_address;
    // 读缓冲区，用于存放从 socket 读取的数据
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区中已读入的数据长度
    long m_read_idx;
    // 解析过程中当前检查的位置索引
    long m_checked_idx;
    // 当前行在读缓冲区的起始位置
    int m_start_line;
    // 写缓冲区，用于存放要发送的数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写缓冲区中已填充的数据长度
    int m_write_idx;
    // 当前解析状态（请求行/头/体）
    CHECK_STATE m_check_state;
    // HTTP 方法（GET/POST/...）
    METHOD m_method;
    // 请求的真实文件路径（文件名缓冲区）
    char m_real_file[FILENAME_LEN];
    // 请求的 URL 路径
    char *m_url;
    // HTTP 版本字符串（如 HTTP/1.1）
    char *m_version;
    // Host 字段
    char *m_host;
    // 请求体长度（Content-Length）
    long m_content_length;
    // 是否保持长连接（Connection: keep-alive）
    bool m_linger;
    // 文件映射地址（mmap 返回）
    char *m_file_address;
    // 文件状态信息（用于 sendfile 或判断文件大小）
    struct stat m_file_stat;
    // writev 使用的 IO 向量
    struct iovec m_iv[2];
    // m_iv 中使用的向量个数
    int m_iv_count;
    // 是否启用 CGI（POST）
    int cgi;        //是否启用的POST
    // 存储请求体或其它临时字符串数据
    char *m_string; //存储请求头数据
    // 要发送的总字节数（写操作中使用）
    int bytes_to_send;
    // 已经发送的字节数
    int bytes_have_send;
    // 文档根目录（服务器根路径）
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
