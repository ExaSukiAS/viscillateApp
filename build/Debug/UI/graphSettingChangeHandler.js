const timeFrameSlider = document.querySelector("#timeFrameSlider");
const voltageLimitSlider = document.querySelector("#voltageLimitSlider");
const voltageOffestSlider = document.querySelector("#voltageOffestSlider");

const timeFrameTextBox = document.querySelector("#timeFrameTextBox");
const voltageLimitTextBox = document.querySelector("#voltageLimitTextBox");
const voltageOffestTextBox = document.querySelector("#voltageOffestTextBox");

export let currentTimeFrame = 100;
export let currentVoltageLimit = 50;
export let currentVoltageOffset = 0;

// Reusable debounce function (fixed typo from clearTime to clearTimeout)
function debounce(func, delay = 150) {
  let timer;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => func(...args), delay);
  };
}

// Function to notify heavy visual or graph recalculations
const updateGraph = debounce(() => {
  console.log("Graph updated with:", {
    currentTimeFrame,
    currentVoltageLimit,
    currentVoltageOffset
  });
}, 150);

/**
 * Calculates the exact horizontal position for the floating textbox 
 * relative to the slider thumb position.
 */
function updateFloatingInputPosition(sliderElement, textBoxElement) {
  const min = parseFloat(sliderElement.min) || 0;
  const max = parseFloat(sliderElement.max) || 100;
  const val = parseFloat(sliderElement.value) || 0;

  // Thumb width in pixels (matches CSS thumb width of 20px)
  const thumbWidth = 20;

  // Percentage value along the range [0, 1]
  const percent = (val - min) / (max - min || 1);

  // Calculate pixel offset accounting for thumb radius centered over track ends
  const offset = percent * (sliderElement.clientWidth - thumbWidth) + thumbWidth / 2;

  // Center the floating box over the thumb (using CSS transform for centering)
  textBoxElement.style.left = `${offset}px`;
  textBoxElement.style.transform = `translateX(-50%)`;
}

// Generic handler to sync inputs, update floating position, and debounce updates
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
  updateGraph();
}

// Slider event listeners
timeFrameSlider.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, timeFrameSlider, timeFrameTextBox, (val) => currentTimeFrame = val);
});

voltageLimitSlider.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageLimitSlider, voltageLimitTextBox, (val) => currentVoltageLimit = val);
});

voltageOffestSlider.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageOffestSlider, voltageOffestTextBox, (val) => currentVoltageOffset = val);
});

// Textbox event listeners
timeFrameTextBox.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, timeFrameSlider, timeFrameTextBox, (val) => currentTimeFrame = val);
});

voltageLimitTextBox.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageLimitSlider, voltageLimitTextBox, (val) => currentVoltageLimit = val);
});

voltageOffestTextBox.addEventListener('input', (e) => {
  handleGraphSettingChange(e.target, voltageOffestSlider, voltageOffestTextBox, (val) => currentVoltageOffset = val);
});

// Initialize positions on page load and window resize
function initializePositions() {
  updateFloatingInputPosition(timeFrameSlider, timeFrameTextBox);
  updateFloatingInputPosition(voltageLimitSlider, voltageLimitTextBox);
  updateFloatingInputPosition(voltageOffestSlider, voltageOffestTextBox);
}

window.addEventListener('load', initializePositions);
window.addEventListener('resize', initializePositions);