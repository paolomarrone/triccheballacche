// Test-only recorder downstream of miniaudio. Production processing does not allocate these buffers.
registerProcessor("capture", class extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.audio = new Float32Array((options.processorOptions.frames + 512) * 2);
        this.position = 0;
        this.started = false;
        this.port.onmessage = () => {
            this.port.postMessage({audio: this.audio, frames: this.position}, [this.audio.buffer]);
        };
    }

    process([input], [output]) {
        if (!input.length) return true;
        if (!this.started && input[0].some(value => value !== 0)) this.started = true;
        for (let c = 0; c < output.length; c++) output[c].set(input[c]);
        if (this.started) {
            for (let i = 0; i < input[0].length && this.position * 2 < this.audio.length; i++, this.position++)
                for (let c = 0; c < 2; c++) this.audio[2 * this.position + c] = input[c][i];
        }
        return true;
    }
});
