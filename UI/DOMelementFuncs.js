let isGraphPlaying = true;

export function changeConnState(newState){
    const connIndicatorDiv = document.querySelector(".connectionIndicator");
    const connText = document.querySelector(".connectionText"); 

    if(newState == true){
        connIndicatorDiv.classList.add('connected');
        connText.textContent = 'Connected'; 
    } else {
        connIndicatorDiv.classList.remove('connected');
        connText.textContent = 'Disconnected'; 
    }
}

export function changeVoltageMode(newMode){
    console.log(newMode);
    if(newMode == "0"){
        document.querySelector("#vMode50").classList.add('active');
        document.querySelector("#vMode20").classList.remove('active');
        document.querySelector("#vMode8").classList.remove('active');
    } else if(newMode == "1"){
        document.querySelector("#vMode50").classList.remove('active');
        document.querySelector("#vMode20").classList.add('active');
        document.querySelector("#vMode8").classList.remove('active');
    } else if(newMode == "2"){
        document.querySelector("#vMode50").classList.remove('active');
        document.querySelector("#vMode20").classList.remove('active');
        document.querySelector("#vMode8").classList.add('active');
    }
}

let isPlaying = false;
export function handlePlayPauseBtnClick() {
    isPlaying = !isPlaying;
    const btnElement = document.querySelector('.playPauseBtn');
    if(isPlaying){
        btnElement.innerHTML = `
            <svg xmlns="http://www.w3.org/2000/svg" height="24px" viewBox="0 -960 960 960" width="24px" fill="#e3e3e3">
                <path d="M520-200v-560h240v560H520Zm-320 0v-560h240v560H200Zm400-80h80v-400h-80v400Zm-320 0h80v-400h-80v400Zm0-400v400-400Zm320 0v400-400Z"/>
            </svg>
            Pause`;
    } else {
        btnElement.innerHTML = `
            <svg xmlns="http://www.w3.org/2000/svg" height="24px" viewBox="0 -960 960 960" width="24px" fill="#e3e3e3">
                <path d="M320-200v-560l440 280-440 280Zm80-280Zm0 134 210-134-210-134v268Z" />
            </svg>
            Play`;
    }
    return isPlaying;
}

export function togglePlayPauseBtnActivation(newState){
    const btnElement = document.querySelector('.playPauseBtn');
    if(newState){
        btnElement.classList.remove('inactive');
    } else {
        btnElement.classList.add('inactive');
    }
}

export function toggleSignalAnalyticsChanging(toggle){
    if(toggle){
        isGraphPlaying = true;
    } else {
        isGraphPlaying = false;
    }
}

export function updateSignalAnalytics(currentVoltage, Vmax, Vmin, Vpp, Vrms, freq, periodMS, dutyCycle){
    if(isGraphPlaying){
        const VppElement = document.querySelector('#Vpp');
        const VrmsElement = document.querySelector('#Vrms');
        const VmaxElement = document.querySelector('#Vmax');
        const VminElement = document.querySelector('#Vmin');
        const freqElement = document.querySelector('#freq');
        const dutyElement = document.querySelector('#duty');
        const periodElement = document.querySelector('#timePeriod');
        const voltageElement = document.querySelector('#voltage');

        const formatTwoDecimals = (value) => {
            const numericValue = Number(value);
            return Number.isFinite(numericValue) ? numericValue.toFixed(2) : '0.00';
        };

        VppElement.innerHTML = `Vpp: ${formatTwoDecimals(Vpp)}V`;
        VrmsElement.innerHTML = `Vrms: ${formatTwoDecimals(Vrms)}V`;
        VmaxElement.innerHTML = `Vmax: ${formatTwoDecimals(Vmax)}V`;
        VminElement.innerHTML = `Vmin: ${formatTwoDecimals(Vmin)}V`;
        freqElement.innerHTML = `Freq: ${formatTwoDecimals(freq)}Hz`;
        dutyElement.innerHTML = `Duty: ${formatTwoDecimals(dutyCycle)}%`;
        periodElement.innerHTML = `Period: ${formatTwoDecimals(periodMS)}ms`;
        voltageElement.innerHTML = `Voltage: ${formatTwoDecimals(currentVoltage)}V`;
    }
}

export function navitageToPage(page){
    const voltmeterPage = document.querySelector(".main-voltmeter");
    const oscilloscopePage = document.querySelector(".main-oscilloscope");
    const highlightPill = document.querySelector(".highlightPill");

    if(page === 'voltmeter'){
        voltmeterPage.classList.remove("inactive");
        highlightPill.classList.remove("oscilloscope");

        highlightPill.classList.add("voltmeter");
        oscilloscopePage.classList.add("inactive");
    } else {
        oscilloscopePage.classList.remove("inactive");
        highlightPill.classList.remove("voltmeter");

        highlightPill.classList.add("oscilloscope");
        voltmeterPage.classList.add("inactive");
    }
}