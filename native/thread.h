#ifndef NATIVE_THREAD_H
#define NATIVE_THREAD_H
// The build selects posix/ or win32/ for this private platform header.
#include <thread_platform.h>

int thread_start(Thread *thread, void *(*run)(void *), void *context);
void thread_join(Thread thread);
void thread_yield(void);
int mutex_init(Mutex *mutex);
void mutex_destroy(Mutex *mutex);
void mutex_lock(Mutex *mutex);
void mutex_unlock(Mutex *mutex);
int condition_init(Condition *condition);
void condition_destroy(Condition *condition);
void condition_signal(Condition *condition);
void condition_broadcast(Condition *condition);
void condition_wait(Condition *condition, Mutex *mutex);
// Returns 1 on timeout, 0 on notification, -1 on error. Mutex remains locked.
int condition_wait_ms(Condition *condition, Mutex *mutex, unsigned milliseconds);
int semaphore_init(Semaphore *semaphore);
void semaphore_destroy(Semaphore *semaphore);
void semaphore_post(Semaphore *semaphore);
void semaphore_wait(Semaphore *semaphore);
#endif
