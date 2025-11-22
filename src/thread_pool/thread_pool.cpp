#include "../../include/thread_pool.h"


ThreadPool::ThreadPool(int thread_num, int queue_capacity, ConnPool* conn_pool): 
                m_thread_num(thread_num), m_queue_capacity(queue_capacity),
                m_conn_pool(conn_pool)
{

    m_threads = new pthread_t[m_thread_num];
    int ret = 0;
    for (unsigned int i = 0; i < m_thread_num; ++i){
        ret = pthread_create(m_threads + i, NULL, worker, this);
        if (ret != 0){
            delete [] m_threads;
            fprintf(stderr, "thread_creation failed:(i = %d, err_msg = %s)\n", i, strerror(ret));
            exit(EXIT_FAILURE);                                     //退出进程，由os负责回收资源
        }
        // pthread_detach(*(m_threads + i));
    }
}


ThreadPool::~ThreadPool(){
    m_stop = true;
    for (int i = 0; i < m_thread_num; ++i){
        m_queue_sem.post();
    }
    for (int i = 0; i < m_thread_num; ++i){
        pthread_join(*(m_threads + i), NULL);
    }

    // 清理剩余任务（如果有任务没做完）
    while (!m_queue.empty()){
        HttpConn* tmp = m_queue.front();
        m_queue.pop();
        delete tmp;
    }
    delete [] m_threads;
}

/*
 * 为提高性能使用非阻塞
 */
bool ThreadPool::add_task(HttpConn* request){
    m_queue_lock.lock();
    if (m_queue.size() == m_queue_capacity){
        m_queue_lock.unlock();
        return false;
    }
    else{
        m_queue.push(request);
    }
    m_queue_lock.unlock();
    m_queue_sem.post();

    // printf("Successfully add_task\n");
    return true;
}


void* ThreadPool::worker(void *arg){
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();

    return NULL;
};

void ThreadPool::run(){
    // printf("worker %ld\n", pthread_self());
    while(true){
        m_queue_sem.wait();
        if (m_stop){
            return;
        }
        m_queue_lock.lock();
        //work
        HttpConn* request = m_queue.front();
        m_queue.pop();
        m_queue_lock.unlock();
        ConnRaii conn_raii(&request->m_mysql, m_conn_pool);
        request->process_http();
    }
}