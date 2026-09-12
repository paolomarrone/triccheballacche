#include "ui.h"
#include "daw.h"
#include "player.h"
#include <signal.h>
#include <stdio.h>
#include <time.h>

static volatile sig_atomic_t stopped;

static void stop(int signal) {
	(void)signal;
	stopped = 1;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s score.janet\n", argv[0]);
		return 1;
	}
	Session session = {.sample_rate = 48000};
	Output cfg;
	UI *views[MAX_NODES] = {0};
	Player *player = NULL;
	int result = 1, count = 0;
	if (load_score(&session, &cfg, argv[1]))
		goto done;
	for (int i = 0; i < session.nnodes; ++i) {
		if (ui_open(views + i, session.nodes + i))
			goto done;
		count += views[i] != NULL;
	}
	if (!count) {
		session.error = "no native Perone UI in this score's bundles";
		goto done;
	}
	if (!(player = player_new(&session)))
		goto done;
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	if (player_start(player))
		goto done;
	printf("Playing with %d plugin UIs; close a window or press Ctrl-C to stop.\n", count);
	fflush(stdout);
	result = 0;
	while (!stopped && !player_status(player)) {
		for (int i = 0; i < session.nnodes; ++i) {
			int status = ui_poll(views[i]);
			if (status) {
				stopped = 1;
				result |= status < 0;
			}
		}
		if (!ma_device_is_started(&player->device)) {
			session.error = "audio device stopped unexpectedly";
			result = 1;
			break;
		}
		nanosleep(&(struct timespec){.tv_nsec = 16000000}, NULL);
	}
	result |= player_status(player) < 0;
done:
	player_free(player);
	for (int i = 0; i < session.nnodes; ++i)
		ui_close(views[i]);
	if (result)
		fprintf(stderr, "UI playback failed%s%s\n", session.error ? ": " : "", session.error ? session.error : "");
	session_free(&session);
	return result;
}
