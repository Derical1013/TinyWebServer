#include "../../include/mysql_pool.h"



void ConnPool::init(int max_conn, const char* host, const char* user, const char* passwd,
                        const char* db, const unsigned int port){
    m_max_conn = max_conn;
    m_cur_conn = max_conn;
    m_free_conn = max_conn;

    for (int i = 0; i < max_conn; ++i){
        MYSQL* tmp = mysql_init(NULL);
        mysql_real_connect(tmp, host, user, passwd, db, port, NULL, 0);
        m_conn_q.push(tmp);
    }

    sem_init(&m_sem, 0, max_conn);
    pthread_mutex_init(&m_mutex, NULL);
}

// 销毁连接池
// 需要保证所有连接都已经归还
void ConnPool::destroy_poll(){

    sem_destroy(&m_sem);
    pthread_mutex_destroy(&m_mutex);
    while (!m_conn_q.empty()){
        MYSQL* tmp = m_conn_q.front();
        mysql_close(tmp);
        m_conn_q.pop();
    }
}

MYSQL* ConnPool::get_conn(){

    sem_wait(&m_sem);
    pthread_mutex_lock(&m_mutex);
    MYSQL* tmp = m_conn_q.front();
    m_conn_q.pop();
    ++m_cur_conn;
    --m_free_conn;
    pthread_mutex_unlock(&m_mutex);
    return tmp;
}


void ConnPool::release_conn(MYSQL* conn){

    pthread_mutex_lock(&m_mutex);
    m_conn_q.push(conn);
    --m_cur_conn;
    ++m_free_conn;
    pthread_mutex_unlock(&m_mutex);
    sem_post(&m_sem);
}


ConnPool* ConnPool::get_instance(){
    static ConnPool pool;
    return &pool;
}

ConnRaii::ConnRaii(MYSQL** conn, ConnPool* conn_pool){

    *conn = conn_pool->get_conn();
    m_conn = *conn;
    m_conn_pool = conn_pool;
}

ConnRaii::~ConnRaii(){

    m_conn_pool->release_conn(m_conn);
}




