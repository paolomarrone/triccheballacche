// Run on the build machine: metadata and factory programs come from the DSP itself.
#include "mda.h"
#include "dsp.h"

static void quoted(const char *text) {
	putchar('"');
	for (; *text; ++text) {
		if (*text == '"' || *text == '\\')
			putchar('\\');
		if ((unsigned char)*text >= 32)
			putchar(*text);
	}
	putchar('"');
}

int main() {
	AudioEffectX *dsp = createEffectInstance(true);
	printf("{\"inputs\":%d,\"midi\":%d,\"parameters\":[", dsp->numInputs, dsp->getNumMidiInputChannels());
	for (int i = 0; i < dsp->numParams; ++i) {
		char name[256] = {};
		dsp->getParameterName(i, name);
		if (i)
			putchar(',');
		printf("{\"name\":");
		quoted(name);
		printf(",\"defaultValue\":%.9g}", dsp->getParameter(i));
	}
	printf("],\"presets\":[");
	for (int i = 0; i < dsp->numPrograms; ++i) {
		char name[256] = {};
		dsp->setProgram(i);
		dsp->getProgramName(name);
		if (i)
			putchar(',');
		printf("{\"name\":");
		quoted(name);
		printf(",\"values\":[");
		for (int j = 0; j < dsp->numParams; ++j)
			printf("%s%.9g", j ? "," : "", dsp->getParameter(j));
		printf("]}");
	}
	printf("]}\n");
	delete dsp;
}
