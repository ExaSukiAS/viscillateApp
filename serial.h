#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regex>
#include <cstdint>
#include "UIhandler.h"

#pragma comment(lib, "setupapi.lib")

class EspSerial {
private:
    int baudRate;
    UIhandler& UI;

    // ESP32 commands
    uint8_t adcStreamReqByte = 0x01;
    uint8_t calibConstReqByte = 0x02;
    uint8_t pingByte = 0x03;

    // ESP32 states and ports
    bool espConnected = false;
    std::string espPortName;

    HANDLE hStream = INVALID_HANDLE_VALUE; // Persistent handle for streaming
    std::vector<uint8_t> streamRXBuffer;   // Buffer to stitch fragmented USB packets

    uint8_t prevMode; // to keep track of voltage mode change
    bool isFirstMode = true; // becomes false when teh initial volatge mode data is sent to teh UI

    const unsigned int ADCsamplingRate = 70000; // 70kHz ADC sampling rate
    std::vector<float> residueSamples;

    // opens port and sets up DCB
    HANDLE openAndConfigureSerial(const std::string& portName) {
        HANDLE hSerial = CreateFileA(
            portName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hSerial == INVALID_HANDLE_VALUE) {
            return INVALID_HANDLE_VALUE;
        }

        DCB dcb = {0};
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(hSerial, &dcb)) {
            dcb.BaudRate = this->baudRate;
            dcb.ByteSize = 8;
            dcb.StopBits = ONESTOPBIT;
            dcb.Parity   = NOPARITY;
            dcb.fDtrControl = DTR_CONTROL_ENABLE;
            dcb.fRtsControl = RTS_CONTROL_ENABLE;
            SetCommState(hSerial, &dcb);
        }

