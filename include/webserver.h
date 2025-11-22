#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>


class WebServer{

    public:
        static int constexpr MAX_EVENT = 512;

    private:
        int m_port;
        int m_fd;

    public:
        WebServer();
        ~WebServer();

    private:
        void init(int port);
        void event_listen();

};






#endif
