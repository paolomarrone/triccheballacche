// Initial readiness is unsolicited. Sent requests carry IDs, so a late reply cannot satisfy the next one.
let sequence = 0;
export function workletReply(node, type, {send = false, payload = {}, timeout = 10000} = {}) {
    return new Promise((resolve, reject) => {
        const {port} = node, id = send ? ++sequence : undefined;
        const finish = (error, result) => {
            clearTimeout(timer);
            port.removeEventListener("message", reply);
            port.removeEventListener("messageerror", failed);
            node.removeEventListener("processorerror", failed);
            error ? reject(error) : resolve(result);
        };
        const reply = ({data}) => {
            if (data?.type === type && data.id === id) finish(data.error ? Error(data.error) : null, data.result);
        };
        const failed = () => finish(Error(`Worklet ${type}: communication failed`));
        const timer = setTimeout(() => finish(Error(`Worklet ${type}: timed out`)), timeout);
        port.addEventListener("message", reply);
        port.addEventListener("messageerror", failed);
        node.addEventListener("processorerror", failed);
        try {
            port.start();
            if (send) port.postMessage({...payload, type, id});
        } catch (error) { finish(error); }
    });
}
