#include "vst3.h"
#include <stdlib.h>

Steinberg_uint32 SMTG_STDMETHODCALLTYPE vst_borrow(void *object) {
	(void)object;
	return 1; // Embedded interfaces borrow their owner's lifetime.
}
Steinberg_tresult vst_match(void *object, const Steinberg_TUID iid, const Steinberg_TUID own, void **out) {
	*out = NULL;
	if (memcmp(iid, own, 16) && memcmp(iid, Steinberg_FUnknown_iid, 16))
		return Steinberg_kNoInterface;
	vst_retain(object);
	*out = object;
	return Steinberg_kResultOk;
}

// Messages and their attributes belong to the main-thread controller connection.
typedef struct Attribute {
	struct Attribute *next;
	char *id;
	unsigned type, size;
	unsigned char data[];
} Attribute;
typedef struct {
	Steinberg_Vst_IAttributeList api;
	unsigned refs;
	Attribute *head;
} Attributes;
static Steinberg_tresult SMTG_STDMETHODCALLTYPE attributes_query(void *self, const Steinberg_TUID iid, void **out) {
	return vst_match(self, iid, Steinberg_Vst_IAttributeList_iid, out);
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE attributes_ref(void *self) {
	return ++((Attributes *)self)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE attributes_release(void *self) {
	Attributes *a = self;
	if (--a->refs)
		return a->refs;
	while (a->head) {
		Attribute *next = a->head->next;
		free(a->head->id);
		free(a->head);
		a->head = next;
	}
	free(a);
	return 0;
}
static Attribute **attribute(Attributes *a, const char *id) {
	Attribute **p = &a->head;
	while (*p && strcmp((*p)->id, id))
		p = &(*p)->next;
	return p;
}
static Steinberg_tresult attr_set(void *self, const char *id, unsigned type, const void *data, unsigned size) {
	if (!id || (size && !data) || size > 16 * 1024 * 1024)
		return Steinberg_kInvalidArgument;
	Attribute *a = malloc(sizeof(*a) + size);
	if (!a)
		return Steinberg_kOutOfMemory;
	a->id = malloc(strlen(id) + 1);
	if (!a->id) {
		free(a);
		return Steinberg_kOutOfMemory;
	}
	strcpy(a->id, id);
	a->type = type;
	a->size = size;
	if (size)
		memcpy(a->data, data, size);
	Attribute **p = attribute(self, id), *old = *p;
	a->next = old ? old->next : NULL;
	*p = a;
	if (old) {
		free(old->id);
		free(old);
	}
	return Steinberg_kResultOk;
}
static Attribute *attr_get(void *self, const char *id, unsigned type) {
	Attribute *a = id ? *attribute(self, id) : NULL;
	return a && a->type == type ? a : NULL;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_int(void *s, const char *id, Steinberg_int64 value) {
	return attr_set(s, id, 0, &value, sizeof(value));
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE get_int(void *s, const char *id, Steinberg_int64 *value) {
	Attribute *a = attr_get(s, id, 0);
	if (!a || !value)
		return Steinberg_kResultFalse;
	memcpy(value, a->data, sizeof(*value));
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_float(void *s, const char *id, double value) {
	return attr_set(s, id, 1, &value, sizeof(value));
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE get_float(void *s, const char *id, double *value) {
	Attribute *a = attr_get(s, id, 1);
	if (!a || !value)
		return Steinberg_kResultFalse;
	memcpy(value, a->data, sizeof(*value));
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_string(void *s, const char *id, const Steinberg_char16 *value) {
	if (!value)
		return Steinberg_kInvalidArgument;
	size_t n = 0;
	while (n < 8 * 1024 * 1024 - 1 && value[n])
		++n;
	if (value[n])
		return Steinberg_kInvalidArgument;
	return attr_set(s, id, 2, value, (n + 1) * sizeof(*value));
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE get_string(
    void *s, const char *id, Steinberg_char16 *value, Steinberg_uint32 size) {
	Attribute *a = attr_get(s, id, 2);
	if (!a || !value || size < sizeof(*value))
		return Steinberg_kResultFalse;
	size -= size % sizeof(*value);
	memcpy(value, a->data, size < a->size ? size : a->size);
	value[size / sizeof(*value) - 1] = 0;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_binary(
    void *s, const char *id, const void *value, Steinberg_uint32 size) {
	return attr_set(s, id, 3, value, size);
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE get_binary(
    void *s, const char *id, const void **value, Steinberg_uint32 *size) {
	Attribute *a = attr_get(s, id, 3);
	if (!a || !value || !size)
		return Steinberg_kResultFalse;
	*value = a->data;
	*size = a->size;
	return Steinberg_kResultOk;
}
static Attributes *attributes_new(void) {
	static Steinberg_Vst_IAttributeListVtbl table = {attributes_query, attributes_ref, attributes_release, set_int,
	    get_int, set_float, get_float, set_string, get_string, set_binary, get_binary};
	Attributes *a = calloc(1, sizeof(*a));
	if (a) {
		a->api.lpVtbl = &table;
		a->refs = 1;
	}
	return a;
}
typedef struct {
	Steinberg_Vst_IMessage api;
	unsigned refs;
	char *id;
	Attributes *attributes;
} Message;
static Steinberg_tresult SMTG_STDMETHODCALLTYPE message_query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_Vst_IMessage_iid, out);
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE message_ref(void *s) {
	return ++((Message *)s)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE message_release(void *s) {
	Message *m = s;
	if (--m->refs)
		return m->refs;
	vst_release(m->attributes);
	free(m->id);
	free(m);
	return 0;
}
static Steinberg_FIDString SMTG_STDMETHODCALLTYPE message_id(void *s) {
	return ((Message *)s)->id;
}
static void SMTG_STDMETHODCALLTYPE message_set_id(void *s, Steinberg_FIDString id) {
	Message *m = s;
	char *copy = id ? malloc(strlen(id) + 1) : NULL;
	if (id && !copy)
		return;
	if (copy)
		strcpy(copy, id);
	free(m->id);
	m->id = copy;
}
static Steinberg_Vst_IAttributeList *SMTG_STDMETHODCALLTYPE message_attributes(void *s) {
	return &((Message *)s)->attributes->api;
}
static Message *message_new(void) {
	static Steinberg_Vst_IMessageVtbl table = {
	    message_query, message_ref, message_release, message_id, message_set_id, message_attributes};
	Message *m = calloc(1, sizeof(*m));
	if (m) {
		m->api.lpVtbl = &table;
		m->refs = 1;
		m->attributes = attributes_new();
		if (!m->attributes) {
			free(m);
			return NULL;
		}
	}
	return m;
}

struct VstHost {
	Steinberg_Vst_IHostApplication api;
	unsigned refs;
	void *loop;
};
// Factories may be singletons even across independent module handles. Host calls
// run on the main thread; modules and plugins retain this shared application.
static VstHost *application;
static Steinberg_tresult SMTG_STDMETHODCALLTYPE host_query(void *s, const Steinberg_TUID iid, void **out) {
	VstHost *h = s;
	if (vst_match(s, iid, Steinberg_Vst_IHostApplication_iid, out) == Steinberg_kResultOk)
		return Steinberg_kResultOk;
	*out = vst_query(h->loop, iid);
	return *out ? Steinberg_kResultOk : Steinberg_kNoInterface;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE host_ref(void *s) {
	return ++((VstHost *)s)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE host_release(void *s) {
	VstHost *h = s;
	if (--h->refs)
		return h->refs;
	application = NULL;
	vst_release(h->loop);
	free(h);
	return 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE host_name(void *s, Steinberg_Vst_String128 name) {
	(void)s;
	const char text[] = "triccheballacche";
	for (size_t i = 0; i < sizeof(text); ++i)
		name[i] = text[i];
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE host_create(
    void *s, Steinberg_TUID cid, Steinberg_TUID iid, void **out) {
	(void)s;
	*out = NULL;
	if (memcmp(cid, iid, 16))
		return Steinberg_kNoInterface;
	if (!memcmp(iid, Steinberg_Vst_IMessage_iid, 16))
		*out = message_new();
	else if (!memcmp(iid, Steinberg_Vst_IAttributeList_iid, 16))
		*out = attributes_new();
	else
		return Steinberg_kNoInterface;
	return *out ? Steinberg_kResultOk : Steinberg_kOutOfMemory;
}
VstHost *vst_host_new(void) {
	if (application) {
		host_ref(application);
		return application;
	}
	static Steinberg_Vst_IHostApplicationVtbl table = {host_query, host_ref, host_release, host_name, host_create};
	VstHost *h = calloc(1, sizeof(*h));
	if (h) {
		h->api.lpVtbl = &table;
		h->refs = 1;
		h->loop = vst_loop_new();
		if (!h->loop) {
			free(h);
			return NULL;
		}
	}
	application = h;
	return h;
}
Steinberg_FUnknown *vst_host_context(VstHost *h) {
	return (Steinberg_FUnknown *)&h->api;
}
void vst_host_poll(VstHost *h) {
	vst_loop_poll(h->loop);
}

// State calls run with rendering stopped. Streams grow here, never in process().
static Steinberg_tresult SMTG_STDMETHODCALLTYPE stream_query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_IBStream_iid, out);
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE stream_ref(void *s) {
	return ++((VstStream *)s)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE stream_release(void *s) {
	VstStream *b = s;
	if (--b->refs)
		return b->refs;
	free(b->data);
	free(b);
	return 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE stream_read(
    void *s, void *data, Steinberg_int32 size, Steinberg_int32 *read) {
	VstStream *b = s;
	if (read)
		*read = 0;
	if (size < 0 || (size && !data))
		return Steinberg_kInvalidArgument;
	size_t available = b->position < b->length ? b->length - b->position : 0;
	size_t n = (size_t)size < available ? (size_t)size : available;
	if (n)
		memcpy(data, b->data + b->position, n);
	b->position += n;
	if (read)
		*read = n;
	return n == (size_t)size ? Steinberg_kResultOk : Steinberg_kResultFalse;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE stream_write(
    void *s, void *data, Steinberg_int32 size, Steinberg_int32 *written) {
	VstStream *b = s;
	if (written)
		*written = 0;
	if (size < 0 || (size && !data) || (size_t)size > VST_STATE_LIMIT - b->position) {
		b->failed = 1;
		return Steinberg_kInvalidArgument;
	}
	size_t end = b->position + size;
	if (end > b->capacity) {
		size_t capacity = end > 2 * b->capacity ? end : 2 * b->capacity;
		if (capacity > VST_STATE_LIMIT)
			capacity = VST_STATE_LIMIT;
		unsigned char *p = realloc(b->data, capacity);
		if (!p) {
			b->failed = 1;
			return Steinberg_kOutOfMemory;
		}
		b->data = p;
		b->capacity = capacity;
	}
	if (b->position > b->length)
		memset(b->data + b->length, 0, b->position - b->length);
	if (size)
		memcpy(b->data + b->position, data, size);
	b->position = end;
	if (end > b->length)
		b->length = end;
	if (written)
		*written = size;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE stream_seek(
    void *s, Steinberg_int64 offset, Steinberg_int32 mode, Steinberg_int64 *result) {
	VstStream *b = s;
	int64_t base = mode == 0 ? 0 : mode == 1 ? (int64_t)b->position : (int64_t)b->length;
	if (mode < 0 || mode > 2 || offset < -base || offset > VST_STATE_LIMIT - base)
		return Steinberg_kInvalidArgument;
	b->position = base + offset;
	if (result)
		*result = b->position;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE stream_tell(void *s, Steinberg_int64 *position) {
	if (!position)
		return Steinberg_kInvalidArgument;
	*position = ((VstStream *)s)->position;
	return Steinberg_kResultOk;
}
VstStream *vst_stream_new(void) {
	static Steinberg_IBStreamVtbl table = {
	    stream_query, stream_ref, stream_release, stream_read, stream_write, stream_seek, stream_tell};
	VstStream *b = calloc(1, sizeof(*b));
	if (b) {
		b->api.lpVtbl = &table;
		b->refs = 1;
	}
	return b;
}
