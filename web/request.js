// Initial readiness is unsolicited; close requests are sent after listeners are installed.
export function workletReply(node, type, {send = false, timeout = 10000} = {}) {
    return new Promise((resolve, reject) => {
        const {port} = node;
        const finish = error => {
            clearTimeout(timer);
            port.removeEventListener("message", reply);
            port.removeEventListener("messageerror", failed);
            node.removeEventListener("processorerror", failed);
            error ? reject(error) : resolve();
        };
        const reply = ({data}) => {
            if (data?.type === type) finish(data.error ? Error(data.error) : null);
        };
        const failed = () => finish(Error(`Worklet ${type}: communication failed`));
        const timer = setTimeout(() => finish(Error(`Worklet ${type}: timed out`)), timeout);
        port.addEventListener("message", reply);
        port.addEventListener("messageerror", failed);
        node.addEventListener("processorerror", failed);
        try {
            port.start();
            if (send) port.postMessage({type});
        } catch (error) { finish(error); }
    });
}
