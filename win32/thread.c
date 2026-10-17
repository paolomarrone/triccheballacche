#include "native/thread.h"
#include <process.h>
#include <stdlib.h>

void thread_yield(void) {
	SwitchToThread();
}

typedef struct {
	void *(*run)(void *);
	void *context;
} Start;
static unsigned __stdcall run_thread(void *context) {
	Start start = *(Start *)context;
	free(context);
	start.run(start.context);
	return 0;
}
int thread_start(Thread *thread, void *(*run)(void *), void *context) {
	Start *start = malloc(sizeof(*start));
	if (!start)
		return -1;
	*start = (Start){run, context};
	*thread = (HANDLE)_beginthreadex(NULL, 0, run_thread, start, 0, NULL);
	if (!*thread)
		free(start);
	return *thread ? 0 : -1;
}
void thread_join(Thread thread) {
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
}
int mutex_init(Mutex *mutex) {
	InitializeSRWLock(mutex);
	return 0;
}
void mutex_destroy(Mutex *mutex) {
	(void)mutex;
}
void mutex_lock(Mutex *mutex) {
	AcquireSRWLockExclusive(mutex);
}
void mutex_unlock(Mutex *mutex) {
	ReleaseSRWLockExclusive(mutex);
}
int condition_init(Condition *condition) {
	InitializeConditionVariable(condition);
	return 0;
}
void condition_destroy(Condition *condition) {
	(void)condition;
}
void condition_signal(Condition *condition) {
	WakeConditionVariable(condition);
}
void condition_broadcast(Condition *condition) {
	WakeAllConditionVariable(condition);
}
void condition_wait(Condition *condition, Mutex *mutex) {
	SleepConditionVariableSRW(condition, mutex, INFINITE, 0);
}
int condition_wait_ms(Condition *condition, Mutex *mutex, unsigned milliseconds) {
	if (SleepConditionVariableSRW(condition, mutex, milliseconds, 0))
		return 0;
	return GetLastError() == ERROR_TIMEOUT ? 1 : -1;
}
int semaphore_init(Semaphore *semaphore) {
	*semaphore = CreateSemaphoreW(NULL, 0, LONG_MAX, NULL);
	return *semaphore ? 0 : -1;
}
void semaphore_destroy(Semaphore *semaphore) {
	CloseHandle(*semaphore);
}
void semaphore_post(Semaphore *semaphore) {
	ReleaseSemaphore(*semaphore, 1, NULL);
}
void semaphore_wait(Semaphore *semaphore) {
	WaitForSingleObject(*semaphore, INFINITE);
}
