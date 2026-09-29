#include <iostream>
#include <thread>
#include <atomic>
#include "UIhandler.h"
#include "serial.h"

UIhandler UI;
EspSerial serial(2000000, UI);

std::atomic<bool> isAppRunning = true; // flag to indicate if teh app is still running (only becomes false when the UI window is closed)
std::atomic<bool> espConnected = false; //  flag to indicate esp32 connection state
std::atomic<bool> isEspInitialized = false; // flag to if esp32 requires initialization commands to fetch gain and offset constants and start ADC streaming

float dummyData[512] = {0.0f};

int main() {
    std::thread serialThread([]() { while (isAppRunning) {
        if(!serial.isConnected()){
            espConnected = false;
            UI.sendMessageToJS("connState:false");
            std::cout << "Esp32-C3 not connected, scanning ports...\n";
            serial.scanForEsp(isAppRunning);
        } else {
            if(espConnected == false){
                UI.sendMessageToJS("connState:true");
                espConnected = true;
                isEspInitialized = false;
            }
        }
        Sleep(1000);
    }});

    std::thread espDataThread([]() { while (isAppRunning) {
        if(espConnected){
            if(isEspInitialized){
                serial.readADCChunkToSharedBuffer(); 
                Sleep(5); // small sleep so we don't cook the CPU, but fast enough to catch 70kHz data
            } else {
                if(serial.updateGainsAndOffsets() && serial.requestADCstream()) isEspInitialized = true;
            }
        }
    }});

    UI.launch(); // Runs indefinetly while window is open, only iterates to next line when window is closed
    
    isAppRunning = false; // Window was closed, tell the thread to stop looping

    serial.stopADCstream();

    // wait for the threads to exit the loop
    if (serialThread.joinable()) serialThread.join();
    if (espDataThread.joinable()) espDataThread.join();

    return 0;
}