#include "daw.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	if (argc != 3 && argc != 4) {
		fprintf(stderr, "Usage: %s score.janet output.wav [sample-rate]\n", argv[0]);
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
	Output cfg;
	int result = load_score(&session, &cfg, argv[1]) || write_score(&session, &cfg, argv[2]);
	if (result)
		fprintf(stderr, "Score/render failed%s%s\n", session.error ? ": " : "", session.error ? session.error : "");
	else
		printf("%.3f seconds, stereo, %s\n", (double)session.frames / session_rate(&session), argv[2]);
	session_free(&session);
	return result;
}
