// Discrete modes retain MDA's normalized parameter values.
const modes = (labels, factor = labels.length - .1) => Object.fromEntries(labels.map((label, i) => [label, Math.min(1, (i + .5) / factor)]));
export const choices = {
    bandisto: {0: {Low: .1, Mid: .3, High: .5, Output: 1}, 9: {Bipolar: 0, Unipolar: 1}},
    beatbox: {10: modes(["Off", "Monitor", "Hat", "Kick", "Snare"])},
    combo: {0: modes(["DI", "Speaker", "Radio", 'MB 1″', 'MB 8″', "4×12 ↑", "4×12 →"]), 4: {Mono: 0, Stereo: 1}},
    dither: {1: modes(["Off", "Triangular", "HP triangular", "Shaped"])},
    envelope: {0: modes(["L × |R|", "In / Env", "Flat / Env"])},
    image: {0: modes(["SM → LR", "MS → LR", "LR → LR", "LR → MS"])},
    jx10: {3: {Poly: .1, "Poly legato": .32, "Poly glide": .45, Mono: .65, "Mono legato": .83, "Mono glide": .95}},
    leslie: {0: {Stop: 0, Slow: .25, Fast: 1}},
    limiter: {4: {Hard: 0, Soft: 1}},
    multiband: {0: {Low: 0, Mid: .2, High: .4, Output: 1}, 12: {Mid: 0, Side: 1}},
    repsycho: {6: {Low: 0, High: 1}},
    shepard: {0: modes(["Tones", "Ring mod", "Tones + In"])},
    splitter: {0: modes(["Normal", "Inverse", "N / I", "I / N"]), 2: modes(["Below", "All", "Above"]), 4: modes(["Below", "All", "Above"])},
    subsynth: {0: modes(["Distort", "Divide", "Invert", "Key osc"], 3.5)},
    talkbox: {2: {Right: 0, Left: 1}},
    testtone: {0: modes(["MIDI", "Impulse", "White", "Pink", "Off", "Sine", "Log sweep", "Log step", "Lin sweep"]), 2: {Left: 0, Center: .5, Right: 1}},
    tracker: {0: modes(["Sine", "Square", "Saw", "Ring", "EQ"])},
    vocinput: {0: modes(["Off", "Free", "Quantized"])},
    vocoder: {0: {Left: 0, Right: 1}, 7: {"8 bands": 0, "16 bands": 1}}
};
export const toggles = {dx10: [16], looplex: [1, 2], loudness: [2]};

export function modeIndex(name, index, value) {
    const entries = Object.entries(choices[name]?.[index] || {});
    if (name === "bandisto" && index === 9) return Math.floor(1.99 * value);
    if (entries.length === 2) return (name === "vocoder" && index === 7 ? value >= .5 : value > .5) ? 1 : 0;
    if (name === "bandisto" && index === 0) return Math.min(3, Math.floor(value * 5));
    if (name === "multiband" && index === 0) return value < .1 ? 0 : value < .3 ? 1 : value < .5 ? 2 : 3;
    if (name === "jx10") return [0, 0, 1, 2, 3, 3, 4, 5][Math.floor(value * 7.9)];
    if (name === "leslie") return value < .1 ? 0 : value < .5 ? 1 : 2;
    if (name === "testtone" && index === 2) return value <= .3 ? 0 : value <= .7 ? 1 : 2;
    const factor = {shepard: 2.95, vocinput: 2.99, subsynth: 3.5}[name] ?? entries.length - .1;
    return Math.min(entries.length - 1, Math.floor(value * factor));
}

// Readouts with sample-rate-independent units. Unmapped controls retain 0–100%.
const gains = {
    ambience: [3], bandisto: [6, 7, 8], combo: [3], deess: [2], degrade: [5],
    image: [5], leslie: [7], limiter: [1], overdrive: [2], rezfilter: [2],
    shepard: [2], splitter: [6], tracker: [7], transient: [2], vocoder: [1]
};
export function readout(name, index, value) {
    const fixed = (v, unit) => `${Number(v.toFixed(2))} ${unit}`;
    if (gains[name]?.includes(index)) return fixed(40 * value - 20, "dB");
    if (name === "bandisto" && index >= 3 && index <= 5) return fixed(60 * value, "dB");
    if (name === "deess" && index === 0) return fixed(60 * value - 60, "dB");
    if (name === "deess" && index === 1) return fixed(1 + 11 * value, "kHz");
    if (name === "degrade" && index === 1) return fixed(Math.floor(4 + 12 * value), "bit");
    if (name === "dither" && index === 0) return fixed(8 + 2 * Math.floor(8.9 * value), "bit");
    if (name === "looplex" && index === 0) return fixed(Math.floor(10.5 + 190 * value), "s");
    if (name === "ringmod" && index === 0) return fixed(100 * Math.floor(160 * value), "Hz");
    if (name === "ringmod" && index === 1) return fixed(100 * value, "Hz");
    if (name === "roundpan" && index === 0) return fixed(360 * (value - .5), "°");
    if (name === "limiter" && index === 0) return fixed(40 * value - 40, "dB");
    if (name === "dx10" && index === 15) return fixed(25 * value * value, "Hz");
    if (name === "epiano" && index === 5) return fixed(Math.exp(6.22 * value - 2.61), "Hz");
    if (name === "rezfilter" && index === 7) return fixed(10 ** (4 * value - 2), "Hz");
    return `${(value * 100).toFixed(1)}%`;
}
