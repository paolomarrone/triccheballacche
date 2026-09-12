#include "daw.h"
#include "player.h"
#include "ui.h"
#include "util.h"
#include "webui.h"
#include <errno.h>
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

static char *quote(const char *text) {
	char *json = malloc(6 * strlen(text) + 3), *p = json;
	if (!p)
		return NULL;
	*p++ = '"';
	for (const unsigned char *s = (const unsigned char *)text; *s; ++s) {
		if (*s < 32)
			p += sprintf(p, "\\u%04x", *s);
		else {
			if (*s == '"' || *s == '\\')
				*p++ = '\\';
			*p++ = *s;
		}
	}
	*p++ = '"';
	*p = 0;
	return json;
}

static void reply(webui_event_t *event, Editor *editor, const char *error, const char *text, const char *path) {
	char *e = quote(error ? error : ""), *t = quote(text ? text : ""), *p = quote(path);
	char *json = e && t && p ? malloc(strlen(e) + strlen(t) + strlen(p) + 160) : NULL;
	if (json) {
		sprintf(json, "{\"error\":%s,\"text\":%s,\"path\":%s,\"playing\":%s,\"time\":%.6f}", e, t, p,
		    editor->player ? "true" : "false", editor->player ? player_time(editor->player) : editor->time);
		webui_return_string(event, json);
	} else {
		webui_return_string(event, "{\"error\":\"Memoria insufficiente\"}");
	}
	free(json);
	free(e);
	free(t);
	free(p);
}

static void command(Editor *editor, webui_event_t *event) {
	const char *op = webui_get_string_at(event, 0), *path = webui_get_string_at(event, 1);
	const char *source = webui_get_string_at(event, 2), *error = NULL;
	char *text = NULL, *diagnostics = NULL;
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
		editor->time = 0;
		editor->session.sample_rate = 48000;
		Output output;
		if (prepare_score(&editor->session, &output, path, source, &diagnostics)) {
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
		if (error)
			finish(editor);
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
	reply(event, editor, error, text, path);
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
	free(editor.error);
	webui_exit();
	webui_clean();
	return !serve && !shown;
}
