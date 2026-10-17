#include "native/thread.h"
#include <errno.h>
#include <time.h>
#include <sched.h>

void thread_yield(void) {
	sched_yield();
}

int thread_start(Thread *thread, void *(*run)(void *), void *context) {
	return pthread_create(thread, NULL, run, context);
}
void thread_join(Thread thread) {
	pthread_join(thread, NULL);
}
int mutex_init(Mutex *mutex) {
	return pthread_mutex_init(mutex, NULL);
}
void mutex_destroy(Mutex *mutex) {
	pthread_mutex_destroy(mutex);
}
void mutex_lock(Mutex *mutex) {
	pthread_mutex_lock(mutex);
}
void mutex_unlock(Mutex *mutex) {
	pthread_mutex_unlock(mutex);
}
int condition_init(Condition *condition) {
	return pthread_cond_init(condition, NULL);
}
void condition_destroy(Condition *condition) {
	pthread_cond_destroy(condition);
}
void condition_signal(Condition *condition) {
	pthread_cond_signal(condition);
}
void condition_broadcast(Condition *condition) {
	pthread_cond_broadcast(condition);
}
void condition_wait(Condition *condition, Mutex *mutex) {
	pthread_cond_wait(condition, mutex);
}
int condition_wait_ms(Condition *condition, Mutex *mutex, unsigned milliseconds) {
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += milliseconds / 1000;
	deadline.tv_nsec += (milliseconds % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		++deadline.tv_sec;
		deadline.tv_nsec -= 1000000000L;
	}
	int result = pthread_cond_timedwait(condition, mutex, &deadline);
	return result == ETIMEDOUT ? 1 : result ? -1 : 0;
}
int semaphore_init(Semaphore *semaphore) {
	return sem_init(semaphore, 0, 0);
}
void semaphore_destroy(Semaphore *semaphore) {
	sem_destroy(semaphore);
}
void semaphore_post(Semaphore *semaphore) {
	sem_post(semaphore);
}
void semaphore_wait(Semaphore *semaphore) {
	while (sem_wait(semaphore) && errno == EINTR) {
	}
}
