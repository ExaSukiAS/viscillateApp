#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include <iostream>
#include <thread>
#include <atomic>
#include "UIhandler.h"
#include "serial.h"

UIhandler UI;
RPSerial serial(2000000, UI);

std::atomic<bool> isAppRunning = true; // flag to indicate if teh app is still running (only becomes false when the UI window is closed)
std::atomic<bool> rpConnected = false; //  flag to indicate rp2040 connection state
std::atomic<bool> isRpInitialized = false; // flag to if rp2040 requires initialization commands to fetch gain and offset constants and start ADC streaming

int main() {
    // attach terminal (ONLY FOR DEBUGGING PURPOSES, REMOVE FOR RELEASE)
    if (AllocConsole()) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
        std::ios::sync_with_stdio(true);
        std::cout.clear();
        std::cerr.clear();
    }
    
    std::thread serialThread([]() { 
        while (isAppRunning) {
            // Wait until the JavaScript UI is ready
            if (!UI.isJSinitialized) {
                Sleep(50); 
                continue;
            }

            if(!serial.isConnected()){
                if(rpConnected) UI.sendMessageToJS("connState:false");
                rpConnected = false;
                std::cout << "RP2040 not connected, scanning ports...\n";
                serial.scanForRp(isAppRunning);
            } else {
                if(rpConnected == false){
                    UI.sendMessageToJS("connState:true");
                    rpConnected = true;
                    isRpInitialized = false;
                }
            }
            Sleep(1000);
        }
    });

    std::thread rpDataThread([]() { 
        while (isAppRunning) {
            // Wait until the JavaScript UI is ready
            if (!UI.isJSinitialized) {
                Sleep(10); 
                continue;
            }

            if(rpConnected){
                if(isRpInitialized){
                    bool modeChanged; uint8_t newMode;
                    serial.readADCChunkToSharedBuffer(modeChanged, newMode);

                    std::string s = "mode:" + std::to_string(newMode);
                    if(modeChanged) UI.sendMessageToJS(s);
                } else {
                    if(serial.updateGainsAndOffsets() && serial.requestADCstream()) {
                        isRpInitialized = true;
                    } else {
                        Sleep(10);
                    }
                }
            } else {
                Sleep(50); 
            }
        }
    });

    UI.launch(); // Runs indefinetly while window is open, only iterates to next line when window is closed
    
    isAppRunning = false; // Window was closed, tell the thread to stop looping

    serial.stopADCstream();

    // wait for the threads to exit the loop
    if (serialThread.joinable()) serialThread.join();
    if (rpDataThread.joinable()) rpDataThread.join();

    return 0;
}