        return hSerial;
    }

    // configures timeouts for serial communication
    void setSerialTimeouts(HANDLE hSerial, DWORD readInterval, DWORD readTotalConst, DWORD writeTotalConst) {
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = readInterval;
        timeouts.ReadTotalTimeoutConstant = readTotalConst;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = writeTotalConst;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        SetCommTimeouts(hSerial, &timeouts);
    }

    // scans Windows for the hardcoded ESP32-C3 Hardware ID
    std::vector<std::string> getCandidatePorts() {
        std::vector<std::string> matchingPorts;
        HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT); 

        if (hDevInfo == INVALID_HANDLE_VALUE)  return matchingPorts;

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
            char hardwareId[512] = {0};
            char friendlyName[256] = {0};

            SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_HARDWAREID, NULL, (PBYTE)hardwareId, sizeof(hardwareId), NULL);
            std::string hwStr(hardwareId);

            if (hwStr.find("VID_303A&PID_1001") != std::string::npos || hwStr.find("vid_303a&pid_1001") != std::string::npos) {
                if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL)){
                    std::regex comRegex(R"((COM\d+))");
                    std::smatch match;
                    std::string friendlyStr(friendlyName);
                    if (std::regex_search(friendlyStr, match, comRegex)) matchingPorts.push_back("\\\\.\\" + match[1].str());
                }
            }
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
        return matchingPorts;
    }

    // returns true if the mentioned port is found
    bool isPortPresent(const std::string& portName) {
        std::string cleanTarget = portName;
        if (cleanTarget.rfind(R"(\\.\)", 0) == 0) cleanTarget = cleanTarget.substr(4);

        HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) return false;

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        std::regex comRegex(R"((COM\d+))", std::regex_constants::icase);

        bool portFound = false;
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
            char friendlyName[256] = {0};
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL)){
                std::smatch match;
                std::string friendlyStr(friendlyName);
                if (std::regex_search(friendlyStr, match, comRegex)) {
                    std::string detectedPort = match[1].str();
                    if (_stricmp(detectedPort.c_str(), cleanTarget.c_str()) == 0) {
                        portFound = true;
                        break;
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return portFound;
    }

public:
    double gains[3] = {0.0, 0.0, 0.0};
    double offsets[3] = {0.0, 0.0, 0.0};

    EspSerial(int baudRate, UIhandler& ui) : UI(ui) {
        this->baudRate = baudRate;
    }

    // scans for esp32
    void scanForEsp(const std::atomic<bool>& isRunningRef) {
        while (!espConnected && isRunningRef) {
            const std::vector<std::string> ports = getCandidatePorts();

            for (const auto& portName : ports) {
                HANDLE hSerial = openAndConfigureSerial(portName);
                if (hSerial == INVALID_HANDLE_VALUE) continue; 
                
                setSerialTimeouts(hSerial, 5, 100, 20);

                // Send ping byte (0x03)
                DWORD bytesWritten = 0;
                if (WriteFile(hSerial, &pingByte, 1, &bytesWritten, NULL)) {
                    uint8_t responseBuf[4] = {0};
                    DWORD bytesRead = 0;

                    if (ReadFile(hSerial, responseBuf, 4, &bytesRead, NULL) && bytesRead == 4) {
                        if (responseBuf[0] == 0x01 && responseBuf[1] == 0x06 && responseBuf[2] == 0x09 && responseBuf[3] == 0x07){
                            std::cout << "Detected ESP32-C3 at " << portName << "\n";
                            espPortName = portName;
                            espConnected = true;

                            CloseHandle(hSerial);
                            return; 
                        }
                    }
                }

                CloseHandle(hSerial);
            }
            Sleep(1000);
        }
    }

    // checks whether the current ESP32 port is still active or not
    bool isConnected() {
        if(isPortPresent(espPortName)){
            return true;
        } else {
            espConnected = false;
            return false;
        }
    }

    // Fetches the constants from the ESP32
    bool updateGainsAndOffsets() {
        if (!espConnected || espPortName.empty()) return false;

        HANDLE hSerial = openAndConfigureSerial(espPortName);
        if (hSerial == INVALID_HANDLE_VALUE) return false;
        
        // clear Windows RX/TX buffers
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        // perform a non-blocking dummy read to suck out any ghost bytes from the ESP32's USB FIFO
        setSerialTimeouts(hSerial, MAXDWORD, 0, 0);
        uint8_t trash[64];
        DWORD trashRead = 0;
        while (ReadFile(hSerial, trash, sizeof(trash), &trashRead, NULL) && trashRead > 0) {}

        // Set the actual timeouts for reading the 48 bytes
        setSerialTimeouts(hSerial, 50, 250, 20);

        // Send 0x02 byte to ESP32
        DWORD bytesWritten = 0;
        if (!WriteFile(hSerial, &calibConstReqByte, 1, &bytesWritten, NULL) || bytesWritten != 1) {
            std::cout << "Error requesting ESP32 for gain and offset constants";
            CloseHandle(hSerial);
            return false;
        }

        uint8_t rxBuffer[48] = {0};
        DWORD totalBytesRead = 0;
        DWORD bytesRead = 0;

        // Read exactly 48 bytes
        while (totalBytesRead < 48) {
            if (ReadFile(hSerial, rxBuffer + totalBytesRead, 48 - totalBytesRead, &bytesRead, NULL)) {
                if (bytesRead == 0) break; 
                totalBytesRead += bytesRead;
            } else {
                break; 
            }
        }

        CloseHandle(hSerial);

        if (totalBytesRead == 48) {
            memcpy(&gains[0], &rxBuffer[0], 8);
            memcpy(&gains[1], &rxBuffer[8], 8);
            memcpy(&gains[2], &rxBuffer[16], 8);
            memcpy(&offsets[0], &rxBuffer[24], 8);
            memcpy(&offsets[1], &rxBuffer[32], 8);
            memcpy(&offsets[2], &rxBuffer[40], 8);

            std::cout << "Gains: [" << gains[0] << ", " << gains[1] << ", " << gains[2] << "]\n";
            std::cout << "Offsets: [" << offsets[0] << ", " << offsets[1] << ", " << offsets[2] << "]\n";

            return true;
        }

        return false;
    }

    // requests esp32 to initiate continuous ADC data streaming
    bool requestADCstream() {
        if (hStream != INVALID_HANDLE_VALUE) {
            CloseHandle(hStream); // Close any existing stream
        }

        hStream = openAndConfigureSerial(espPortName);
        if (hStream == INVALID_HANDLE_VALUE) return false;

        // Clear Windows RX/TX buffers
        PurgeComm(hStream, PURGE_RXCLEAR | PURGE_TXCLEAR);

        // Dummy read to suck out any ghost bytes
        setSerialTimeouts(hStream, MAXDWORD, 0, 0);
        uint8_t trash[64];
        DWORD trashRead = 0;
        while (ReadFile(hStream, trash, sizeof(trash), &trashRead, NULL) && trashRead > 0) {}

        // Set non-blocking timeouts for streaming
        // MAXDWORD interval + 0 TotalConst means ReadFile returns immediately with whatever is in the buffer (no freezing)
        setSerialTimeouts(hStream, MAXDWORD, 0, 50); 

        // Send 0x01 byte to ESP32
        DWORD bytesWritten = 0;
        if(!WriteFile(hStream, &adcStreamReqByte, 1, &bytesWritten, NULL) || bytesWritten != 1){
            std::cout << "Error requesting ESP32 for ADC stream\n";
            CloseHandle(hStream);
            hStream = INVALID_HANDLE_VALUE;
            return false;
        }

        streamRXBuffer.clear(); // clear software buffer for new stream
        return true;
    }

    // Reads and downsamples(min-max downsampling) available data from ESP32. Intended to be called rapidly in a while loop
    void readADCChunkToSharedBuffer(bool& modeChanged, uint8_t& newMode) {
        if (hStream == INVALID_HANDLE_VALUE) return;

        uint8_t tempBuf[4096];
        DWORD bytesRead = 0;
        
        // Read whatever data arrived since the last loop iteration
        if (ReadFile(hStream, tempBuf, sizeof(tempBuf), &bytesRead, NULL) && bytesRead > 0) {
            streamRXBuffer.insert(streamRXBuffer.end(), tempBuf, tempBuf + bytesRead);
        }

        // Process all complete packets in our buffer
        while (streamRXBuffer.size() >= 13) {
            // Find Sync Header (0xAA 0xBB)
            size_t syncIdx = 0;
            bool syncFound = false;
            for (size_t i = 0; i < streamRXBuffer.size() - 1; ++i) {
                if (streamRXBuffer[i] == 0xAA && streamRXBuffer[i+1] == 0xBB) {
                    syncIdx = i;
                    syncFound = true;
                    break;
                }
            }

            if (!syncFound) {
                // No sync found. Erase garbage, but keep the last byte in case it's half of a sync header (0xAA)
                uint8_t lastByte = streamRXBuffer.back();
                streamRXBuffer.clear();
                if (lastByte == 0xAA) streamRXBuffer.push_back(0xAA);
                break;
            }

            // Remove any garbage bytes before the sync header
            if (syncIdx > 0) {
                streamRXBuffer.erase(streamRXBuffer.begin(), streamRXBuffer.begin() + syncIdx);
            }

            // Ensure we have at least the full 13-byte header structure
            // (2 Sync + 8 Timestamp + 1 Mode + 2 SampleCount) = 13
            if (streamRXBuffer.size() < 13) break; 

            // Extract sample count to calculate total packet size
            int16_t sampleCount = 0;
            memcpy(&sampleCount, &streamRXBuffer[11], 2);

            size_t totalPacketSize = 13 + (sampleCount * 2); // 13 header bytes + (N * 2 payload bytes)

            // If the full packet hasn't arrived over USB yet, break and wait for next loop iteration
            if (streamRXBuffer.size() < totalPacketSize) {
                break; 
            }

            // Full packet detected, parsing logic:
            uint64_t timestamp = 0;
            memcpy(&timestamp, &streamRXBuffer[2], 8);
            
            uint8_t mode = streamRXBuffer[10];
            if (mode > 2) {
                std::cout << "Warning: Corrupt mode byte (" << (int)mode << "). Skipping packet.\n";
                streamRXBuffer.erase(streamRXBuffer.begin(), streamRXBuffer.begin() + totalPacketSize);
                continue; // skip to the next packet
            }

            // detect voltage mode changes
            if(prevMode != mode || isFirstMode){
                newMode = mode;
                modeChanged = true;
                isFirstMode = false;
            } else {
                modeChanged = false;
            }

            // fetch correct gains and offsets
            double gain = gains[mode];
            double offset = offsets[mode];

            std::vector<float> voltArray; // stores the actual input voltages 
            voltArray.reserve(sampleCount);

            for (int i = 0; i < sampleCount; ++i) {
                int16_t mV = 0;
                memcpy(&mV, &streamRXBuffer[13 + (i * 2)], 2);
                voltArray.push_back(static_cast<float>((mV - offset) / (gain * 1000.0))); // Vin = (mVout - offset)/ (gain * 1000)
            }

            const int numConsecutiveSamples = (2 * ADCsamplingRate * UI.graphTimeFrame) / (UI.maxPointsPerGraphFrame * 1000); 

            // combine any leftover samples from the last packet with the new voltArray
            std::vector<float> processBuffer = residueSamples;
            processBuffer.insert(processBuffer.end(), voltArray.begin(), voltArray.end());

            std::vector<float> voltArrayDownsampled;

            // only downsample if the window is at least 2 samples (prevents divide-by-zero if UI parameters cause numConsecutiveSamples to evaluate to < 2)
            if (numConsecutiveSamples >= 2) {
                int numFullWindows = processBuffer.size() / numConsecutiveSamples; // number full windows we can process right now
                voltArrayDownsampled.reserve(numFullWindows * 2);

                for (int w = 0; w < numFullWindows; ++w) {
                    int startIndex = w * numConsecutiveSamples;
                    
                    float minVal = processBuffer[startIndex];
                    float maxVal = processBuffer[startIndex];
                    int minIdx = startIndex;
                    int maxIdx = startIndex;

                    // Find min and max within the current window
                    for (int i = 1; i < numConsecutiveSamples; ++i) {
                        float val = processBuffer[startIndex + i];
                        if (val < minVal) {
                            minVal = val;
                            minIdx = startIndex + i;
                        }
                        if (val > maxVal) {
                            maxVal = val;
                            maxIdx = startIndex + i;
                        }
                    }

                    // push chronologically to prevent visual zig-zag/backward artifacts in the UI graph
                    if (minIdx <= maxIdx) {
                        voltArrayDownsampled.push_back(minVal);
                        voltArrayDownsampled.push_back(maxVal);
                    } else {
                        voltArrayDownsampled.push_back(maxVal);
                        voltArrayDownsampled.push_back(minVal);
                    }
                }

                // save the unprocessed remainder for the next iteration/packet
                int processedCount = numFullWindows * numConsecutiveSamples;
                residueSamples.assign(processBuffer.begin() + processedCount, processBuffer.end());

                // share the downsampled data to the UI
                if (!voltArrayDownsampled.empty()) UI.shareNewFrame(voltArrayDownsampled.data(), voltArrayDownsampled.size()); 
            } else {
                // If the window size is too small, skip downsampling to prevent data distortion
                UI.shareNewFrame(voltArray.data(), voltArray.size());
                residueSamples.clear(); // Reset residue since we bypassed it
            }

            // Erase this parsed packet from the stream buffer so we can parse the next one
            streamRXBuffer.erase(streamRXBuffer.begin(), streamRXBuffer.begin() + totalPacketSize);

            prevMode = mode;
        }
    }

    // stop ADC stream handlers
    void stopADCstream() {
        if (hStream != INVALID_HANDLE_VALUE) {            
            CloseHandle(hStream);
            hStream = INVALID_HANDLE_VALUE;
            streamRXBuffer.clear();
        }
    }
};