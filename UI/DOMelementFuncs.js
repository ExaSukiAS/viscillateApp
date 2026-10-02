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