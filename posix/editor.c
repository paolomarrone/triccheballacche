#include "daw.h"
#include "player.h"
#include "ui.h"
#include "util.h"
#include "webui.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
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
	ScoreView score;
	unsigned revision;
	Player *player;
	UI *views[MAX_NODES];
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
		webui_return_string(event, "{\"error\":\"Editor occupato o in chiusura\"}");
	} else {
		pending = event;
		while (pending == event)
			pthread_cond_wait(&answered, &mutex);
	}
	pthread_mutex_unlock(&mutex);
}

static void finish(Editor *editor) {
	if (editor->player)
		editor->time = player_time(editor->player);
	player_free(editor->player);
	editor->player = NULL;
	for (int i = 0; i < editor->session.nnodes; ++i) {
		ui_close(editor->views[i]);
		editor->views[i] = NULL;
	}
	session_free(&editor->session);
}

static const char *read_text(const char *path, char **text) {
	struct stat info;
	if (stat(path, &info))
		return strerror(errno);
	if (!S_ISREG(info.st_mode))
		return "Il percorso non è un file regolare";
	FILE *file = fopen(path, "rb");
	if (!file)
		return strerror(errno);
	long size = -1;
	if (!fseek(file, 0, SEEK_END))
		size = ftell(file);
	const char *error = "Impossibile leggere il file (massimo 8 MiB di testo)";
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
		return "Memoria insufficiente";
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
	return result ? "Impossibile salvare il file; il precedente è conservato" : NULL;
}

typedef struct {
	char *data;
	size_t length, capacity;
	int failed;
} Text;

static void append(Text *text, const char *format, ...) {
	if (text->failed)
		return;
	va_list args, copy;
	va_start(args, format);
	va_copy(copy, args);
	int n = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (n < 0 || (size_t)n > SIZE_MAX - text->length - 1) {
		text->failed = 1;
	} else if (text->length + n + 1 > text->capacity) {
		size_t capacity = text->length + n + 1;
		if (capacity < text->capacity * 2)
			capacity = text->capacity * 2;
		if (capacity < 1024)
			capacity = 1024;
		char *data = realloc(text->data, capacity);
		if (!data)
			text->failed = 1;
		else {
			text->data = data;
			text->capacity = capacity;
		}
	}
	if (!text->failed) {
		vsnprintf(text->data + text->length, text->capacity - text->length, format, args);
		text->length += n;
	}
	va_end(args);
}

static void quote(Text *text, const char *value) {
	append(text, "\"");
	const char *p = value ? value : "";
	while (*p) {
		const char *start = p;
		while ((unsigned char)*p >= 32 && *p != '"' && *p != '\\')
			++p;
		if (p != start)
			append(text, "%.*s", (int)(p - start), start);
		if (*p) {
			if ((unsigned char)*p < 32)
				append(text, "\\u%04x", (unsigned char)*p);
			else
				append(text, "\\%c", *p);
			++p;
		}
	}
	append(text, "\"");
}

static void metadata(Text *json, const Editor *editor) {
	const ScoreView *view = &editor->score;
	append(json, ",\"score\":{\"revision\":%u,\"end\":%.17g,\"nodes\":[", editor->revision, view->end);
	for (int i = 0; i < view->nnodes; ++i) {
		const ScoreNode *node = view->nodes + i;
		ScoreSummary summary = node->count ? node->events[node->count / 2].summary : (ScoreSummary){0};
		append(json, "%s{\"name\":", i ? "," : "");
		quote(json, node->name);
		append(json, ",\"low\":%d,\"high\":%d}", summary.low, summary.high);
	}
	append(json, "],\"tracks\":[");
	for (int i = 0; i < view->ntracks; ++i) {
		const Track *track = view->tracks + i;
		append(json, "%s[%d,%d", i ? "," : "", track->source, track->mixer);
		for (int j = 0; j < track->count; ++j)
			append(json, ",%d", track->effects[j]);
		append(json, "]");
	}
	append(json, "]}");
}

enum { MAX_FRAMES = 256, MAX_ACTIVE_EVENTS = 8192, MAX_LANES = 8, MAX_NOTES = 512, MAX_BINS = 512 };

typedef struct {
	const ScoreView *view;
	const ScoreFrame *frames[MAX_FRAMES];
	size_t count, visited;
	int truncated;
} Frames;

