#include "score_janet.h"
#include "player.h"
#include "controls.h"
#include "assets.h"
#include "util.h"
#include "json_write.h"
#include "transport.h"
#include "files.h"
#include "prepare.h"
#include "midi.h"
#include "discovery.h"
#include "webui.h"
#include <locale.h>
#include <limits.h>
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
	MidiDevices *midi;
	Controls controls;
	unsigned sample_rate;
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
	midi_devices_stop(editor->midi);
	const char *error = NULL;
	if (editor->player) {
		if (player_pause(editor->player)) {
			error = editor->session.error;
			// Uninit still joins the callback if stopping the device failed.
			player_free(editor->player);
			editor->player = NULL;
		}
	}
	session_midi_panic(&editor->session);
	accept_revision(editor);
	transport_stopped(&editor->transport);
	return error;
}

// Reuse the device and its borrowed Session across transport changes and graph replacement.
static const char *start_editor(Editor *editor, int playing) {
	Session *s = &editor->session;
	if (!editor->player)
		editor->player = player_new(s);
	const char *midi_error = editor->player ? midi_devices_start(editor->midi, editor->player) : NULL;
	if (!editor->player || midi_error || player_monitor(editor->player) ||
	    (playing && s->frames && player_start(editor->player))) {
		const char *error = midi_error ? midi_error : s->error ? s->error : "Audio startup failed";
		pause_editor(editor);
		return error;
	}
	if (playing && s->frames)
		transport_started(&editor->transport);
	free(editor->error);
	editor->error = NULL;
	return NULL;
}

// A transport stop leaves the MIDI producer attached and the prepared graph alive.
static const char *stop_editor(Editor *editor) {
	if (!editor->player)
		return NULL;
	int active = player_active(editor->player);
	if (player_pause(editor->player))
		return editor->session.error;
	accept_revision(editor);
	transport_stopped(&editor->transport);
	return active && player_monitor(editor->player) ? editor->session.error : NULL;
}

static const char *seek_editor(Editor *editor, double seconds) {
	Session *s = &editor->session;
	if (!s->audio)
		return "Prepare a score before seeking";
	uint64_t frame = session_frame(s, seconds);
	if (frame == UINT64_MAX)
		return "Position outside score";
	int playing = editor->transport.playing;
	int active = editor->player && player_active(editor->player);
	const char *error = pause_editor(editor);
	if (!error && (editor->player ? player_seek(editor->player, seconds) : session_seek(s, frame)))
		error = s->error;
	if (!error) {
		transport_seeked(&editor->transport);
		free(editor->error);
		editor->error = NULL;
		if (active)
			error = start_editor(editor, playing);
	}
	return error;
}

