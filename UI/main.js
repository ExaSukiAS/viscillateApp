import { changeConnState } from "/DOMelementFuncs.js";

let sharedFloatView = null;

// Capture the memory when C++ posts it after the handshake
window.chrome.webview.addEventListener('sharedbufferreceived', (e) => {
    const rawBuffer = e.getBuffer();
    sharedFloatView = new Float32Array(rawBuffer);
    console.log(`Oscilloscope buffer linked. Capacity: ${sharedFloatView.length} samples.`);
});

// Process incoming sync notifications
window.chrome.webview.addEventListener('message', (e) => {
    if (typeof e.data === "string") {
        const strParts = e.data.split(":"); // every message that comes is in colon-saperated format
        if (strParts[0] == "sync" && sharedFloatView){
            const startIndex = parseInt(strParts[1], 10);
            const amount = parseInt(strParts[2], 10);
            console.log(`Received: ${amount} samples. First 3: ${sharedFloatView[startIndex]}, ${sharedFloatView[startIndex+1]}, ${sharedFloatView[startIndex+2]}`);
        } else if (strParts[0] == "connState"){
            changeConnState(strParts[1] == "true" ? true : false);
        }
    }
});

//  Inform C++ that DOM and listeners are initialized
window.addEventListener('DOMContentLoaded', () => {
    window.chrome.webview.postMessage("ready");
});