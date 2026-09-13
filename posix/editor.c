#include "daw.h"
#include "player.h"
#include "controls.h"
#include "assets.h"
#include "util.h"
#include "json_write.h"
#include "score_view_json.h"
#include "webui.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { MAX_SOURCE = 8 * 1024 * 1024 };

// WebUI callbacks borrow their event until the main thread has answered it.
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t answered = PTHREAD_COND_INITIALIZER;
static webui_event_t *pending;
static int closing;
static volatile sig_atomic_t stopped;

typedef struct {
	Session session;
	Modules modules;
	ScoreView score;
	unsigned revision;
	Player *player;
	int playing;
	Controls controls;
	const char *entry;
	char *error;
	double time;
} Editor;

static void stop(int signal) {
	(void)signal;
	stopped = 1;
}

static void request(webui_event_t *event) {
	pthread_mutex_lock(&mutex);
	if (closing || pending) {
		webui_return_string(event, "{\"error\":\"Editor busy or closing\"}");
	} else {
		pending = event;
		while (pending == event)
			pthread_cond_wait(&answered, &mutex);
	}
	pthread_mutex_unlock(&mutex);
}

static const char *pause_editor(Editor *editor) {
	const char *error = NULL;
	if (editor->player) {
		if (player_pause(editor->player)) {
			error = editor->session.error;
			// Uninit still joins the callback if stopping the device failed.
			player_free(editor->player);
			editor->player = NULL;
		}
		editor->time = (double)editor->session.time / editor->session.sample_rate;
	}
	editor->playing = 0;
	return error;
}

// The device is stopped; its borrowed Session keeps the same address across successful Runs.
static const char *play_editor(Editor *editor) {
	Session *s = &editor->session;
	if (editor->player ? player_rewind(editor->player) : session_rewind(s))
		return s->error ? s->error : "Cannot rewind score";
	if (!editor->player)
		editor->player = player_new(s);
	if (!editor->player || player_start(editor->player)) {
		const char *error = s->error ? s->error : "Audio startup failed";
		pause_editor(editor);
		return error;
	}
	editor->playing = 1;
	editor->time = 0;
	free(editor->error);
	editor->error = NULL;
	return NULL;
}

static void finish(Editor *editor) {
	pause_editor(editor);
	player_free(editor->player);
	editor->player = NULL;
	controls_close(&editor->controls, &editor->session);
	session_free(&editor->session);
}

static const char *read_text(const char *path, char **text) {
	struct stat info;
	if (stat(path, &info))
		return strerror(errno);
	if (!S_ISREG(info.st_mode))
		return "Path is not a regular file";
	FILE *file = fopen(path, "rb");
	if (!file)
		return strerror(errno);
	long size = -1;
	if (!fseek(file, 0, SEEK_END))
		size = ftell(file);
	const char *error = "Cannot read file (at most 8 MiB of text)";
	if (size >= 0 && size <= MAX_SOURCE && !fseek(file, 0, SEEK_SET)) {
		*text = malloc((size_t)size + 1);
		if (*text && fread(*text, 1, size, file) == (size_t)size && !ferror(file) && !memchr(*text, 0, size)) {
			(*text)[size] = 0;
			error = NULL;
		}
	}
	fclose(file);
	if (error) {
		free(*text);
		*text = NULL;
	}
	return error;
}

static const char *save_text(const char *path, const char *text) {
	// Follow an existing symlink and preserve permissions; publish only a complete write.
	char *resolved = realpath(path, NULL);
	const char *target = resolved ? resolved : path;
	char *temporary = malloc(strlen(target) + sizeof(".XXXXXX"));
	if (!temporary) {
		free(resolved);
		return "Out of memory";
	}
	sprintf(temporary, "%s.XXXXXX", target);
	struct stat info;
	int exists = !stat(target, &info), fd = mkstemp(temporary), result = -1;
	if (fd >= 0) {
		FILE *file = fdopen(fd, "wb");
		if (file) {
			result = (exists && fchmod(fd, info.st_mode & 0777)) || fputs(text, file) < 0;
			result |= fclose(file) != 0;
		} else {
			close(fd);
		}
		if (!result)
			result = rename(temporary, target);
		if (result)
			unlink(temporary);
	}
	free(temporary);
	free(resolved);
	return result ? "Cannot save file; the previous file is preserved" : NULL;
}

