// graph.js
const graphContainer = document.querySelector(".graphContainer");

const X_DIVS = 10;
const Y_DIVS = 10;

// fetch CSS colors
const styles = getComputedStyle(document.documentElement);
const colors = {};
const colorKeys = ['primary','primary-light','secondary','tertiary','error','success','background','background-light','background-lighter','text'];
colorKeys.forEach((key) => {
    const value = styles.getPropertyValue(`--color-${key}`).trim();
    if (value) colors[key] = value;
});


const MAX_POINTS = 1000; // Set this to match C++ 'UI.maxPointsPerGraphFrame'

const xs = new Float32Array(MAX_POINTS);
const ys = new Float32Array(MAX_POINTS);

for (let i = 0; i < MAX_POINTS; i++) {
    xs[i] = i; 
}

const data = [xs, ys];

const opts = {
    width: graphContainer.clientWidth,
    height: graphContainer.clientHeight,
    legend: { show: false },
    scales: {
        x: { time: false, range: [0, MAX_POINTS] }, 
        y: { range: [-50, 50] }, // Change range depending on your voltage needs
    },
    axes: [
        {
            stroke: colors.text,
            grid:  { show: true, stroke: colors['background-light'], width: 1 },
            ticks: { show: true, stroke: colors.text, width: 1, size: 6 },
            splits: (u, axisIdx, min, max) => {
                const step = (max - min) / X_DIVS;
                return Array.from({ length: X_DIVS + 1 }, (_, i) => min + i * step);
            },
        },
        {
            stroke: colors.text,
            grid:  { show: true, stroke: colors['background-light'], width: 1 },
            ticks: { show: true, stroke: colors.text, width: 1, size: 6 },
            splits: (u, axisIdx, min, max) => {
                const step = (max - min) / Y_DIVS;
                return Array.from({ length: Y_DIVS + 1 }, (_, i) => min + i * step);
            },
        },
    ],
    series: [
        {},
        { stroke: colors.primary, width: 2, points: { show: false } },
    ],
};

const u = new uPlot(opts, data, graphContainer);

export function changeGraphYRange(min, max) {
    u.setScale('y', { range: [min, max] });
    isDirty = true;
}

let isDirty = false;

export function appendGraphData(sharedView, startIndex, amount) {
    if (amount <= 0) return;

    // Create a fast, zero-copy reference to the new data chunk
    const newSamples = sharedView.subarray(startIndex, startIndex + amount);

    if (amount >= MAX_POINTS) {
        // If the new chunk is bigger than the whole screen, just overwrite everything
        ys.set(newSamples.subarray(amount - MAX_POINTS));
    } else {
        ys.copyWithin(0, amount, MAX_POINTS);
        ys.set(newSamples, MAX_POINTS - amount);
    }

    isDirty = true; // Flag that data changed
}

// Decouples drawing from receiving. The graph will ONLY redraw when the screen is ready for the next frame, saving massive amounts of CPU/GPU overhead.
function renderLoop() {
    if (isDirty) {
        // setData updates the internal pointers and redraws the canvas
        u.setData(data);
        isDirty = false;
    }
    requestAnimationFrame(renderLoop);
}
requestAnimationFrame(renderLoop);