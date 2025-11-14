#ifndef MUTEX_LOCK_H
#define MUTEX_LOCK_H
#include <pthread.h>
#include <string.h>
#include <stdexcept>
#include <cstdio>
#include <string>

class MutexLock{

    public:
        MutexLock(){
            int ret = pthread_mutex_init(&m_mutex, NULL);
            if (ret == -1){
                throw std::runtime_error(std::string("pthread_mutex init failed: ") + strerror(ret));
            }
        };
        ~MutexLock(){
            int ret = pthread_mutex_destroy(&m_mutex);
            if (ret == -1){
                fprintf(stderr, "pthread_mutex destroy failed: %s", strerror(ret));
            }
        };
        void lock(){
            int ret = pthread_mutex_lock(&m_mutex);
            if (ret == -1){
                throw std::runtime_error(std::string("pthread_mutex lock failed: ")  + strerror(ret));
            }
        };
        void unlock(){
            int ret = pthread_mutex_unlock(&m_mutex);
            if (ret == -1){
                throw std::runtime_error(std::string("pthread_mutex unlock failed: ")  + strerror(ret));
            }
        };

    private:
        pthread_mutex_t m_mutex;
};

#endif