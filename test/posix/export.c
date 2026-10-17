#include "test/platform.h"
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

void test_export_write_failure(void (*failed_exports)(size_t, int), size_t block) {
	for (int late = 0; late < 2; ++late) {
		pid_t child = fork();
		assert(child >= 0);
		if (!child) {
			assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
			rlim_t limit = late ? 48 : 128;
			assert(!setrlimit(RLIMIT_FSIZE, &(struct rlimit){limit, limit}));
			failed_exports(late ? 2 : block + 1, 0);
			_exit(0);
		}
		int status;
		assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
}
