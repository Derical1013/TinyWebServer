#include "../../include/http_conn.h"

int HttpConn::m_epoll_fd = -1;

void HttpConn::init_connection(int sock_fd, const struct sockaddr_in &addr,int epoll_fd, int trigger_mode){
    
    m_sock_fd = sock_fd;
    m_addr = addr;
    m_epoll_fd = epoll_fd;

    struct epoll_event event;
    event.data.fd = m_sock_fd;
    if (trigger_mode == TRIGGER_ET){
        event.events = EPOLLIN | EPOLLET;
    }
    else
        event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event);
}

void HttpConn::read_data(){

    // 缓冲区溢出
    if (m_read_idx >= READ_BUFFER_SIZE){
        printf("Read_buffer overflow\n");
        return;
    }

    if (m_trigger_mode == TRIGGER_LT){

        int len = recv(m_sock_fd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (len == 0){
            printf("Client is disconnected.\n");
            return;
        }
        else if (len < 0){
            perror("LT read");
            return;
        }
        m_read_idx += len;

    }
    else if (m_trigger_mode == TRIGGER_ET){
        
        while (true){
            int len = recv(m_sock_fd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (len == 0){
                printf("Client is disconnected.\n");
                return;
            }
            else if (len < 0){
                if (errno == EAGAIN){
                    return;
                }
            }
            m_read_idx += len;
        }
        return;
    }
}

void HttpConn::reset_state(){

    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_check_state = CheckState::request_line;
    m_trigger_mode = TRIGGER_LT;
    m_request = HttpRequest{};
}

/*
 *  主状态机
 *  解析请求报文
 */ 
HttpConn::HttpCode HttpConn::parse_request(){
    
    LineStatus line_status = LineStatus::line_ok;

    while ((m_check_state == CheckState::content && line_status == LineStatus::line_ok)
            || pares_line() == LineStatus::line_ok){

                char* txt = m_read_buffer + m_start_line;
                m_start_line = m_checked_idx;
                HttpCode ret;
                switch(m_check_state){

                    case CheckState::request_line:{
                        ret = parse_request_line(txt);
                        if (ret == HttpCode::bad_request)
                            return ret;

                        // 解析完成，进入下一个状态
                        m_check_state = CheckState::header;
                        break;
                    }
                    case CheckState::header:{
                        ret = parse_header(txt);
                        if (ret == HttpCode::bad_request)
                            return ret;
                        // 无body
                        if (ret == HttpCode::get_request){
                            process_request();
                            return ret;
                        }
                        // 有content的情况会自动跳转到content状态
                        break;
                    }
                    case CheckState::content:{
                        ret = parse_content(txt);
                        // 解析完成
                        if (ret == HttpCode::get_request){
                            process_request();
                            return ret;
                        }
                        // 设置状态调用parse_line继续解析
                        line_status = LineStatus::line_open;
                        break;
                    }
                    default:
                        return HttpCode::internal_error;
                }
            }
        return HttpCode::no_request;
}

/*
 *  从状态机
 *  解析行
 */
HttpConn::LineStatus HttpConn::pares_line(){
    char tmp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx){
        tmp = m_read_buffer[m_checked_idx];

        if (tmp == '\r'){
            if (m_checked_idx == m_read_idx - 1)
                return LineStatus::line_open;
            if (m_read_buffer[m_checked_idx + 1] == '\n'){
                m_read_buffer[m_checked_idx++] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LineStatus::line_ok;
            }
            return LineStatus::line_bad;
        }
    }
    return LineStatus::line_open;
}

HttpConn::HttpCode HttpConn::parse_request_line(char* s){
    printf("REQUEST LINE:\n%s\n",s);

    char* method, *url, *protocol;
    
    method = s;
    url = strpbrk(url, " \t");
    // 没有空格或\t，错误格式
    if (!url)
        return HttpCode::bad_request;
    *url++ = '\0';
    // 防止有多个空格/制表符
    url += strspn(url, " \t");
    protocol = strpbrk(url, " \t");
    *protocol++ = '\0';
    protocol += strspn(url, " \t");

    /*
     * 解析三个部分
     */

    //解析请求方式
    if (strcmp(method, "GET") == 0){
        m_request.method = Method::get;
    }else if (strcmp(method, "POST") == 0){
        m_request.method = Method::post;       
    }else if (strcmp(method, "PUT") == 0){
        m_request.method = Method::put;
    }else if (strcmp(method, "DELETE") == 0){
        m_request.method = Method::del;        
    }else
        return HttpCode::bad_request;

    //解析路径与参数
    if (strncasecmp(url, "http://", 7) == 0){
        url += 7;
        url = strchr(url, '/');
    }else if (strncasecmp(url, "https://", 8) == 0){
        url += 8;
        url = strchr(url, '/');
    }
    // 错误请求
    if (*url == '\0')
        return HttpCode::bad_request;
    int i = 0;
    for (; url[i] != '\0'; ++i){
        if(url[i] == '?'){
            url[i] = '\0';
            parse_query_parameter(url + i + 1);
            break;
        }
    }
    m_request.route = url;


    // 解析协议
    // 暂时只支持HTTP/1.1
    if (strcmp(protocol, "HTTP/1.1") != 0)
        return HttpCode::bad_request;
    

    return HttpCode::no_request;
}

HttpConn::HttpCode HttpConn::parse_header(char* s){
    printf("Header:\n%s\n", s);
    if (s[0] == '\0'){
        // 有请求体
        if (m_request.content_len != 0){
            m_check_state = CheckState::content;
            return HttpCode::no_request;
        }
        return HttpCode::get_request;
    }
    else if (strncasecmp(s, "Connection:", 11) == 0){
        s += 11;
        s += strspn(s, "\t ");
        // 长连接
        if (strncasecmp(s, "keep-alive", 9) == 0){
            m_request.keep_alive = true;
        }
    }
    else if (strncasecmp(s, "Host:", 5) == 0){
        s += 5;
        s += strspn(s, "\t ");
        m_request.host = s;
    }
    else if (strncasecmp(s, "Content-length:", 15) == 0){
        s += 15;
        s += strspn(s, "\t ");
        m_request.content_len = atoi(s);
    }

    return HttpCode::no_request;
};

HttpConn::HttpCode HttpConn::parse_content(char* s){
    printf("Content:\n%s\n", s);

    // 已经读完所有的content
    if ((m_checked_idx + m_request.content_len) <= m_read_idx){
        // content可以由任意字符结尾，取决于client传输的数据
        // 因此人为加上结束符
        s[m_request.content_len] = '\0';
        m_request.content = s;
        return HttpCode::get_request;
    }

    // 继续进入状态机等待完成读取
    return HttpCode::no_request;
}

void HttpConn::parse_query_parameter(char *p){
    char* start = p, *tmp;
    while ((tmp = strchr(start, '&')) != NULL){
        *tmp = '\0';
        char* new_start = tmp + 1;
        tmp = strchr(start, '=');
        *tmp = '\0';
        m_request.parameter[start] = tmp + 1;
        start = new_start;
    }
    // 没有后续的参数对
    tmp = strchr(start, '=');
    *tmp = '\0';
    m_request.parameter[start] = tmp + 1;

}


// 处理Http请求
void HttpConn::process_request(){

}