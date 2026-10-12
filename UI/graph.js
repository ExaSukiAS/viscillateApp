import { handlePlayPauseBtnClick, toggleSignalAnalyticsChanging } from "/DOMelementFuncs.js";

const graphContainer = document.querySelector(".graphContainer");
const playPauseBtn = document.querySelector(".playPauseBtn");

let isPlaying = true; // Track the play/pause state of the graph

// number of visual divisions on the graph axes
const X_DIVS = 20;
const Y_DIVS = 20;

// fetch CSS colors
const styles = getComputedStyle(document.documentElement);
const colors = {};
const colorKeys = ['primary','primary-light','secondary','tertiary','error','success','background','background-light','background-lighter','text'];
colorKeys.forEach((key) => {
    const value = styles.getPropertyValue(`--color-${key}`).trim();
    if (value) colors[key] = value;
});

const MAX_POINTS = 1000; // Maximum number of points to display

const xs = new Float32Array(MAX_POINTS);
const ys = new Float32Array(MAX_POINTS);

for (let i = 0; i < MAX_POINTS; i++) {
    xs[i] = i; 
}

const data = [xs, ys];

let displayTimeMs = 100; // Default to 100ms
let graphVoltageRange = { min: -50, max: 50 }; // Default voltage range

// Graph interaction plugin (Hover Tooltip & Measuring Tool)
function interactionPlugin() {
    let hoveredIdx = null;
    let measureState = 0; // 0: none, 1: currently measuring (following mouse), 2: locked
    let startIdx = null;
    let endIdx = null;

    // Helper to draw arrowheads for the measuring line
    function drawArrowHead(ctx, x, y, dir, pr) {
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x - dir * 10 * pr, y - 5 * pr);
        ctx.lineTo(x - dir * 10 * pr, y + 5 * pr);
        ctx.closePath();
        ctx.fillStyle = colors.tertiary;
        ctx.fill();
    }

    return {
        hooks: {
            init: (u) => {
                const over = u.root.querySelector(".u-over");
                
                // Left click to start/finish measurement
                over.addEventListener("click", () => {
                    if (isPlaying || hoveredIdx === null) return;

                    if (measureState === 0 || measureState === 2) {
                        measureState = 1;
                        startIdx = hoveredIdx;
                        endIdx = hoveredIdx;
                    } else if (measureState === 1) {
                        measureState = 2;
                        endIdx = hoveredIdx;
                    }
                    u.redraw(false, false);
                });
                
                // Right click to cancel the measurement tool
                over.addEventListener("contextmenu", (e) => {
                    if (!isPlaying && measureState !== 0) {
                        e.preventDefault();
                        measureState = 0;
                        u.redraw(false, false);
                    }
                });
            },
            setCursor: (u) => {
                if (isPlaying) return;
                
                hoveredIdx = u.cursor.idx;
                if (measureState === 1 && hoveredIdx !== null) {
                    endIdx = hoveredIdx;
                }
                
                u.redraw(false, false); // Update graph to show floating tooltip/measurement line
            },
            draw: (u) => {
                // When playing, reset measurement states and don't draw
                if (isPlaying) {
                    measureState = 0;
                    return;
                }
                
                const ctx = u.ctx;
                const pr = window.devicePixelRatio || 1; // handle high DPI displays

                ctx.save();
                ctx.beginPath();
                ctx.rect(u.bbox.left, u.bbox.top, u.bbox.width, u.bbox.height);
                ctx.clip();
                ctx.translate(u.bbox.left, u.bbox.top);

                // Draw measurment tool
                if ((measureState === 1 || measureState === 2) && startIdx !== null && endIdx !== null) {
                    const t1 = (data[0][startIdx] / MAX_POINTS) * displayTimeMs;
                    const v1 = data[1][startIdx];
                    const t2 = (data[0][endIdx] / MAX_POINTS) * displayTimeMs;
                    const v2 = data[1][endIdx];

                    const x1 = u.valToPos(data[0][startIdx], 'x', true) - u.bbox.left;
                    const y1 = u.valToPos(data[1][startIdx], 'y', true) - u.bbox.top;
                    const x2 = u.valToPos(data[0][endIdx], 'x', true) - u.bbox.left;
                    const y2 = u.valToPos(data[1][endIdx], 'y', true) - u.bbox.top;

                    // Draw Dashed Arrows (1D horizontal with a vertical drop)
                    ctx.beginPath();
                    ctx.setLineDash([5 * pr, 5 * pr]);
                    ctx.moveTo(x1, y1);
                    ctx.lineTo(x2, y1); // Horizontal constraint
                    ctx.moveTo(x2, y1);
                    ctx.lineTo(x2, y2); // Vertical drop to target point
                    ctx.strokeStyle = colors.tertiary;
                    ctx.lineWidth = 1.5 * pr;
                    ctx.stroke();
                    ctx.setLineDash([]);

                    // Draw Arrow Heads pointing outward
                    if (Math.abs(x2 - x1) > 10 * pr) {
                        drawArrowHead(ctx, x1, y1, x1 < x2 ? -1 : 1, pr);
                        drawArrowHead(ctx, x2, y1, x1 < x2 ? 1 : -1, pr);
                    }

                    // Draw Delta T and Delta V Text on line
                    const deltaT = Math.abs(t2 - t1);
                    const deltaV = v2 - v1;
                    const text = `ΔT: ${deltaT.toFixed(2)}ms | ΔV: ${deltaV.toFixed(2)}V`;
                    
                    ctx.font = `${12 * pr}px 'Poppins-Bold', sans-serif`;
                    const metrics = ctx.measureText(text);
                    const midX = (x1 + x2) / 2;
                    
                    // Keep text above the line, unless clipped
                    let textY = y1 - 10 * pr;
                    if (textY < 20 * pr) textY = y1 + 20 * pr; 
                    
                    ctx.fillStyle = colors.text;
                    ctx.textAlign = "center";
                    ctx.textBaseline = "middle";
                    ctx.fillText(text, midX, textY-30);

                    // Draw exact voltages next to selected points
                    const padding = 8 * pr;
                    ctx.textAlign = x1 < u.bbox.width / 2 ? "left" : "right";
                    ctx.fillText(`${v1.toFixed(2)}V`, x1 + (x1 < u.bbox.width / 2 ? padding : -padding) - 17, y1-15);
                    
                    if (startIdx !== endIdx) {
                        ctx.textAlign = "left";
                        ctx.fillText(`${v2.toFixed(2)}V`, x2 - padding - 8, y2-15);
                    }
                }

                // DRAW HOVER TOOLTIP
                if (hoveredIdx !== null && measureState !== 1) {
                    const time = (data[0][hoveredIdx] / MAX_POINTS) * displayTimeMs;
                    const volt = data[1][hoveredIdx];
                    const cx = u.valToPos(data[0][hoveredIdx], 'x', true) - u.bbox.left;
                    const cy = u.valToPos(data[1][hoveredIdx], 'y', true) - u.bbox.top;

                    // Draw Highlight Dot
                    ctx.beginPath();
                    ctx.arc(cx, cy, 5 * pr, 0, 2 * Math.PI);
                    ctx.fillStyle = colors.secondary;
                    ctx.fill();

                    // Draw Floating Text
                    const hoverText = `${time.toFixed(1)}ms, ${volt.toFixed(2)}V`;
                    ctx.font = `${12 * pr}px 'Poppins', sans-serif`;
                    const hMetrics = ctx.measureText(hoverText);
                    
                    let tx = cx + 12 * pr;
                    let ty = cy - 12 * pr;
                    
                    // Keep floating tooltip on screen
                    if (tx + hMetrics.width + 10 * pr > u.bbox.width) tx = cx - hMetrics.width - 12 * pr;
                    if (ty - 10 * pr < 0) ty = cy + 20 * pr;

                    const borderRadius = 4 * pr; // Adjust the radius to your liking
                    ctx.beginPath();
                    ctx.roundRect(tx - 4 * pr, ty - 10 * pr, hMetrics.width + 9 * pr, 22 * pr, borderRadius);
                    ctx.fillStyle = colors['background-light'];
                    ctx.fill();
                    ctx.strokeStyle = colors.tertiary;
                    ctx.stroke();

                    ctx.fillStyle = colors.text;
                    ctx.textAlign = "left";
                    ctx.textBaseline = "middle";
                    ctx.fillText(hoverText, tx, ty+2);
                }

                ctx.restore();
            }
        }
    };
}
// -------------------------------------------------------------

