let sharedFloatView = null;

// 1. Capture the memory when C++ posts it after the handshake
window.chrome.webview.addEventListener('sharedbufferreceived', (e) => {
    const rawBuffer = e.getBuffer();
    sharedFloatView = new Float32Array(rawBuffer);
    console.log(`Oscilloscope buffer linked. Capacity: ${sharedFloatView.length} samples.`);
});

// 2. Process incoming sync notifications
window.chrome.webview.addEventListener('message', (e) => {
    if (typeof e.data === "string" && e.data.startsWith("sync") && sharedFloatView) {
        const parts = e.data.split(":");
        const startIndex = parseInt(parts[1], 10);
        const amount = parseInt(parts[2], 10);

        console.log(`Received: ${amount} samples. First 3: ${sharedFloatView[startIndex]}, ${sharedFloatView[startIndex+1]}, ${sharedFloatView[startIndex+2]}`);
    }
});

// 3. Inform C++ that DOM and listeners are initialized
window.addEventListener('DOMContentLoaded', () => {
    window.chrome.webview.postMessage("ready");
});