static int collect_frames(const ScoreEvent *event, void *context) {
	Frames *out = context;
	if (++out->visited > MAX_ACTIVE_EVENTS) {
		out->truncated = 1;
		return 0;
	}
	for (size_t i = 0; i < event->norigins; ++i) {
		const ScoreOrigin *origin = out->view->origins + out->view->references[event->first_origin + i];
		for (size_t j = 0; j < origin->count; ++j) {
			const ScoreFrame *frame = origin->frames + j;
			size_t k = 0;
			for (; k < out->count; ++k)
				if (out->frames[k]->line == frame->line && out->frames[k]->column == frame->column &&
				    !strcmp(out->frames[k]->file, frame->file))
					break;
			if (k < out->count)
				continue;
			if (out->count == MAX_FRAMES) {
				out->truncated = 1;
				return 0;
			}
			out->frames[out->count++] = frame;
		}
	}
	return 1;
}

static void write_frames(Text *json, const Frames *frames) {
	append(json, ",\"frames\":[");
	for (size_t i = 0; i < frames->count; ++i) {
		append(json, "%s[", i ? "," : "");
		quote(json, frames->frames[i]->file);
		append(json, ",%d,%d]", frames->frames[i]->line, frames->frames[i]->column);
	}
	append(json, "],\"truncated\":%s", frames->truncated ? "true" : "false");
}

typedef struct {
	const ScoreEvent *events[MAX_NOTES];
	size_t count;
} Notes;

static int collect_notes(const ScoreEvent *event, void *context) {
	Notes *notes = context;
	notes->events[notes->count++] = event;
	return notes->count < MAX_NOTES;
}

static double decimal(webui_event_t *request, int index) {
	const char *value = webui_get_string_at(request, index);
	char *end;
	double result = strtod(value, &end);
	return end == value || *end ? NAN : result;
}

static const char *range(Text *json, const Editor *editor, webui_event_t *request) {
	double from = decimal(request, 2), to = decimal(request, 3);
	long long first = webui_get_int_at(request, 4), count = webui_get_int_at(request, 5);
	long long bins = webui_get_int_at(request, 6);
	if (!isfinite(from) || !isfinite(to) || from < 0 || from >= to || first < 0 || first > editor->score.ntracks ||
	    count < 1 || count > MAX_LANES || bins < 1 || bins > MAX_BINS) {
		return "Intervallo della vista non valido";
	}
	append(json, ",\"from\":%.17g,\"to\":%.17g,\"first\":%lld,\"lanes\":[", from, to, first);
	for (int i = first; i < first + count && i < editor->score.ntracks; ++i) {
		int node = editor->score.tracks[i].source;
		ScoreSummary summary = score_view_summary(&editor->score, node, from, to);
		append(json, "%s{\"count\":%zu,", i != first ? "," : "", summary.count);
		if (summary.count <= MAX_NOTES) {
			Notes notes = {0};
			score_view_visit(&editor->score, node, from, to, 1, collect_notes, &notes);
			append(json, "\"notes\":[");
			for (size_t j = 0; j < notes.count; ++j) {
				const ScoreEvent *n = notes.events[j];
				append(
				    json, "%s[%zu,%.17g,%.17g,%d,%d]", j ? "," : "", n->order, n->start, n->end, n->pitch, n->velocity);
			}
			append(json, "]}");
		} else {
			append(json, "\"density\":[");
			for (int j = 0; j < bins; ++j) {
				double a = from + (to - from) * j / bins, b = from + (to - from) * (j + 1) / bins;
				ScoreSummary bin = score_view_summary(&editor->score, node, a, b);
				append(json, "%s[%zu,%d,%d]", j ? "," : "", bin.count, bin.low, bin.high);
			}
			append(json, "]}");
		}
	}
	append(json, "]");
	return NULL;
}

