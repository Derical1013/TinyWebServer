#include "../../include/thread_pool.h"
#include "../../include/http_conn.h"
#include <stdio.h>
#include <queue>
#include <unordered_map>

#define PORT 9396
#define ARRAY_SIZE 512

int main(){

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0){
        perror("socket creation failed");
        return -1;
    }
    // 设置端口复用
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_port = htons(PORT);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    // inet_pton(AF_INET,"192.168.10.100", &addr.sin_addr);

    int ret = 0;
    ret = bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (ret < 0){
        perror("bind failed");
        return -1;
    }

    ret = listen(fd, 128);
    if (ret < 0){
        perror("listen failed");
        return -1;
    }

    int epfd = epoll_create(1);
    if (epfd == -1){
        perror("epoll_create failed");
        return -1;
    }
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    if (ret < 0){
        perror("epoll add failed");
    }

    char root[64] = "/home/derical/linux_cpp/webserver/TinyWebServer/static";
    char* static_path = (char*)malloc(sizeof(root));
    strcpy(static_path, root);
    ThreadPool thread_pool(4,100);
    std::unordered_map<int, HttpConn*> conns;

    struct epoll_event ev_array[ARRAY_SIZE];
    while(1){
        int num = epoll_wait(epfd, ev_array, ARRAY_SIZE, -1);

        for (int i = 0; i < num; ++i){
            // listen fd
            if (ev_array[i].data.fd == fd && (ev_array[i].events & EPOLLIN)){
                struct sockaddr_in peer_addr;
                socklen_t addrlen = sizeof(peer_addr);
                int peer_fd = accept(fd, (struct sockaddr *)&peer_addr, &addrlen);
                if (peer_fd < 0){
                    perror("accept failed");
                    return -1;
                }else{
                    char ip[INET_ADDRSTRLEN];
                    printf("Client IP: %s\n", inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof(ip)));
                    printf("Client port:%d\n", ntohs(peer_addr.sin_port));
                }

                HttpConn* request = new HttpConn();
                conns[peer_fd] = request;
                request->m_user_cnt++;
                request->init_connection(peer_fd, peer_addr, static_path, epfd, HttpConn::TRIGGER_ET);
            }
            // conn fd
            else if (ev_array[i].events & EPOLLIN){
                int fd = ev_array[i].data.fd;
                HttpConn* request = conns[fd];
                thread_pool.add_task(request);
            }
            else if (ev_array[i].events & EPOLLOUT){
                printf("EPOLLOUT triggered\n");
                int fd = ev_array[i].data.fd;
                HttpConn* request = conns[fd];
                thread_pool.add_task(request);
            }
        }
    }

    free(static_path);
    close(fd);
}
