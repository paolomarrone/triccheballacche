#include "score.h"
#include "util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int fail(Score *s, const char *message) {
	s->error = message;
	return -1;
}

unsigned score_rate(Score *s) {
	if (!s->sample_rate)
		s->sample_rate = DEFAULT_SAMPLE_RATE;
	return s->sample_rate <= 384000 ? s->sample_rate : 0;
}

static int valid(Score *s, int id, int param, double value) {
	if (s->sealed || id < 0 || id >= s->nnodes)
		return fail(s, "sealed score or invalid handle");
	const Node *n = s->nodes + id;
	const PluginConfig *config = &n->config;
	if (param < 0 || param >= config->nparams || (config->parameters[param].flags & PARAM_OUTPUT))
		return fail(s, "unknown or output parameter");
	if (!isfinite(value) || (!n->path && (value < (param ? -1 : 0) || value > (param ? 1 : 4))))
		return fail(s, "parameter outside range");
	return 0;
}

int score_plugin(Score *s, const char *path, const PluginConfig *config) {
	if (s->sealed || s->nnodes == MAX_NODES)
		return fail(s, "sealed score or node limit reached");
	if (!plugin_config_valid(config))
		return fail(s, "invalid plugin configuration");
	Node *n = s->nodes + s->nnodes;
	*n = (Node){0};
	if (plugin_config_copy(&n->config, config))
		return fail(s, "out of memory");
	n->path = copy_string(path);
	if (!n->path) {
		plugin_config_free(&n->config);
		return fail(s, "out of memory");
	}
	return s->nnodes++;
}

int score_set(Score *s, int id, int param, double value) {
	if (valid(s, id, param, value))
		return -1;
	Node *n = s->nodes + id;
	n->config.parameters[param].value = value;
	return 0;
}

static int handle(Score *s, int id) {
	return !s->sealed && id >= 0 && id < s->nnodes;
}

int score_through(Score *s, int source, int effect) {
	if (!handle(s, source) || !handle(s, effect))
		return fail(s, "sealed score or invalid handle");
	Node *n = s->nodes + effect;
	if (!n->path || !n->config.input || n->ninputs)
		return fail(s, "expected an effect without an input");
	n->inputs = malloc(sizeof(int));
	if (!n->inputs)
		return fail(s, "out of memory");
	n->inputs[0] = source;
	n->ninputs = 1;
	return effect;
}

int score_midi(Score *s) {
	if (s->sealed || s->nnodes == MAX_NODES)
		return fail(s, "sealed score or node limit reached");
	s->nodes[s->nnodes] = (Node){.midi_source = 1, .config = {.midi = 0, .midi_output = 1}};
	return s->nnodes++;
}
int score_midi_through(Score *s, int source, int target) {
	if (!handle(s, source) || !handle(s, target) || !s->nodes[source].config.midi_output ||
	    s->nodes[target].config.midi < 0)
		return fail(s, "expected MIDI output and input");
	Node *n = s->nodes + target;
	for (int i = 0; i < n->nmidi_inputs; ++i)
		if (n->midi_inputs[i] == source)
			return fail(s, "duplicate MIDI connection");
	int *inputs = realloc(n->midi_inputs, (n->nmidi_inputs + 1) * sizeof(int));
	if (!inputs)
		return fail(s, "out of memory");
	n->midi_inputs = inputs;
	inputs[n->nmidi_inputs++] = source;
	return target;
}
int score_midi_output(Score *s, int source) {
	if (!handle(s, source) || !s->nodes[source].config.midi_output)
		return fail(s, "expected MIDI output");
	s->nodes[source].midi_output = 1;
	return source;
}

int score_mix(Score *s, const int *inputs, int count) {
	if (s->sealed || s->nnodes == MAX_NODES || count < 1 || count > MAX_NODES)
		return fail(s, "sealed score or invalid mix size");
	for (int i = 0; i < count; ++i)
		if (!handle(s, inputs[i]))
			return fail(s, "invalid mix input");
	Node *n = s->nodes + s->nnodes;
	*n = (Node){.ninputs = count};
	Parameter parameters[] = {{.value = 1, .minimum = 0, .maximum = 4, .flags = PARAM_AUTOMATABLE},
	    {.id = 1, .value = 0, .minimum = -1, .maximum = 1, .flags = PARAM_AUTOMATABLE}};
	PluginConfig config = {.nparams = 2, .midi = -1, .output = 2, .parameters = parameters};
	if (plugin_config_copy(&n->config, &config))
		return fail(s, "out of memory");
	n->inputs = calloc(count, sizeof(int));
	if (!n->inputs) {
		plugin_config_free(&n->config);
		return fail(s, "out of memory");
	}
	for (int i = 0; i < count; ++i)
		n->inputs[i] = inputs[i];
	return s->nnodes++;
}

