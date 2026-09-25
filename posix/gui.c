#include "daw.h"
#include "player.h"
#include "controls.h"
#include "assets.h"
#include "util.h"
#include "json_write.h"
#include "score_view_json.h"
#include "files.h"
#include "prepare.h"
#include "webui.h"
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// WebUI callbacks borrow their event until the main thread has answered it.
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t answered = PTHREAD_COND_INITIALIZER;
static webui_event_t *pending;
static int closing;
static int frontend_ready;
static volatile sig_atomic_t stopped;

typedef struct {
	Session session;
	Modules modules;
	ScoreView score, queued;
	unsigned revision, control_revision;
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

static bool close_window(size_t window) {
	if (stopped || !frontend_ready) {
		stopped = 1;
		return true;
	}
	webui_run(window, "window.dispatchEvent(new Event('close-request'))");
	return false;
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

static void accept_revision(Editor *editor) {
	unsigned revision = atomic_load(&editor->session.revision);
	if (editor->queued.nnodes && revision > editor->revision) {
		score_view_free(&editor->score);
		editor->score = editor->queued;
		editor->queued = (ScoreView){0};
		editor->revision = revision;
		score_view_activate(&editor->score, &editor->session);
		assets_set(&editor->score, editor->control_revision);
		session_collect(&editor->session);
	}
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
	accept_revision(editor);
	session_cancel(&editor->session);
	score_view_free(&editor->queued);
	return error;
}

// The device is stopped; its borrowed Session keeps the same address across successful Runs.
static const char *play_editor(Editor *editor) {
	Session *s = &editor->session;
	if (!editor->player)
		editor->player = player_new(s);
	if (!editor->player || player_start(editor->player)) {
		const char *error = s->error ? s->error : "Audio startup failed";
		pause_editor(editor);
		return error;
	}
	editor->playing = 1;
	editor->time = (double)s->time / s->sample_rate;
	free(editor->error);
	editor->error = NULL;
	return NULL;
}

static const char *seek_editor(Editor *editor, double seconds) {
	Session *s = &editor->session;
	double frame = seconds * s->sample_rate;
	if (!s->sealed)
		return "Run a score before seeking";
	if (!isfinite(frame) || frame < 0 || frame >= 0x1p53 || (uint64_t)llround(frame) > s->frames)
		return "Position outside score";
	int playing = editor->playing;
	const char *error = pause_editor(editor);
	if (!error && (editor->player ? player_seek(editor->player, seconds) : session_seek(s, llround(frame))))
		error = s->error;
	editor->time = (double)s->time / s->sample_rate;
	if (!error) {
		score_view_activate(&editor->score, s);
		free(editor->error);
		editor->error = NULL;
		if (playing)
			error = play_editor(editor);
	}
	return error;
}

static void finish(Editor *editor) {
	pause_editor(editor);
	player_free(editor->player);
	editor->player = NULL;
	controls_close(&editor->controls, &editor->session);
	session_free(&editor->session);
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
	json_print(&json, ",\"controlRevision\":%u", editor->control_revision);
	json_print(&json, ",\"queued\":%s,\"live\":%s", editor->queued.nnodes ? "true" : "false",
	    editor->session.sequence ? "true" : "false");
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
	frontend_ready = 1;
	accept_revision(editor);
	const char *op = webui_get_string_at(event, 0);
	if (!strcmp(op, "close")) {
		stopped = 1;
		webui_return_string(event, "{}");
		return;
	}
	if (!strcmp(op, "library") || !strcmp(op, "files")) {
		char *json = !strcmp(op, "library") ? library_json() : directory_json(webui_get_string_at(event, 1));
		webui_return_string(event, json ? json : "{\"error\":\"Cannot read library\"}");
		free(json);
		return;
	}
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
	if (!strcmp(op, "seek")) {
		const char *error = seek_editor(editor, decimal(event, 1));
		reply(event, editor, error, NULL, "", 0);
		return;
	}
	if (!strcmp(op, "watch") || !strcmp(op, "controls") || !strcmp(op, "parameter") || !strcmp(op, "message")) {
		controls_command(&editor->controls, &editor->session, &editor->score, editor->control_revision, event);
		return;
	}
	if (!strcmp(op, "score")) {
		reply(event, editor, NULL, NULL, "", 1);
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
		error = file_read(path, &text);
	} else if (!strcmp(op, "save")) {
		error = file_save(path, source);
	} else if (!strcmp(op, "run")) {
		int live = editor->playing && editor->session.sequence;
		if (editor->queued.nnodes)
			error = "Wait for the pending revision to become active";
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
			next->describe = 1;
			next->modules = &editor->modules;
			if (prepare_background(next, &output, path, source, &diagnostics, &score))
				error = diagnostics ? diagnostics : next->error ? next->error : "Preparation failed";
		}
		if (!error && live) {
			int mapping[MAX_NODES];
			if (player_update(editor->player, next, editor->revision + 1, mapping))
				error = next->error;
			else {
				score_view_remap(&score, mapping);
				editor->queued = score;
				score = (ScoreView){0};
			}
		} else if (!error) {
			error = pause_editor(editor);
			if (!error && session_activate(next))
				error = next->error;
		}
		if (!error && !live) {
			controls_close(&editor->controls, &editor->session);
			session_free(&editor->session);
			editor->session = *next;
			*next = (Session){0};
			if (editor->player && player_seek(editor->player, 0))
				error = editor->session.error;
			else
				error = play_editor(editor);
			if (error)
				finish(editor);
			else {
				score_view_free(&editor->score);
				editor->score = score;
				score = (ScoreView){0};
				++editor->revision;
				editor->control_revision = editor->revision;
				assets_set(&editor->score, editor->control_revision);
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
		if (!editor->session.sealed)
			error = "Run a score before playing";
		else if (!editor->playing) {
			if (editor->session.time == editor->session.frames)
				error = seek_editor(editor, 0);
			if (!error)
				error = play_editor(editor);
		}
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
	accept_revision(editor);

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
	webui_set_config(show_wait_connection, false);
	webui_set_public(window, false);
	if (!webui_set_root_folder(window, "editor")) {
		fputs("Run from the repository root: cannot find editor/\n", stderr);
		return 1;
	}
	webui_bind(window, "command", request);
	webui_set_file_handler(window, assets_read);
	webui_set_size(window, 1000, 760);
	webui_set_icon_file(window, "editor/icon.svg");
	webui_set_close_handler_wv(window, close_window);
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	int shown = serve ? *webui_start_server(window, "native.html") != 0 : webui_show_wv(window, "native.html");
	// The WebView toolkit may initialize the locale; score numbers and JSON use decimal points.
	setlocale(LC_NUMERIC, "C");
	if (!serve && !shown) {
		fputs("Cannot open the native editor. Install the WebView runtime or use --serve.\n", stderr);
		stopped = 1;
	}
	printf("Editor: http://localhost:%zu/native.html\n", webui_get_port(window));
	fflush(stdout);
	int connected = 0;
	while (!stopped && webui_wait_async()) {
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
	score_view_free(&editor.queued);
	free(editor.error);
	webui_exit();
	webui_clean();
	assets_set(NULL, 0);
	return !serve && !shown;
}
