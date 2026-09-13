#include "controls.h"
#include "module.h"
#include "json_write.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void controls_close(Controls *c, Session *s) {
	ui_close(c->native);
	c->native = NULL;
	if (c->node >= 0 && c->node < s->nnodes)
		watch_dsp(s->nodes[c->node].dsp[0].dsp, 0);
	c->node = -1;
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
		error = "Vista plugin scaduta";
		goto done;
	}
	if (!strcmp(op, "watch") && id == -1) {
		controls_close(c, s);
		goto done;
	}
	if (!isfinite(id) || id < 0 || id >= s->nnodes || id != floor(id) || !s->nodes[(int)id].path) {
		error = "Modulo non valido";
		goto done;
	}
	Node *node = s->nodes + (int)id;
	DSP *dsp = node->dsp[0].dsp;
	const PluginConfig *config = &node->dsp[0].config;
	if (!strcmp(op, "watch")) {
		controls_close(c, s);
		c->node = id;
		if (webui_get_bool_at(event, 3)) {
			if (ui_open(&c->native, node) || !c->native) {
				controls_close(c, s);
				error = "GUI nativa non disponibile";
			} else
				ui_show(c->native, 1);
		} else {
			// Discard messages from the previous attachment, then start a fresh stream.
			size_t size;
			unsigned char bytes[MAX_MESSAGE];
			while (message_pop(&dsp->to_ui, &size, bytes)) {
			}
			atomic_store(&dsp->overflow, 0);
			watch_dsp(dsp, 1);
		}
	} else if (c->node != id || c->native) {
		error = "Vista web non collegata";
	} else if (!strcmp(op, "parameter")) {
		double index = number(event, 3), value = number(event, 4);
		if (!isfinite(index) || index < 0 || index >= config->nparams || index != floor(index) ||
		    (config->outputs & (UINT64_C(1) << (int)index)) || !isfinite((float)value)) {
			error = "Parametro non valido";
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
			error = "Messaggio non valido";
		} else {
			for (size_t i = 0; i < length; i += 2) {
				int a = hex(text[i]), b = hex(text[i + 1]);
				if (a < 0 || b < 0) {
					error = "Messaggio non valido";
					break;
				}
				bytes[i / 2] = 16 * a + b;
			}
			if (!error && send_dsp(dsp, length / 2, bytes))
				error = "Coda messaggi GUI piena";
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
					error = "Coda messaggi DSP piena";
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
	webui_return_string(event, json.failed ? "{\"error\":\"Memoria insufficiente\"}" : json.data);
	free(json.data);
}
