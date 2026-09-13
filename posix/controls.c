#include "controls.h"
#include "module.h"
#include "json_write.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void detach(Controls *c, Session *s, int id) {
	ui_close(c->native[id]);
	c->native[id] = NULL;
	if (c->watched[id])
		watch_dsp(s->nodes[id].dsp[0].dsp, 0);
	c->watched[id] = 0;
}

void controls_close(Controls *c, Session *s) {
	for (int i = 0; i < s->nnodes; ++i)
		detach(c, s, i);
}

int controls_poll(Controls *c, Session *s) {
	int error = 0;
	for (int i = 0; i < s->nnodes; ++i) {
		int status = ui_poll(c->native[i]);
		if (status)
			detach(c, s, i);
		error |= status < 0;
	}
	return error ? -1 : 0;
}

static double number(webui_event_t *event, size_t index) {
	const char *text = webui_get_string_at(event, index);
	char *end;
	double value = strtod(text, &end);
	return end != text && !*end && isfinite(value) ? value : NAN;
}

static int hex(char c) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

void controls_command(Controls *c, Session *s, const ScoreView *view, unsigned revision, webui_event_t *event) {
	const char *op = webui_get_string_at(event, 0), *error = NULL;
	double version = number(event, 1), id = number(event, 2);
	Json json = {0};
	json_print(&json, "{");
	if (version != revision || !s->sealed) {
		error = "Stale plugin view";
		goto done;
	}
	if (!isfinite(id) || id < 0 || id >= s->nnodes || id != floor(id) || !s->nodes[(int)id].path) {
		error = "Invalid module";
		goto done;
	}
	Node *node = s->nodes + (int)id;
	DSP *dsp = node->dsp[0].dsp;
	const PluginConfig *config = &node->dsp[0].config;
	int n = id;
	if (!strcmp(op, "watch")) {
		const char *mode = webui_get_string_at(event, 3);
		if (strcmp(mode, "off") && strcmp(mode, "web") && strcmp(mode, "native")) {
			error = "Invalid view type";
			goto done;
		}
		detach(c, s, n);
		if (!strcmp(mode, "native")) {
			if (ui_open(&c->native[n], node) || !c->native[n]) {
				detach(c, s, n);
				error = "Native UI unavailable";
			} else
				ui_show(c->native[n], 1);
		} else if (!strcmp(mode, "web")) {
			watch_dsp(dsp, 1);
			c->watched[n] = 1;
		}
	} else if (!c->watched[n]) {
		error = "Web view not attached";
	} else if (!strcmp(op, "parameter")) {
		double index = number(event, 3), value = number(event, 4);
		if (!isfinite(index) || index < 0 || index >= config->nparams || index != floor(index) ||
		    (config->outputs & (UINT64_C(1) << (int)index)) || !isfinite((float)value)) {
			error = "Invalid parameter";
		} else {
			const ScoreNode *meta = view->nodes + (int)id;
			int p = index, integer = !!(meta->integers & (UINT64_C(1) << p));
			float low = meta->minimum[p], high = meta->maximum[p];
			if (integer) {
				value = round(value);
				low = ceilf(low);
				high = floorf(high);
			}
			edit_dsp(dsp, p, fmin(high, fmax(low, value)));
		}
	} else if (!strcmp(op, "message")) {
		const char *text = webui_get_string_at(event, 3);
		size_t length = strlen(text);
		unsigned char bytes[MAX_MESSAGE];
		if (length % 2 || length / 2 > config->to_dsp || !config->to_dsp) {
			error = "Invalid message";
		} else {
			for (size_t i = 0; i < length; i += 2) {
				int a = hex(text[i]), b = hex(text[i + 1]);
				if (a < 0 || b < 0) {
					error = "Invalid message";
					break;
				}
				bytes[i / 2] = 16 * a + b;
			}
			if (!error && send_dsp(dsp, length / 2, bytes))
				error = "UI message queue full";
		}
	} else if (!strcmp(op, "controls")) {
		json_print(&json, "\"values\":[");
		for (int i = 0; i < config->nparams; ++i) {
			float value;
			json_print(&json, "%s", i ? "," : "");
			if (read_dsp(dsp, i, &value) && isfinite(value))
				json_print(&json, "%.9g", value);
			else
				json_print(&json, "null");
		}
		json_print(&json, "],\"messages\":[");
		size_t size;
		unsigned char bytes[MAX_MESSAGE];
		for (int i = 0; i < MESSAGE_SLOTS; ++i) {
			int result = receive_dsp(dsp, &size, bytes);
			if (result <= 0) {
				if (result < 0)
					error = "DSP message queue full";
				break;
			}
			json_print(&json, "%s[", i ? "," : "");
			for (size_t j = 0; j < size; ++j)
				json_print(&json, "%s%u", j ? "," : "", bytes[j]);
			json_print(&json, "]");
		}
		json_print(&json, "],");
	}
done:
	json_print(&json, "\"error\":");
	json_string(&json, error);
	json_print(&json, "}");
	webui_return_string(event, json.failed ? "{\"error\":\"Out of memory\"}" : json.data);
	free(json.data);
}