int score_output(Score *s, int source) {
	if (!handle(s, source) || s->has_output)
		return fail(s, "expected one output in an unsealed score");
	s->output = source;
	s->has_output = 1;
	return source;
}

// A track is a visible mixer point. Its source may itself be a mix or another track.
int score_track(Score *s, int source, const int *effects, int count, int master) {
	if (!handle(s, source) || count < 0 || count > MAX_FX || (master ? s->has_master : s->ntracks == MAX_TRACKS))
		return fail(s, "sealed score or track/effect limit reached");
	int signal = source, connected = 0;
	for (; connected < count; ++connected) {
		signal = score_through(s, signal, effects[connected]);
		if (signal < 0)
			goto rollback;
	}
	int mixer = score_mix(s, &signal, 1);
	if (mixer < 0)
		goto rollback;
	Track *t = master ? &s->master : &s->tracks[s->ntracks++];
	*t = (Track){.source = source, .mixer = mixer, .count = count};
	for (int i = 0; i < count; ++i)
		t->effects[i] = effects[i];
	if (master)
		s->has_master = 1;
	else
		s->nodes[mixer].track = s->ntracks;
	return mixer;
rollback:
	while (connected) {
		Node *n = s->nodes + effects[--connected];
		free(n->inputs);
		n->inputs = NULL;
		n->ninputs = 0;
	}
	return -1;
}

// Validate and order only at sealing: plugins may be declared before their inputs.
static int visit(Score *s, int id, unsigned char *state) {
	if (state[id] == 1)
		return fail(s, "audio/MIDI graph contains a cycle");
	if (state[id] == 2)
		return 0;
	state[id] = 1;
	Node *n = s->nodes + id;
	if (!plugin_config_valid(&n->config))
		return fail(s, "invalid plugin configuration");
	if (n->path &&
	    (n->ninputs > 1 || (n->ninputs && !n->config.input) || (!n->ninputs && n->config.input && n->config.midi < 0)))
		return fail(s, "invalid plugin audio input");
	n->upstream = n->track ? 1u << (n->track - 1) : 0;
	for (int i = 0; i < n->ninputs; ++i) {
		int input = n->inputs[i];
		if (visit(s, input, state))
			return -1;
		n->upstream |= s->nodes[input].upstream;
	}
	for (int i = 0; i < n->nmidi_inputs; ++i) {
		int source = n->midi_inputs[i];
		if (source < 0 || source >= s->nnodes || !s->nodes[source].config.midi_output || n->config.midi < 0)
			return fail(s, "invalid MIDI connection");
		if (visit(s, source, state))
			return -1;
	}
	for (int i = 0; i < n->ninputs; ++i)
		if (!s->nodes[n->inputs[i]].channels)
			return fail(s, "MIDI stream used as audio");
	n->channels = n->midi_source ? 0 : n->path ? n->config.output : 2;
	if (n->path && n->ninputs && s->nodes[n->inputs[0]].channels == 2 && n->config.input == 1) {
		if (n->config.format != PLUGIN_PERONE || n->config.midi >= 0 || n->config.midi_output)
			return fail(s, "a mono plugin requires a mono signal");
		if (n->channels == 2)
			return fail(s, "a mono-to-stereo effect requires a mono signal");
		n->channels = 2;
	}
	state[id] = 2;
	s->order[s->norder++] = id;
	return 0;
}

static int compile(Score *s) {
	unsigned char state[MAX_NODES] = {0};
	s->norder = 0;
	if (s->has_output && visit(s, s->output, state))
		return -1;
	for (int i = 0; i < s->nnodes; ++i)
		if (s->nodes[i].midi_output && visit(s, i, state))
			return -1;
	if (!s->norder)
		return fail(s, "missing audio or MIDI output");
	if (s->norder != s->nnodes)
		return fail(s, "node does not reach the output");
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		n->downstream = n->track ? 1u << (n->track - 1) : 0;
	}
	for (int i = s->norder; i-- > 0;) {
		Node *n = s->nodes + s->order[i];
		for (int j = 0; j < n->ninputs; ++j)
			s->nodes[n->inputs[j]].downstream |= n->downstream;
	}
	return 0;
}