static double decimal(webui_event_t *request, int index) {
	const char *value = webui_get_string_at(request, index);
	char *end;
	double result = strtod(value, &end);
	return end == value || *end ? NAN : result;
}

static void reply(
    webui_event_t *event, Editor *editor, const char *error, const char *text, const char *path, int score) {
	const char *op = webui_get_string_at(event, 0);
	double time = editor->playing ? player_time(editor->player) : editor->time;
	char *view = NULL;
	int query = !strcmp(op, "range") || !strcmp(op, "note");
	if (query)
		view = score_view_json(&editor->score, editor->revision, op, decimal(event, 1), decimal(event, 2),
		    decimal(event, 3), decimal(event, 4), decimal(event, 5), decimal(event, 6));
	else if (score || !strcmp(op, "status"))
		view = score_view_json(
		    &editor->score, editor->revision, score ? "score" : "status", time, editor->playing, 0, 0, 0, 0);
	if (!view && (query || score || !strcmp(op, "status")))
		error = "Out of memory";
	Json json = {0};
	json_print(&json, "{\"text\":");
	json_string(&json, text);
	json_print(&json, ",\"path\":");
	json_string(&json, path);
	json_print(&json, ",\"nativeOpen\":[");
	for (int i = 0, count = 0; i < editor->session.nnodes; ++i)
		if (editor->controls.native[i])
			json_print(&json, "%s%d", count++ ? "," : "", i);
	json_print(&json, "]");
	if (score) {
		json_print(&json, ",\"nativeAvailable\":[");
		for (int i = 0, count = 0; i < editor->session.nnodes; ++i)
			if (ui_available(editor->session.nodes + i))
				json_print(&json, "%s%d", count++ ? "," : "", i);
		json_print(&json, "]");
	}
	json_print(&json, ",\"prepared\":%s,\"playing\":%s,\"time\":%.17g,\"revision\":%u,\"view\":%s,\"error\":",
	    editor->session.sealed ? "true" : "false", editor->playing ? "true" : "false", time, editor->revision,
	    view ? view : "null");
	json_string(&json, error);
	json_print(&json, "}");
	webui_return_string(event, json.failed ? "{\"error\":\"Out of memory\"}" : json.data);
	free(json.data);
	free(view);
}

static void command(Editor *editor, webui_event_t *event) {
	const char *op = webui_get_string_at(event, 0);
	if (!strcmp(op, "listen")) {
		double revision = decimal(event, 1), track = decimal(event, 2), flags = decimal(event, 3);
		const char *error = NULL;
		if (!editor->session.sealed || revision != editor->revision)
			error = "Stale track view";
		else if (!isfinite(track) || track < 0 || track >= editor->session.ntracks || track != floor(track) ||
		    !isfinite(flags) || flags < 0 || flags > (TRACK_MUTE | TRACK_SOLO) || flags != floor(flags) ||
		    session_listen(&editor->session, track, flags))
			error = "Invalid track state";
		reply(event, editor, error, NULL, "", 0);
		return;
	}
	if (!strcmp(op, "watch") || !strcmp(op, "controls") || !strcmp(op, "parameter") || !strcmp(op, "message")) {
		controls_command(&editor->controls, &editor->session, &editor->score, editor->revision, event);
		return;
	}
	if (!strcmp(op, "range") || !strcmp(op, "note")) {
		reply(event, editor, NULL, NULL, "", 0);
		return;
	}
	const char *path = webui_get_string_at(event, 1);
	const char *source = webui_get_string_at(event, 2), *error = NULL;
	char *text = NULL, *diagnostics = NULL;
	int changed = 0;
	if (!*path)
		path = editor->entry;
	if (webui_get_size_at(event, 2) > MAX_SOURCE) {
		error = "Score too large (at most 8 MiB)";
	} else if (!strcmp(op, "open")) {
		error = read_text(path, &text);
	} else if (!strcmp(op, "save")) {
		error = save_text(path, source);
	} else if (!strcmp(op, "run")) {
		error = pause_editor(editor);
		free(editor->error);
		editor->error = NULL;
		double previous_time = editor->time;
		Session *next = calloc(1, sizeof(*next));
		Output output;
		ScoreView score = {0};
		if (!next)
			error = "Out of memory";
		if (!error) {
			next->sample_rate = 48000;
			next->modules = &editor->modules;
			if (prepare_score(next, &output, path, source, &diagnostics, &score))
				error = diagnostics ? diagnostics : next->error ? next->error : "Preparation failed";
		}
		if (!error) {
			controls_close(&editor->controls, &editor->session);
			session_free(&editor->session);
			editor->session = *next;
			*next = (Session){0};
			error = play_editor(editor);
			if (error)
				finish(editor);
			else {
				score_view_free(&editor->score);
				editor->score = score;
				score = (ScoreView){0};
				++editor->revision;
				assets_set(&editor->score, editor->revision);
				changed = 1;
			}
		}
		if (next)
			session_free(next);
		free(next);
		score_view_free(&score);
		if (error)
			editor->time = previous_time;
	} else if (!strcmp(op, "play")) {
		error = pause_editor(editor);
		if (!error)
			error = editor->session.sealed ? play_editor(editor) : "Run a score before playing";
	} else if (!strcmp(op, "stop")) {
		error = pause_editor(editor);
	} else if (!strcmp(op, "status")) {
		error = editor->error;
	} else {
		error = "Unknown command";
	}
	reply(event, editor, error, text, path, changed);
	free(diagnostics);
	free(text);
}

