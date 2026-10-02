import { changeConnState, changeVoltageMode } from "/DOMelementFuncs.js";
import { appendGraphData } from "./graph.js"; // IMPORT THE NEW FUNCTION

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
        const strParts = e.data.split(":");
        const cmdType = strParts[0];
        
        if (cmdType == "sync" && sharedFloatView){
            const startIndex = parseInt(strParts[1], 10);
            const amount = parseInt(strParts[2], 10);
            appendGraphData(sharedFloatView, startIndex, amount); // Push directly to the graph buffer
        } else if (cmdType == "connState"){
            changeConnState(strParts[1] == "true" ? true : false);
        } else if (cmdType == "mode"){
            changeVoltageMode(strParts[1]);
        }
    }
});

// Inform C++ that DOM and listeners are initialized
window.addEventListener('DOMContentLoaded', () => {
    window.chrome.webview.postMessage("ready:dom");
});