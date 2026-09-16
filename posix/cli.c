#include "daw.h"
#include "player.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stopped;

static void stop(int sig) {
	(void)sig;
	stopped = 1;
}

static int play_score(Session *s) {
	Player *p = player_new(s);
	if (!p)
		return -1;
	stopped = 0;
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	int result = player_start(p);
	if (!result) {
		puts("Playing; Ctrl-C to stop.");
		fflush(stdout);
		while (!stopped && !player_status(p)) {
			if (!ma_device_is_started(&p->device)) {
				s->error = "audio device stopped unexpectedly";
				result = -1;
				break;
			}
			nanosleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
		}
		result = result || player_status(p) < 0;
	}
	player_free(p);
	return result;
}

int main(int argc, char **argv) {
	if (argc != 3 && argc != 4) {
		fprintf(stderr,
		    "Usage: %s score.janet output.wav [sample-rate]\n"
		    "       %s --play score.janet [sample-rate]\n",
		    argv[0], argv[0]);
		return 1;
	}
	Session session = {0};
	if (argc == 4) {
		char *end;
		unsigned long rate = strtoul(argv[3], &end, 10);
		if (!rate || rate > 384000 || *end) {
			fputs("Sample rate must be within 1..384000 Hz\n", stderr);
			return 1;
		}
		session.sample_rate = (unsigned)rate;
	}
	int play = !strcmp(argv[1], "--play");
	Output cfg;
	int result = load_score(&session, &cfg, argv[play ? 2 : 1]) ||
	    (play ? play_score(&session) : write_score(&session, &cfg, argv[2]));
	if (result)
		fprintf(stderr, "Score/audio failed%s%s\n", session.error ? ": " : "", session.error ? session.error : "");
	else if (!play)
		printf("%.3f seconds, stereo, %s\n", (double)session.frames / session_rate(&session), argv[2]);
	session_free(&session);
	return result;
}