static void poll_player(Editor *editor) {
	if (!editor->session.sealed)
		return;
	const char *error = NULL;

	if (controls_poll(&editor->controls, &editor->session) < 0)
		error = "Plugin UI control error";
	if (!editor->playing) {
		session_sync(&editor->session);
		if (error) {
			free(editor->error);
			editor->error = copy_string(error);
		}
		return;
	}
	int status = player_status(editor->player);
	if (status < 0)
		error = editor->session.error ? editor->session.error : "Audio error";
	if (!status && !ma_device_is_started(&editor->player->device))
		error = "Audio device stopped";
	if (error || status) {
		free(editor->error);
		editor->error = error ? copy_string(error) : NULL;
		pause_editor(editor);
	}
}

int main(int argc, char **argv) {
	int serve = argc > 1 && !strcmp(argv[1], "--serve"), arg = 1 + serve;
	if (argc > arg + 1) {
		fprintf(stderr, "Usage: %s [--serve] [score.janet]\n", argv[0]);
		return 1;
	}
	Editor editor = {.entry = argc > arg ? argv[arg] : "examples/gui.janet"};
	size_t window = webui_new_window();
	webui_set_timeout(0);
	webui_set_public(window, false);
	if (!webui_set_root_folder(window, "editor")) {
		fputs("Run from the repository root: cannot find editor/\n", stderr);
		return 1;
	}
	webui_bind(window, "command", request);
	webui_set_file_handler(window, assets_read);
	webui_set_size(window, 1000, 760);
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	int shown = webui_show_browser(window, "native.html", serve ? NoBrowser : AnyBrowser);
	if (!serve && !shown) {
		fputs("Cannot open the editor browser. Try --serve and open its URL.\n", stderr);
		stopped = 1;
	}
	printf("Editor: %s/native.html\n", webui_get_url(window));
	fflush(stdout);
	int connected = shown;
	while (!stopped) {
		int visible = webui_is_shown(window);
		if (connected && !visible)
			break;
		connected |= visible;
		pthread_mutex_lock(&mutex);
		webui_event_t *event = pending;
		pthread_mutex_unlock(&mutex);
		if (event) {
			command(&editor, event);
			pthread_mutex_lock(&mutex);
			pending = NULL;
			pthread_cond_broadcast(&answered);
			pthread_mutex_unlock(&mutex);
		}
		poll_player(&editor);
		nanosleep(&(struct timespec){.tv_nsec = 16000000}, NULL);
	}
	pthread_mutex_lock(&mutex);
	closing = 1;
	pending = NULL;
	pthread_cond_broadcast(&answered);
	pthread_mutex_unlock(&mutex);
	finish(&editor);
	modules_free(&editor.modules);
	score_view_free(&editor.score);
	free(editor.error);
	webui_exit();
	webui_clean();
	assets_set(NULL, 0);
	return !serve && !shown;
}
