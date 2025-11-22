#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <queue>
#include <pthread.h>
#include <atomic>
#include "http_conn.h"
#include "mutex_lock.h"
#include "sem.h"
#include "mysql_pool.h"


class ThreadPool{
    public:
        ThreadPool(int thread_num, int queue_capacity, ConnPool* conn_pool);
        ~ThreadPool();
        bool add_task(HttpConn* request);

        //禁止拷贝线程池
        // ThreadPool(const ThreadPool&) = delete;
        // ThreadPool& operator=(const ThreadPool&) = delete;

    private:
        uint32_t m_thread_num;
        uint32_t m_queue_capacity;
        std::queue<HttpConn*> m_queue;
        pthread_t *m_threads;
        MutexLock m_queue_lock;
        Sem m_queue_sem;
        std::atomic<bool> m_stop{false};
        //连接池
        ConnPool* m_conn_pool;

    private:
        /*
         * 由于类成员自带this指针作为参数，需要增加static修饰符
         * 从语义上来说，static依然是类成员，可以访问private和protect,作用域在类内
         * 
         */
        static void* worker(void *arg);
        void run();
};




#endif