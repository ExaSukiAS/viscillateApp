import { changeGraphYRange } from "./graph.js";

const timeFrameSlider = document.querySelector("#timeFrameSlider");
const voltageLimitSlider = document.querySelector("#voltageLimitSlider");
const voltageOffsetSlider = document.querySelector("#voltageOffsetSlider");

const timeFrameTextBox = document.querySelector("#timeFrameTextBox");
const voltageLimitTextBox = document.querySelector("#voltageLimitTextBox");
const voltageOffsetTextBox = document.querySelector("#voltageOffsetTextBox");

let currentTimeFrame = 100;
let currentVoltageLimit = 50;
let currentVoltageOffset = 0;

// debounce function 
function debounce(func, delay = 400) {
  let timer;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => func(...args), delay);
  };
}

// this function is is fired whenever graph settings are changed (debounced)
const updateGraphSettings = debounce(() => {
  window.chrome.webview.postMessage(`graphSettings:${currentTimeFrame}:${currentVoltageLimit}:${currentVoltageOffset}`); // inform C++ of the new settings
  changeGraphYRange(currentVoltageOffset - currentVoltageLimit, currentVoltageOffset + currentVoltageLimit); // update the graph's Y range
}, 400);


// Calculates the exact horizontal position for the floating textbox relative to the slider thumb position.
function updateFloatingInputPosition(sliderElement, textBoxElement) {
  const min = parseFloat(sliderElement.min) || 0;
  const max = parseFloat(sliderElement.max) || 100;
  const val = parseFloat(sliderElement.value) || 0;

  const thumbWidth = 20;
  const percent = (val - min) / (max - min || 1);

  // Calculate pixel offset accounting for thumb radius centered over track ends
  const offset = percent * (sliderElement.clientWidth - thumbWidth) + thumbWidth / 2;

  // Center the floating box over the thumb (using CSS transform for centering)
  textBoxElement.style.left = `${offset}px`;
  textBoxElement.style.transform = `translateX(-50%)`;
}

// handler to sync inputs, update floating position, and debounce updates
function handleGraphSettingChange(sourceElement, sliderElement, textBoxElement, updateVariable) {
  const value = parseFloat(sourceElement.value) || 0;

  // Sync value between slider and textbox
  sliderElement.value = value;
  textBoxElement.value = value;

  // Update state variable
  updateVariable(value);

  // Position textbox directly above thumb
  updateFloatingInputPosition(sliderElement, textBoxElement);

  // Debounce graph rendering call
  updateGraphSettings();
}

// Slider event listeners
timeFrameSlider.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, timeFrameSlider, timeFrameTextBox, (val) => currentTimeFrame = val);
});

voltageLimitSlider.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageLimitSlider, voltageLimitTextBox, (val) => currentVoltageLimit = val);
});

voltageOffsetSlider.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageOffsetSlider, voltageOffsetTextBox, (val) => currentVoltageOffset = val);
});

// Textbox event listeners
timeFrameTextBox.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, timeFrameSlider, timeFrameTextBox, (val) => currentTimeFrame = val);
});

voltageLimitTextBox.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageLimitSlider, voltageLimitTextBox, (val) => currentVoltageLimit = val);
});

voltageOffsetTextBox.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageOffsetSlider, voltageOffsetTextBox, (val) => currentVoltageOffset = val);
});

// Initialize positions on page load and window resize
function initializePositions() {
  updateFloatingInputPosition(timeFrameSlider, timeFrameTextBox);
  updateFloatingInputPosition(voltageLimitSlider, voltageLimitTextBox);
  updateFloatingInputPosition(voltageOffsetSlider, voltageOffsetTextBox);
}

window.addEventListener('load', initializePositions);
window.addEventListener('resize', initializePositions);