static void finish(Editor *editor) {
	midi_devices_detach(editor->midi);
	pause_editor(editor);
	player_free(editor->player);
	editor->player = NULL;
	controls_close(&editor->controls, &editor->session);
	transport_detach(&editor->transport);
	session_free(&editor->session);
	assets_set(NULL, 0);
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
	json_print(&json, "{\"audio\":%s,\"text\":", editor->player && player_active(editor->player) ? "true" : "false");
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
	if (!strcmp(op, "library") || !strcmp(op, "library-save") || !strcmp(op, "library-refresh") ||
	    !strcmp(op, "files")) {
		char *json;
		if (!strcmp(op, "files"))
			json = directory_json(webui_get_string_at(event, 1));
		else {
			int save = !strcmp(op, "library-save");
			if (save && webui_get_size_at(event, 1) > 1024 * 1024) {
				webui_return_string(event, "{\"error\":\"Plugin paths are too large\"}");
				return;
			}
			if (strcmp(op, "library"))
				scan_plugins_clear();
			json = library_json(save ? webui_get_string_at(event, 1) : NULL);
		}
		webui_return_string(event, json ? json : "{\"error\":\"Cannot read library\"}");
		free(json);
		return;
	}
	if (!strcmp(op, "audio-info")) {
		Json json = {0};
		json_print(&json,
		    "{\"sampleRate\":%u,\"output\":", editor->player ? editor->player->device.sampleRate : editor->sample_rate);
		json_string(&json, editor->player ? editor->player->device.playback.name : "System default");
		json_print(&json, ",\"latency\":%.6f}",
		    editor->player ? (double)editor->player->latency / session_rate(&editor->session) : 0);
		webui_return_string(event, json.failed ? "{\"error\":\"Cannot read audio device\"}" : json.data);
		free(json.data);
		return;
	}
	if (!strcmp(op, "sample-rate")) {
		double rate = decimal(event, 1);
		const char *error = NULL;
		if (editor->player && player_active(editor->player))
			error = "Turn the engine off before changing sample rate";
		else if (!isfinite(rate) || rate < 1 || rate > 384000 || rate != floor(rate))
			error = "Sample rate must be within 1..384000 Hz";
		else if (rate != editor->sample_rate) {
			finish(editor);
			editor->sample_rate = (unsigned)rate;
			free(editor->error);
			editor->error = NULL;
		}
		reply(event, editor, error, NULL, "", 0);
		return;
	}
	if (!strcmp(op, "midi-devices") || !strcmp(op, "midi-refresh") || !strcmp(op, "midi-connect")) {
		const char *error = NULL;
		int resume = editor->transport.playing;
		int active = editor->player && player_active(editor->player);
		int changing = strcmp(op, "midi-devices");
		if (changing)
			error = pause_editor(editor);
		if (!error && !strcmp(op, "midi-refresh")) {
			midi_devices_free(editor->midi);
			editor->midi = NULL;
		}
		if (!error && !editor->midi)
			editor->midi = midi_devices_new();
		if (!editor->midi)
			error = "Cannot initialize MIDI devices";
		if (!error && !strcmp(op, "midi-connect")) {
			double revision = decimal(event, 1), device = decimal(event, 2), node = decimal(event, 3);
			if (revision != editor->transport.control_revision || !isfinite(device) || device < 0 || device > INT_MAX ||
			    device != floor(device) || !isfinite(node) || node < -1 || node >= editor->session.nnodes ||
			    node != floor(node))
				error = "Invalid or stale MIDI connection";
			else
				error = midi_devices_connect(editor->midi, &editor->session, device, node);
		}
		if (changing && active) {
			const char *start = start_editor(editor, resume);
			if (!error)
				error = start;
		}
		if (error)
			reply(event, editor, error, NULL, "", 0);
		else {
			char *json = midi_devices_json(editor->midi);
			webui_return_string(event, json ? json : "{\"error\":\"Out of memory\"}");
			free(json);
		}
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
	if (!strcmp(op, "audio")) {
		double enabled = decimal(event, 1);
		const char *error = !editor->session.audio ? "Prepare a score before enabling audio"
		    : enabled == 0                         ? pause_editor(editor)
		    : enabled == 1                         ? start_editor(editor, editor->transport.playing)
		                                           : "Invalid audio state";
		reply(event, editor, error, NULL, "", 0);
		return;
	}
	if (!strcmp(op, "seek")) {
		const char *error = seek_editor(editor, decimal(event, 1));
		reply(event, editor, error, NULL, "", 0);
		return;
	}
	if (!strcmp(op, "state-save") || !strcmp(op, "state-load")) {
		int active = editor->player && player_active(editor->player);
		const char *error =
		    decimal(event, 1) != editor->transport.control_revision ? "Stale plugin view" : pause_editor(editor);
		if (error)
			reply(event, editor, error, NULL, "", 0);
		else {
			controls_command(&editor->controls, &editor->session, editor->transport.control_revision, event);
			const char *start = active ? start_editor(editor, 0) : NULL;
			if (start) {
				free(editor->error);
				editor->error = copy_string(start);
			}
		}
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
	if (webui_get_size_at(event, 2) > (!strcmp(op, "preset-file") ? 32 * 1024 * 1024 + 4096 : MAX_SOURCE)) {
		error = "File too large";
	} else if (!strcmp(op, "open")) {
		error = file_read(path, &text);
	} else if (!strcmp(op, "save") || !strcmp(op, "preset-file")) {
		error = file_save(path, source);
	} else if (!strcmp(op, "run") || !strcmp(op, "prepare") || !strcmp(op, "update")) {
		int live = !strcmp(op, "update");
		int playing = !strcmp(op, "run") || editor->transport.playing;
		if (live && editor->transport.queued.nnodes)
			error = "Wait for the pending revision to become active";
		else if (live && !(editor->transport.playing && transport_live(&editor->transport)))
			error = "Live updates require a playing live score";
		free(editor->error);
		editor->error = NULL;
		Score *next = calloc(1, sizeof(*next));
		Session prepared = {.modules = &editor->modules};
		Output output;
		ScoreView score = {0};
		if (!next)
			error = "Out of memory";
		if (!error) {
			next->sample_rate = editor->sample_rate;
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
			midi_devices_detach(editor->midi);
			controls_close(&editor->controls, &editor->session);
			transport_detach(&editor->transport);
			session_free(&editor->session);
			editor->session = prepared;
			prepared = (Session){0};
			transport_attach(&editor->transport, &editor->session, &score);
			if (editor->player && player_seek(editor->player, 0))
				error = editor->session.error;
			else
				error = start_editor(editor, playing);
			if (error)
				finish(editor);
			else {
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
			error = "Prepare a score before playing";
		else if (!editor->transport.playing) {
			if (editor->session.frames && editor->session.time == editor->session.frames)
				error = seek_editor(editor, 0);
			if (!error)
				error = start_editor(editor, 1);
		}
	} else if (!strcmp(op, "stop")) {
		error = stop_editor(editor);
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
	const char *error = midi_devices_error(editor->midi);
	accept_revision(editor);
	int service = session_poll(&editor->session);
	if (service < 0)
		error = editor->session.error;
	else if (service > 0) {
		int playing = editor->transport.playing;
		int active = editor->player && player_active(editor->player);
		error = pause_editor(editor);
		if (!error && session_restart(&editor->session))
			error = editor->session.error;
		if (!error && active)
			error = start_editor(editor, playing);
	}

	if (controls_poll(&editor->controls, &editor->session) < 0)
		error = "Plugin UI control error";
	int active = editor->player && player_active(editor->player);
	if (!active)
		session_sync(&editor->session);
	int status = editor->player ? player_status(editor->player) : 2;
	if (status < 0)
		error = editor->session.error ? editor->session.error : "Audio error";
	if (active && !ma_device_is_started(&editor->player->device))
		error = "Audio device stopped";
	if (!error && status == 1)
		error = stop_editor(editor);
	if (error) {
		free(editor->error);
		editor->error = copy_string(error);
		pause_editor(editor);
	}
}

int main(int argc, char **argv) {
	int serve = argc > 1 && !strcmp(argv[1], "--serve"), arg = 1 + serve;
	if (argc > arg + 1) {
		fprintf(stderr, "Usage: %s [--serve] [score.janet]\n", argv[0]);
		return 1;
	}
	Editor editor = {.sample_rate = 48000, .entry = argc > arg ? argv[arg] : "examples/gui.janet"};
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
	midi_devices_free(editor.midi);
	modules_free(&editor.modules);
	transport_free(&editor.transport);
	free(editor.error);
	webui_exit();
	webui_clean();
	return !serve && !shown;
}
