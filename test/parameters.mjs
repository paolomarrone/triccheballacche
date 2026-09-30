import assert from "node:assert/strict";
import {parameterRatio, parameterValue, parameterText} from "../editor/parameters.js";

for (const p of [
    {minimum: -1, maximum: 1},
    {minimum: 20, maximum: 20000, map: "logarithmic"},
    {minimum: -20000, maximum: -20, map: "logarithmic"},
    {minimum: 0, maximum: 1, map: "logarithmic"}
]) {
    for (const ratio of [0, .1, .5, .9, 1])
        assert(Math.abs(parameterRatio(p, parameterValue(p, ratio)) - ratio) < 1e-12);
    assert.equal(parameterRatio(p, p.minimum - 1), 0);
    assert.equal(parameterRatio(p, p.maximum + 1), 1);
    assert.equal(parameterValue(p, -1), p.minimum);
    assert(Math.abs(parameterValue(p, 2) - p.maximum) < 1e-10);
}
for (const map of [undefined, "logarithmic"]) {
    const fixed = {minimum: 5, maximum: 5, map};
    assert.equal(parameterRatio(fixed, 5), 0, "A fixed logarithmic range must not produce NaN");
    assert.equal(parameterValue(fixed, .5), 5);
}
assert.equal(parameterText({unit: "hz"}, 200), "200 Hz");
assert.equal(parameterText({unit: "db"}, -6), "-6 dB");
assert.equal(parameterText({unit: "oct"}, 2), "2 oct");
assert.equal(parameterText({toggled: true}, 1), "On");
assert.equal(parameterText({isBypass: true}, 0), "Off");
assert.equal(parameterText({scalePoints: {Pulse: 90, Sine: 10, Saw: 20}}, 10), "Sine");
assert.equal(parameterText({scalePoints: {"": 0}}, 0), "");
console.log("OK: shared parameter scales, fixed ranges, units and labels");
