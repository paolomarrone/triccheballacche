import assert from "node:assert/strict";
import {getEventListeners} from "node:events";
import {MessageChannel} from "node:worker_threads";
import {workletReply} from "../web/request.js";

const {port1, port2} = new MessageChannel();
const node = Object.assign(new EventTarget(), {port: port1});
const request = (type, options = {}) => workletReply(node, type, {send: true, ...options});
try {
    const ready = workletReply(node, "ready");
    port2.postMessage({type: "ready"});
    await ready;
    port2.onmessage = ({data}) => port2.postMessage({type: data.type});
    await request("prepare");

    port2.onmessage = ({data}) => port2.postMessage({type: data.type, error: "DSP initialization failed"});
    await assert.rejects(request("prepare"), /DSP initialization failed/);

    // An old/malformed response must not acknowledge the current operation.
    port2.onmessage = () => { port2.postMessage(null); port2.postMessage({type: "prepare"}); };
    await assert.rejects(request("close", {timeout: 20}), /timed out/);
    port2.onmessage = ({data}) => port2.postMessage({type: data.type});
    await request("close");

    port2.onmessage = () => {};
    const failed = request("close");
    node.dispatchEvent(new Event("processorerror"));
    await assert.rejects(failed, /communication failed/);

    const unreadable = request("close");
    port1.dispatchEvent(new Event("messageerror"));
    await assert.rejects(unreadable, /communication failed/);

    const post = port1.postMessage;
    port1.postMessage = () => { throw Error("send failed"); };
    await assert.rejects(request("close"), /send failed/);
    port1.postMessage = post;
    assert.equal(getEventListeners(port1, "message").length, 0);
    assert.equal(getEventListeners(port1, "messageerror").length, 0);
    assert.equal(getEventListeners(node, "processorerror").length, 0);
    console.log("OK: worklet replies, stale messages, timeouts, processor/message errors and listener cleanup");
} finally {
    port1.close();
    port2.close();
}
