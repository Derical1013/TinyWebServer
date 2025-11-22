#include "../../include/http_conn.h"

int HttpConn::m_epoll_fd = -1;
int HttpConn::m_user_cnt = 0;

// reason_phrase
const char* STATUS_200 = "OK";
const char* STATUS_400 = "Bad Request";
const char* STATUS_400_MSG = "Your request contains bad syntax or cannot be understood by the server.\n";
const char* STATUS_403 = "Forbidden";
const char* STATUS_403_MSG = "You do not have permission to access the requested resource.\n";
const char* STATUS_404 = "Not Found";
const char* STATUS_404_MSG = "Access to the requested resource is denied.\n";
const char* STATUS_500 = "Internal Server Error";
const char* STATUS_500_MSG = "The server encountered an unexpected condition that prevented it from fulfilling the request.\n";

std::unordered_map<std::string, std::string> HttpConn::m_db_users;


void HttpConn::init_connection(int sock_fd, const struct sockaddr_in &addr, 
                                char* static_path,
                                int epoll_fd, int trigger_mode){
    
    m_sock_fd = sock_fd;
    m_addr = addr;
    m_epoll_fd = epoll_fd;
    m_static_path = static_path;

    struct epoll_event event;
    event.data.fd = m_sock_fd;
    if (trigger_mode == TRIGGER_ET){
        event.events = EPOLLIN | EPOLLET;

        int flag = fcntl(m_sock_fd, F_GETFL);
        flag |= O_NONBLOCK;
        fcntl(m_sock_fd, F_SETFL, flag);
    }
    else
        event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event);

    reset_state();
}

void HttpConn::init_mysql_result(ConnPool* conn_pool){

    MYSQL *mysql = NULL;
    ConnRaii conn_raii(&mysql, conn_pool);
    if (mysql_query(mysql, "SELECT username, password FROM users")){
        printf("mysql_query failed: %s\n", mysql_error(mysql));
    }
    MYSQL_RES *result = mysql_store_result(mysql);
    if (result == NULL) {
        printf("mysql_store_result failed: %s\n", mysql_error(mysql));
    }
    else{
        // int num_fields = mysql_field_count(m_mysql);
        MYSQL_ROW res;
        while (res = mysql_fetch_row(result)){
            std::string name(res[0]);
            std::string passwd(res[1]);
            m_db_users[name] = passwd;
        }
    }
}

