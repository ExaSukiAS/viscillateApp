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