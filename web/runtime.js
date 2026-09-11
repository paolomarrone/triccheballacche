// The setup processor and miniaudio share this AudioWorklet scope.
if (ENVIRONMENT_IS_AUDIO_WORKLET) globalThis.peroneHost = Module;
