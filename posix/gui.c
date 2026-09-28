#include "score_janet.h"
#include "player.h"
#include "controls.h"
#include "assets.h"
#include "util.h"
#include "json_write.h"
#include "transport.h"
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
	Transport transport;
	Player *player;
	Controls controls;
	const char *entry;
	char *error;
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
	if (transport_collect(&editor->transport))
		assets_set(&editor->transport.view, editor->transport.control_revision);
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
	}
	accept_revision(editor);
	transport_stopped(&editor->transport);
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
	transport_started(&editor->transport);
	free(editor->error);
	editor->error = NULL;
	return NULL;
}

static const char *seek_editor(Editor *editor, double seconds) {
	Session *s = &editor->session;
	if (!s->audio)
		return "Run a score before seeking";
	uint64_t frame = session_frame(s, seconds);
	if (frame == UINT64_MAX)
		return "Position outside score";
	int playing = editor->transport.playing;
	const char *error = pause_editor(editor);
	if (!error && (editor->player ? player_seek(editor->player, seconds) : session_seek(s, frame)))
		error = s->error;
	if (!error) {
		transport_seeked(&editor->transport);
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
	transport_detach(&editor->transport);
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
	const Score *active = session_score(&editor->session);
	double time = editor->transport.playing ? player_time(editor->player) : editor->transport.time;
	double args[6];
	for (int i = 0; i < 6; ++i)
		args[i] = decimal(event, i + 1);
	char *state = transport_json(&editor->transport, score ? "score" : op, time, args);
	if (!state)
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
			if (ui_available(active->nodes + i))
				json_print(&json, "%s%d", count++ ? "," : "", i);
		json_print(&json, "]");
	}
	json_print(&json, ",\"state\":%s,\"error\":", state ? state : "null");
	json_string(&json, error);
	json_print(&json, "}");
	webui_return_string(event, json.failed ? "{\"error\":\"Out of memory\"}" : json.data);
	free(json.data);
	free(state);
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
		if (!editor->session.audio || revision != editor->transport.revision)
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
		controls_command(&editor->controls, &editor->session, editor->transport.control_revision, event);
		return;
	}
	if (!strcmp(op, "score")) {
		reply(event, editor, NULL, NULL, "", 1);
		return;
	}
	if (!strcmp(op, "range") || !strcmp(op, "origin") || !strcmp(op, "automation")) {
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
		int live = editor->transport.playing && transport_live(&editor->transport);
		if (editor->transport.queued.nnodes)
			error = "Wait for the pending revision to become active";
		free(editor->error);
		editor->error = NULL;
		Score *next = calloc(1, sizeof(*next));
		Session prepared = {.modules = &editor->modules};
		Output output;
		ScoreView score = {0};
		if (!next)
			error = "Out of memory";
		if (!error) {
			next->sample_rate = 48000;
			if (prepare_background(next, &output, path, source, &diagnostics, &score))
				error = diagnostics ? diagnostics : next->error ? next->error : "Preparation failed";
		}
		if (!error && live) {
			if (transport_update(&editor->transport, next, &score, player_position(editor->player)))
				error = next->error;
		} else if (!error) {
			error = pause_editor(editor);
			if (!error && session_activate(&prepared, next))
				error = prepared.error;
		}
		if (!error && !live) {
			controls_close(&editor->controls, &editor->session);
			transport_detach(&editor->transport);
			session_free(&editor->session);
			editor->session = prepared;
			prepared = (Session){0};
			if (editor->player && player_seek(editor->player, 0))
				error = editor->session.error;
			else
				error = play_editor(editor);
			if (error)
				finish(editor);
			else {
				transport_attach(&editor->transport, &editor->session, &score);
				assets_set(&editor->transport.view, editor->transport.control_revision);
				changed = 1;
			}
		}
		if (next)
			score_free(next);
		free(next);
		session_free(&prepared);
		score_view_free(&score);
	} else if (!strcmp(op, "play")) {
		if (!editor->session.audio)
			error = "Run a score before playing";
		else if (!editor->transport.playing) {
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
	if (!editor->session.audio)
		return;
	const char *error = NULL;
	accept_revision(editor);

	if (controls_poll(&editor->controls, &editor->session) < 0)
		error = "Plugin UI control error";
	if (!editor->transport.playing) {
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
	transport_free(&editor.transport);
	free(editor.error);
	webui_exit();
	webui_clean();
	assets_set(NULL, 0);
	return !serve && !shown;
}
