#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>

class HttpConn{
    public:
        // 主状态机： 当前解析阶段
        enum class CheckState: uint8_t{
            request_line,
            header,
            content,
            done,
            bad
        };
        // 从状态机： 当前行的解析状态
        enum class LineStatus: uint8_t{
            line_ok,
            line_bad,
            line_open
        };
        // HTTP请求方法
        enum class Method: uint8_t{
            get,
            post,
            del,
            put,
        };
        // HTTP中间层状态码
        enum class HttpCode
        {
            no_request,
            get_request,
            bad_request,
            no_resource,
            forbidden_request,
            file_request,
            internal_error,
            closed_connection,
        };
        // 封装解析到的Http请求
        struct HttpRequest {
            Method method;
            char* route;
            std::unordered_map<std::string, std::string> parameter;

            int content_len;
            char* host;
            bool keep_alive;

            char* content;

            HttpRequest() : method(Method::get), route(nullptr), content_len(0),
                             host(nullptr), keep_alive(false), content(nullptr) {};
        };
        static constexpr int TRIGGER_LT = 0;
        static constexpr int TRIGGER_ET = 1;
        static constexpr int READ_BUFFER_SIZE = 2048;
        static constexpr int WRITE_BUFFER_SIZE = 2048;

    private:
        int m_sock_fd;
        struct sockaddr_in m_addr;

        // 读写事务标志位
        uint8_t m_read_write = 255;

        // 读缓冲区与请求报文解析
        char m_read_buffer[READ_BUFFER_SIZE];
        int m_read_idx = 0;
        int m_checked_idx = 0;
        int m_start_line = 0;
        CheckState m_check_state;
        struct HttpRequest m_request;

        // 写缓冲区与响应报文
        char m_write_buffer[WRITE_BUFFER_SIZE];
        int m_write_idx = 0;

        static int m_epoll_fd;
        static int m_trigger_mode;

    // tools
    private:
        void del_from_epoll();
        void reset_state();

    public:
        void init_connection(int sock_fd, const struct sockaddr_in &addr,int epoll_fd, int trigger_mode);
        void read_data();
        // 解析HTTP请求
        HttpConn::HttpCode parse_request();
        HttpConn::LineStatus pares_line();
        HttpCode parse_request_line(char* s);
        HttpCode parse_header(char* s);
        HttpCode parse_content(char* s);

        void parse_query_parameter(char *p);
        
        // 处理HTTP请求
        void process_request();
};








#endif