static void reply(
    webui_event_t *event, Editor *editor, const char *error, const char *text, const char *path, int score) {
	const char *op = webui_get_string_at(event, 0);
	int query = !strcmp(op, "range") || !strcmp(op, "note");
	double time = editor->player ? player_time(editor->player) : editor->time;
	Text json = {0};
	append(&json, "{\"text\":");
	quote(&json, text);
	append(&json, ",\"path\":");
	quote(&json, path);
	append(&json, ",\"playing\":%s,\"time\":%.17g,\"revision\":%u", editor->player ? "true" : "false", time,
	    editor->revision);
	if (score)
		metadata(&json, editor);
	if (query && webui_get_int_at(event, 1) != editor->revision) {
		append(&json, ",\"stale\":true");
	} else if (!strcmp(op, "range")) {
		error = range(&json, editor, event);
	} else if (!strcmp(op, "note")) {
		long long node = webui_get_int_at(event, 2), order = webui_get_int_at(event, 3);
		const ScoreEvent *note = node >= 0 && node < editor->score.nnodes && order >= 0
		    ? score_view_find(&editor->score, node, order)
		    : NULL;
		Frames frames = {.view = &editor->score};
		if (note && note->pitch >= 0)
			collect_frames(note, &frames);
		write_frames(&json, &frames);
	} else if (!strcmp(op, "status")) {
		Frames frames = {.view = &editor->score};
		for (int i = 0; editor->player && !frames.truncated && i < editor->score.nnodes; ++i)
			score_view_visit(&editor->score, i, time, nextafter(time, INFINITY), 0, collect_frames, &frames);
		write_frames(&json, &frames);
	}
	append(&json, ",\"error\":");
	quote(&json, error);
	append(&json, "}");
	webui_return_string(event, json.failed ? "{\"error\":\"Memoria insufficiente\"}" : json.data);
	free(json.data);
}

static void command(Editor *editor, webui_event_t *event) {
	const char *op = webui_get_string_at(event, 0);
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
		error = "Partitura troppo grande (massimo 8 MiB)";
	} else if (!strcmp(op, "open")) {
		error = read_text(path, &text);
	} else if (!strcmp(op, "save")) {
		error = save_text(path, source);
	} else if (!strcmp(op, "run")) {
		finish(editor);
		free(editor->error);
		editor->error = NULL;
		double previous_time = editor->time;
		editor->session.sample_rate = 48000;
		Output output;
		ScoreView score;
		if (prepare_score(&editor->session, &output, path, source, &diagnostics, &score)) {
			error = diagnostics ? diagnostics : editor->session.error ? editor->session.error : "Preparazione fallita";
		} else {
			for (int i = 0; i < editor->session.nnodes; ++i) {
				if (ui_open(editor->views + i, editor->session.nodes + i)) {
					error = "Impossibile aprire la GUI di un plugin";
					break;
				}
				ui_show(editor->views[i], webui_get_bool_at(event, 3));
			}
			if (!error && (!(editor->player = player_new(&editor->session)) || player_start(editor->player)))
				error = editor->session.error ? editor->session.error : "Avvio audio fallito";
		}
		if (error) {
			finish(editor);
			score_view_free(&score);
			editor->time = previous_time;
		} else {
			score_view_free(&editor->score);
			editor->score = score;
			editor->time = 0;
			++editor->revision;
			changed = 1;
		}
	} else if (!strcmp(op, "stop")) {
		finish(editor);
	} else if (!strcmp(op, "views")) {
		for (int i = 0; i < editor->session.nnodes; ++i)
			ui_show(editor->views[i], webui_get_bool_at(event, 3));
	} else if (!strcmp(op, "status")) {
		error = editor->error;
	} else {
		error = "Comando sconosciuto";
	}
	reply(event, editor, error, text, path, changed);
	free(diagnostics);
	free(text);
}

static void poll_player(Editor *editor) {
	if (!editor->player)
		return;
	const char *error = NULL;
	for (int i = 0; i < editor->session.nnodes; ++i) {
		int status = ui_poll(editor->views[i]);
		if (status == 1)
			ui_show(editor->views[i], 0);
		if (status < 0)
			error = "Errore nei controlli della GUI di un plugin";
	}
	int status = player_status(editor->player);
	if (status < 0)
		error = editor->session.error ? editor->session.error : "Errore audio";
	if (!status && !ma_device_is_started(&editor->player->device))
		error = "Dispositivo audio arrestato";
	if (error || status) {
		free(editor->error);
		editor->error = error ? copy_string(error) : NULL;
		finish(editor);
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
	webui_set_size(window, 1000, 760);
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	int shown = webui_show_browser(window, "index.html", serve ? NoBrowser : AnyBrowser);
	if (!serve && !shown) {
		fputs("Cannot open the editor browser. Try --serve and open its URL.\n", stderr);
		stopped = 1;
	}
	printf("Editor: %s/index.html\n", webui_get_url(window));
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
	score_view_free(&editor.score);
	free(editor.error);
	webui_exit();
	webui_clean();
	return !serve && !shown;
}
