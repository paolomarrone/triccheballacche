// Emscripten 6.0.9 unconditionally suspends even a closed context when unregistering it.
// We close and await the context before releasing C memory shared with its worklet.
addToLibrary({
    emscripten_destroy_audio_context__deps: ["$emAudio"],
    emscripten_destroy_audio_context: handle => {
        const context = emAudio[handle];
        if (context && context.state !== "closed") context.suspend();
        delete emAudio[handle];
    }
});