static int valid_cue(Score *s, Cue c) {
	if (s->sealed || c.node < 0 || c.node >= s->nnodes || c.stream < 0 || c.stream >= 65536)
		return fail(s, "invalid event source or sealed score");
	const Node *n = s->nodes + c.node;
	if (c.parameter == -2) {
		int status = c.midi[0] & 0xf0;
		if (n->config.midi < 0 || c.start != c.end || c.midi_size > 3 || !midi_valid(c.midi, c.midi_size) ||
		    status < 0xa0 || status > 0xe0)
			return fail(s, "expected a short MIDI controller message (use note for notes)");
	} else if (c.parameter < 0) {
		if (c.parameter != -1 || n->config.midi < 0 || c.pitch < 0 || c.pitch > 127 || c.velocity < 1 ||
		    c.velocity > 127 || c.channel < 0 || c.channel > 15 || c.end <= c.start)
			return fail(s, "invalid note");
	} else if (valid(s, c.node, c.parameter, c.value) || c.start != c.end ||
	    !(n->config.parameters[c.parameter].flags & PARAM_AUTOMATABLE)) {
		return fail(s, "invalid parameter event");
	}
	return 0;
}

int score_cue(Score *s, Cue cue) {
	if (valid_cue(s, cue))
		return -1;
	if (sequence_add(&s->sequence, cue))
		return fail(s, "invalid cue, event limit exceeded or out of memory");
	++s->nodes[cue.node].event_count;
	return 0;
}

int score_param(Score *s, int id, uint64_t time, int param, double value) {
	return score_cue(s,
	    (Cue){.node = id,
	        .start = (double)time / score_rate(s),
	        .end = (double)time / score_rate(s),
	        .parameter = param,
	        .value = value});
}

int score_note(Score *s, int id, uint64_t time, uint64_t end, int pitch, int velocity) {
	return score_cue(s,
	    (Cue){.node = id,
	        .start = (double)time / score_rate(s),
	        .end = (double)end / score_rate(s),
	        .parameter = -1,
	        .pitch = pitch,
	        .velocity = velocity});
}

int score_end(Score *s, uint64_t frames) {
	if (s->sealed || (!frames && (s->sequence.count || s->live)) || !score_rate(s))
		return fail(s, "empty duration or already sealed");
	if (!s->sequence.bpm)
		s->sequence.bpm = 120;
	if (!s->sequence.quantum)
		s->sequence.quantum = 4;
	if (!sequence_valid(&s->sequence, s->sample_rate))
		return fail(s, "invalid sequence timing");
	size_t counts[MAX_NODES] = {0};
	for (size_t i = 0; i < s->sequence.count; ++i) {
		Cue c = s->sequence.cues[i];
		if (valid_cue(s, c))
			return -1;
		++counts[c.node];
		// Finite programs must fit their declared duration. A live score's duration is an export crop.
		if (!s->live &&
		    (c.start < 0 || c.period || (uint64_t)llround(c.start * s->sample_rate) >= frames ||
		        (uint64_t)llround(c.end * s->sample_rate) > frames))
			return fail(s, "event outside duration");
	}
	if (compile(s))
		return -1;
	for (int i = 0; i < s->nnodes; ++i)
		s->nodes[i].event_count = counts[i];
	s->frames = frames;
	s->sealed = 1;
	return 0;
}

void score_remap(Score *s, const int *mapping) {
	Node nodes[MAX_NODES];
	memcpy(nodes, s->nodes, s->nnodes * sizeof(Node));
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + mapping[i];
		*n = nodes[i];
		for (int j = 0; j < n->ninputs; ++j)
			n->inputs[j] = mapping[n->inputs[j]];
		for (int j = 0; j < n->nmidi_inputs; ++j)
			n->midi_inputs[j] = mapping[n->midi_inputs[j]];
	}
	for (int i = 0; i < s->ntracks + s->has_master; ++i) {
		Track *t = i < s->ntracks ? s->tracks + i : &s->master;
		t->source = mapping[t->source];
		t->mixer = mapping[t->mixer];
		for (int j = 0; j < t->count; ++j)
			t->effects[j] = mapping[t->effects[j]];
	}
	for (int i = 0; i < s->norder; ++i)
		s->order[i] = mapping[s->order[i]];
	if (s->has_output)
		s->output = mapping[s->output];
	for (size_t i = 0; i < s->sequence.count; ++i)
		s->sequence.cues[i].node = mapping[s->sequence.cues[i].node];
}

void score_free(Score *s) {
	sequence_free(&s->sequence);
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		plugin_info_free(&n->info);
		plugin_config_free(&n->config);
		free(n->path);
		free(n->name);
		free(n->key);
		free(n->inputs);
		free(n->midi_inputs);
	}
	*s = (Score){0};
}