const opts = {
    width: graphContainer.clientWidth,
    height: graphContainer.clientHeight,
    legend: { show: false },
    cursor: { drag: { x: false, y: false }, hover: {prox: false}}, // Allow our plugin to handle drawing
    plugins: [
        interactionPlugin() // Register the interaction plugin
    ],
    scales: {
        x: { time: false, range: [0, MAX_POINTS] },
        y: { 
            range: (self, dataMin, dataMax) => [graphVoltageRange.min, graphVoltageRange.max] 
        },
    },
    axes: [
        {
            font: "12px Poppins, sans-serif",
            stroke: colors.text,
            grid:  { show: true, stroke: colors['background-light'], width: 1 },
            ticks: { show: true, stroke: colors.text, width: 1, size: 6 },
            splits: (u, axisIdx, min, max) => {
                const step = (max - min) / X_DIVS;
                return Array.from({ length: X_DIVS + 1 }, (_, i) => min + i * step);
            },
            values: (u, splits) => splits.map(split => {
                const mappedValue = (split / MAX_POINTS) * displayTimeMs; 
                return Number.isInteger(mappedValue) ? mappedValue.toString() : mappedValue.toFixed(1);
            }),
        },
        {
            font: "12px Poppins, sans-serif",
            stroke: colors.text,
            grid:  { show: true, stroke: colors['background-light'], width: 1 },
            ticks: { show: true, stroke: colors.text, width: 1, size: 6 },
            splits: (u, axisIdx, min, max) => {
                const step = (max - min) / Y_DIVS;
                return Array.from({ length: Y_DIVS + 1 }, (_, i) => min + i * step);
            },
            values: (u, splits) => {
                return splits.map(split => split.toFixed(1));
            },
        },
    ],
    series: [
        {},
        { stroke: colors.primary, width: 2, points: { show: false } },
    ],
};

