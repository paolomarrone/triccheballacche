#ifndef POSIX_THREAD_PLATFORM_H
#define POSIX_THREAD_PLATFORM_H
#include <pthread.h>
#include <semaphore.h>
typedef pthread_t Thread;
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Condition;
typedef sem_t Semaphore;
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define CONDITION_INITIALIZER PTHREAD_COND_INITIALIZER
#endif
