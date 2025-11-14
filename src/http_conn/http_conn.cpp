#include "../../include/http_conn.h"

void HttpConn::init_connection(int sock_fd, const struct sockaddr_in &addr,int epoll_fd, int trigger_mode){
    
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