const u = new uPlot(opts, data, graphContainer);

// Automatically resize
const resizeObserver = new ResizeObserver((entries) => {
    for (let entry of entries) {
        const width = entry.contentRect.width;
        const height = entry.contentRect.height;
        if (width > 0 && height > 0) {
            u.setSize({ width, height });
        }
    }
});
resizeObserver.observe(graphContainer);


// called by graphSettingsHandler.js to update the graph's voltage range and time frame
export function changeGraphVoltageRange(min, max) {
    graphVoltageRange.min = min;
    graphVoltageRange.max = max;
    u.redraw(false, true);

    isDirty = true;
}
export function changeDisplayTimeFrame(ms) {
    displayTimeMs = ms;
    u.redraw(false, true);
    
    isDirty = true;
}

playPauseBtn.addEventListener("click", () => {
    isPlaying = handlePlayPauseBtnClick();
    toggleSignalAnalyticsChanging(isPlaying);
    if (!isPlaying) {
        // Redraw immediately to trigger plugin tools if cursor is already hovering
        u.root.querySelector(".u-over").style.cursor = "crosshair";
        requestAnimationFrame(() => u.redraw(false, false));
    } else {
        u.root.querySelector(".u-over").style.cursor = "default";
    }
});

let isDirty = false; // Flag to indicate if the graph needs to be redrawn

// Appends the new data to the graph's buffer and marks the graph as dirty for rendering.
export function appendGraphData(sharedView, startIndex, amount) {
    if (!isPlaying) return; // Only append data if the graph is in the playing state
    if (amount <= 0) return;

    const newSamples = sharedView.subarray(startIndex, startIndex + amount);

    if (amount >= MAX_POINTS) {
        ys.set(newSamples.subarray(amount - MAX_POINTS));
    } else {
        ys.copyWithin(0, amount, MAX_POINTS);
        ys.set(newSamples, MAX_POINTS - amount);
    }

    isDirty = true;
}

// Render loop only redraws if data is dirty, separating performance constraints from UI.
function renderLoop() {
    requestAnimationFrame(renderLoop);

    if (isDirty) {
        u.setData(data, false);
        u.redraw(); 
        isDirty = false;
    }
}
requestAnimationFrame(renderLoop);