bool HttpConn::read_data(){

    // 缓冲区溢出
    if (m_read_idx >= READ_BUFFER_SIZE){
        printf("Read_buffer overflow\n");
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
        close(m_sock_fd);
        --m_user_cnt;
        return false;
    }

    if (m_trigger_mode == TRIGGER_LT){

        int len = recv(m_sock_fd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (len == 0){
            printf("Client is disconnected.\n");
            epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
            close(m_sock_fd);
            --m_user_cnt;
            return false;
        }
        else if (len < 0){
            perror("LT read");
            epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
            close(m_sock_fd);
            m_user_cnt--;
            return false;
        }
        m_read_idx += len;

    }
    else if (m_trigger_mode == TRIGGER_ET){
        
        while (true){
            int len = recv(m_sock_fd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (len == 0){
                printf("Client is disconnected.\n");
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
                close(m_sock_fd);
                --m_user_cnt;
                return false;
            }
            else if (len < 0){
                if (errno == EAGAIN || errno == EWOULDBLOCK){
                    return true;
                }
                //出现错误
                perror("ET read");
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
                close(m_sock_fd);
                m_user_cnt--;
                return false;
            }
            m_read_idx += len;
            //overflow
            if (m_read_idx >= READ_BUFFER_SIZE){
                printf("Read_buffer overflow\n");
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
                close(m_sock_fd);
                --m_user_cnt;
                return false;
            }
        }
    }
    return true;
}

void HttpConn::reset_state(){

    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_check_state = CheckState::request_line;
    //m_trigger_mode = TRIGGER_LT;
    m_request = HttpRequest{};

    m_read_write = READ_MODE;

    m_write_idx = 0;
    memset(&m_file_stat, 0, sizeof(m_file_stat));

    m_iovec_bytes_left = 0;
    m_iovec_bytes_sent = 0;
}

/*
 *  主状态机
 *  解析请求报文
 */ 
HttpConn::HttpCode HttpConn::process_request(){
    
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
                            printf("NO CONTENT. Processing request...\n");
                            return handle_route();
                        }
                        // 有content的情况会自动跳转到content状态
                        break;
                    }
                    case CheckState::content:{
                        ret = parse_content(txt);
                        // 解析完成
                        if (ret == HttpCode::get_request){
                            printf("FIND CONTENT. Processing request...\n");
                            return handle_route();
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
    url = strpbrk(s, " \t");
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
    // printf("Header:\n%s\n", s);
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
    else{
        // printf("Ignore header: %s\n", s);
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
HttpConn::HttpCode HttpConn::handle_route(){

    char* p = m_request.route;
    char dst[256]  = "";
    strcat(dst, m_static_path);
    if (m_request.method == Method::post){
        // 提取用户名和密码
        // content格式是application/x-www-form-urlencoded
        // user=xxx&password=xxx
        char* s = m_request.content;
        char name[100], password[100];
        char *token = strtok(s, "&");

        while (token != NULL){
            char *eq = strchr(token, '=');
            *eq = '\0';

            if (strcmp(token, "user") == 0) {
                strcpy(name, eq + 1);
            } 
            else if (strcmp(token, "password") == 0) {
                strcpy(password, eq + 1);
            }
            token = strtok(NULL, "&");
        }

        //log
        if (*(p + 1) == '2'){
            if (m_db_users.find(name) != m_db_users.end()){
                strcat(dst, "/welcome.html");
            }
            else{
                strcat(dst, "/logError.html");
            }
        }
        //register
        else if (*(p + 1) == '3'){
            if (m_db_users.find(name) == m_db_users.end()){
                char sql_insert[200];
                strcpy(sql_insert, "insert into users (username, password) values ('");
                strcat(sql_insert, name);
                strcat(sql_insert, "', '");
                strcat(sql_insert, password);
                strcat(sql_insert, "')");
                mysql_query(m_mysql, sql_insert);
                m_db_lock.lock();
                m_db_users[std::string(name)] = std::string(password);
                m_db_lock.unlock();
                // strcat(dst, "/welcome.html");
                strcat(dst, "/log.html");
            }
            else{
                strcat(dst, "/registerError.html");
            }
        }
    }
    else if (strncasecmp(p, "/register", 9) == 0){
        strcat(dst, p);
        strcat(dst, ".html");
    }
    else if (strncasecmp(p, "/login", 6) == 0){
        strcat(dst, "/log.html");
    }
    else if (strncasecmp(p, "/picture", 8) == 0){
        strcat(dst, p);
        strcat(dst, ".html");
    }
    else if (strncasecmp(p, "/video", 6) == 0){
        strcat(dst, p);
        strcat(dst, ".html");
    }
    else if (strncasecmp(p, "/fans", 4) == 0){
        strcat(dst, p);
        strcat(dst, ".html");
    }
    else{
        snprintf(dst + strlen(m_static_path), 256, "%s", p);
    }

    printf("target file path:%s\n", dst);

    // 文件不存在
    if (stat(dst, &m_file_stat) == -1)
        return HttpCode::no_resource;
    
    // 没有读权限
    if (!(m_file_stat.st_mode & S_IROTH))
        return HttpCode::forbidden_request;
    
    if (S_ISDIR(m_file_stat.st_mode))
        return HttpCode::bad_request;  

    int fd = open(dst, O_RDONLY);
    m_file_addr = (char *)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return HttpCode::file_request;
}

bool HttpConn::process_response(HttpCode state){

    switch(state){

        case HttpCode::internal_error:{
            add_status_line(500, STATUS_500);
            add_content_length(strlen(STATUS_500_MSG));
            add_content_type();
            add_connection_status_header();
            add_blank_line();
            if (!add_content(STATUS_500_MSG))
                return false;
            break;
        }
        case HttpCode::bad_request:{
            add_status_line(400, STATUS_400);
            add_content_length(strlen(STATUS_400_MSG));
            add_content_type();
            add_connection_status_header();
            add_blank_line();
            if (!add_content(STATUS_400_MSG))
                return false;
            break;
        }
        case HttpCode::forbidden_request:{
            add_status_line(403, STATUS_403);
            add_content_length(strlen(STATUS_403_MSG));
            add_content_type();
            add_connection_status_header();
            add_blank_line();
            if (!add_content(STATUS_403_MSG))
                return false;
            break;
        }
        case HttpCode::file_request:{
            add_status_line(200, STATUS_200);

            if (m_file_stat.st_size != 0){

                add_content_type();
                add_content_length(m_file_stat.st_size);
                add_connection_status_header();
                add_blank_line();
                m_iovec[0].iov_base = m_write_buffer;
                m_iovec[0].iov_len = m_write_idx;
                m_iovec[1].iov_base = m_file_addr;
                m_iovec[1].iov_len = m_file_stat.st_size;
                m_iovev_cnt = 2;
                m_iovec_bytes_left = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else{
                return true;
            }
            break;
        }
        default:
            return false;
    }
    // 非请求文件
    m_iovec[0].iov_base = m_write_buffer;
    m_iovec[0].iov_len = m_write_idx;
    m_iovec_bytes_left = m_write_idx;
    m_iovev_cnt = 1;

    return true;
}

bool HttpConn::add_response(const char* format, ...){
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buffer + m_write_idx, WRITE_BUFFER_SIZE - m_read_idx, format, arg_list);
    //空间不足，内容被截断
    if (len >= WRITE_BUFFER_SIZE - m_read_idx){
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    return true;
}

bool HttpConn::add_status_line(int status, const char* reason_phrase){

    return add_response("HTTP/1.1 %d %s\r\n", status, reason_phrase);
}


bool HttpConn::add_connection_status_header(){

    return add_response("Connection:%s\r\n", m_request.keep_alive == true ? "keep-alive" : "close");
}

bool HttpConn::add_blank_line(){
    
    return add_response("\r\n");
}

bool HttpConn::add_content_length(int len){

    return add_response("Content-Length:%d\r\n", len);
}

bool HttpConn::add_content_type(){

    return add_response("Content-type:%s\r\n", "text/html");
}

bool HttpConn::add_content(const char *txt){
    
    return add_response("%s", txt);
}

bool HttpConn::process_http(){

    if (m_read_write == READ_MODE){
        //读取Http报文
        if (!read_data())
            return false;
        HttpCode ret = process_request();
        // 仍有未收到的请求报文
        // 为了避免出现两个线程竞争一个报文的情况，设置为EPOLLONESHOT，但也因此需要再注册一次
        if (ret == HttpCode::no_request){
            register_epoll(EPOLLIN);
            return true;
        }
        
        switch(ret){
            case HttpCode::no_request:
                printf("no_request\n");
                break;
            case HttpCode::file_request:
                printf("file_request\n");
                break;
            case HttpCode::internal_error:
                printf("internal_error\n");
                break;
            case HttpCode::bad_request:
                printf("bad_request\n");
                break;
            case HttpCode::forbidden_request:
                printf("forbidden_request\n");
                break;
            case HttpCode::get_request:
                printf("get_request\n");
                break;
            case HttpCode::no_resource:
                printf("no_resource\n");
                break;
            default:
                printf("default %d\n", static_cast<int>(ret));
                break;
        }

        if (!process_response(ret)){
            printf("Write buffer overflow\n");
            printf("Close sock %d\n", m_sock_fd);
            epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_sock_fd, 0);
            close(m_sock_fd);
            --m_user_cnt;
            return false;
        }
        m_read_write = WRITE_MODE;
        register_epoll(EPOLLOUT);
    }
    else if (m_read_write == WRITE_MODE){
        printf("Processing response...\n");
        printf("Response:\n%s\n", m_write_buffer);
        process_write();
    }

    return true;
}

void HttpConn::register_epoll(int ev){
    struct epoll_event event;
    event.data.fd = m_sock_fd;
    if (ev == EPOLLIN){
        if (m_trigger_mode == TRIGGER_ET)
            event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        else if (m_trigger_mode == TRIGGER_LT)
            event.events = EPOLLIN | EPOLLONESHOT;
    }
    else if (ev == EPOLLOUT){
        if (m_trigger_mode == TRIGGER_ET)
            event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
        else if (m_trigger_mode == TRIGGER_LT)
            event.events = EPOLLOUT | EPOLLONESHOT;
    }

    epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_sock_fd, &event);
}

bool HttpConn::process_write(){
    int len = 0;

    //长连接状态下，发送完响应报文等待client后续请求
    if (m_iovec_bytes_left == 0 && m_request.keep_alive){
        register_epoll(EPOLLIN);
        return true;
    }

    while(true){
        len = writev(m_sock_fd, m_iovec, m_iovev_cnt);

        if(len < 0){
            // 发送缓冲区满
            if (errno == EAGAIN){
                printf("Kernal write buffer is full\n");
                register_epoll(EPOLLOUT);
                return true;
            }
            // 发生其他错误
            perror("write buffer");
            munmap(m_file_addr, m_file_stat.st_size);
            m_file_addr = 0;
            return false;
        }
        m_iovec_bytes_left -= len;
        m_iovec_bytes_sent += len;
        //响应报文的 首行 header \r\n已经发完
        if (m_iovec_bytes_sent >= m_iovec[0].iov_len){
            m_iovec[0].iov_len = 0;
            m_iovec[1].iov_base = m_file_addr + m_iovec_bytes_sent - m_write_idx;
            m_iovec[1].iov_len = m_iovec_bytes_left;
        }
        else{
            m_iovec[0].iov_base = m_write_buffer + m_iovec_bytes_sent;
            m_iovec[0].iov_len = m_write_idx - m_iovec_bytes_sent;
        }
        if (m_iovec_bytes_left <= 0){

            munmap(m_file_addr, m_file_stat.st_size);
            m_file_addr = 0;
            register_epoll(EPOLLIN);

            if (m_request.keep_alive){
                reset_state();
                return true;
            }
            else{
                return false;
            }
        }
    }
}