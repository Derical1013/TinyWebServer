#ifndef SEM_H
#define SEM_H

#include <semaphore.h>
#include <string.h>
#include <string>
#include <stdexcept>
#include <cstdio>

class Sem{
    private:
        sem_t m_sem;
    public:
        Sem(){
            int ret = sem_init(&m_sem, 0, 0);
            if (ret != 0){
                throw std::runtime_error(std::string("Creating semaphore failed: ") + strerror(errno));
            }
        }
        Sem(int value){
            int ret = sem_init(&m_sem, 0, value);
            if (ret != 0){
                throw std::runtime_error(std::string("Creating semaphore failed: ") + strerror(errno));
            }
        }
        ~Sem(){
            int ret = sem_destroy(&m_sem);
            if (ret != 0){
                fprintf(stderr, "Destroying semaphore failed: %s\n", strerror(errno));
            }
        }
        void wait(){
            int ret = sem_wait(&m_sem);
            if (ret != 0){
                throw std::runtime_error(std::string("Sem_wait failed: ") + strerror(errno));
            }
        }
        void post(){
            int ret = sem_post(&m_sem);
            if (ret != 0){
                throw std::runtime_error(std::string("Sem_post failed: ") + strerror(errno));
            }
        }
};











#endif