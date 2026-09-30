// Metadata has one meaning for parameter controls and score automation.
const units = {db: "dB", hz: "Hz", khz: "kHz", pc: "%"};
const format = new Intl.NumberFormat(undefined, {maximumFractionDigits: 3});
const logarithmic = p => p.map === "logarithmic" && p.minimum * p.maximum > 0 && p.minimum !== p.maximum;

export function parameterRatio(p, value) {
    value = Math.max(Math.min(p.minimum, p.maximum), Math.min(Math.max(p.minimum, p.maximum), value));
    const ratio = logarithmic(p) ? Math.log(value / p.minimum) / Math.log(p.maximum / p.minimum) :
        (value - p.minimum) / (p.maximum - p.minimum || 1);
    return Math.max(0, Math.min(1, ratio));
}

export function parameterValue(p, ratio) {
    ratio = Math.max(0, Math.min(1, ratio));
    return logarithmic(p) ? p.minimum * Math.pow(p.maximum / p.minimum, ratio) : p.minimum + (p.maximum - p.minimum) * ratio;
}

export function parameterText(p, value) {
    const label = Object.entries(p.scalePoints || {}).find(([, number]) => number === value)?.[0];
    if (label !== undefined) return label;
    if (p.toggled || p.isBypass) return value >= .5 ? "On" : "Off";
    return format.format(value) + (p.unit ? ` ${units[p.unit] || p.unit}` : "");
}
