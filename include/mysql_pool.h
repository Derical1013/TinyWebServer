#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <semaphore.h>
#include <pthread.h>

class ConnPool{

    public:
        void init(int max_conn, const char* host, const char* user, const char* passwd,
                        const char* db, const unsigned int port);
        MYSQL* get_conn();
        void release_conn(MYSQL* conn);
        void destroy_poll();
        
        static ConnPool* get_instance();

    private:
        int m_max_conn;
        int m_cur_conn;
        int m_free_conn;
        sem_t m_sem;
        pthread_mutex_t m_mutex;
        std::queue<MYSQL*> m_conn_q; 

    private:
        ConnPool();
        ~ConnPool();
};

class ConnRaii{
    public:
        ConnRaii(MYSQL** conn, ConnPool* conn_pool);
        ~ConnRaii();
    private:
        MYSQL* m_conn;
        ConnPool* m_conn_pool;
};
















#endif
