// Only explicitly saved text lives here. Published plugins and scripts remain in the catalog.
export async function storage() {
    const database = await new Promise((resolve, reject) => {
        const request = indexedDB.open("triccheballacche", 1);
        request.onupgradeneeded = () => request.result.createObjectStore("files", {keyPath: "path"});
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error);
        request.onblocked = () => reject(Error("Close other editor tabs to update local storage"));
    });
    database.onversionchange = () => database.close();
    function transaction(mode, run) {
        return new Promise((resolve, reject) => {
            const transaction = database.transaction("files", mode);
            const request = run(transaction.objectStore("files"));
            transaction.oncomplete = () => resolve(request.result);
            transaction.onabort = () => reject(transaction.error || Error("Local storage transaction failed"));
            transaction.onerror = () => {};
        });
    }
    return {
        files: () => transaction("readonly", store => store.getAll()),
        save: (path, text) => transaction("readwrite", store => store.put({path, text})),
        close: () => database.close()
    };
}
