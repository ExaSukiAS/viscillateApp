#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regex>
#include <cstdint>
#include "UIhandler.h"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <cstdlib>

#pragma comment(lib, "setupapi.lib")

class RPSerial {
private:
    int baudRate;
    UIhandler& UI;

    // RP2040 commands
    uint8_t oscilloscopeStreamByte = 0x01;
    uint8_t voltmeterStreamByte = 0x02;
    uint8_t streamStopByte = 0x03;
    uint8_t calibConstReqByte = 0x04;
    uint8_t pingByte = 0x05;
    uint8_t timeFrameSetByte = 0x06;

    // RP2040 states and ports
    bool rpConnected = false;
    std::string rpPortName;

    HANDLE hStream = INVALID_HANDLE_VALUE; // Persistent handle for streaming
    std::vector<uint8_t> streamRXBuffer;   // Buffer to stitch fragmented USB packets

    uint8_t prevVoltageMode; // to keep track of voltage mode change
    bool isFirstMode = true; // becomes false when the initial voltage mode data is sent to the UI

    const unsigned int ADCsamplingRate = 500000; // 500ksps ADC sampling rate
    std::vector<float> residueSamples;

    // Builds and writes a strict 9-byte little-endian packet sent to the rp2040: [cmd, b0, b1, b2, b3, b4, b5, b6, b7]
    bool sendCommand(HANDLE hSerial, uint8_t cmd, double payload = 0) {
        if (hSerial == INVALID_HANDLE_VALUE) return false;

        uint8_t packet[9];
        packet[0] = cmd;
        std::memcpy(&packet[1], &payload, sizeof(double));

        DWORD totalWritten = 0;
        while (totalWritten < sizeof(packet)) {
            DWORD bytesWritten = 0;
            if (!WriteFile(hSerial, packet + totalWritten, sizeof(packet) - totalWritten, &bytesWritten, NULL) || bytesWritten == 0) {
                return false;
            }
            totalWritten += bytesWritten;
        }
        return true;
    }

    // Reads an exact number of bytes or returns false on timeout/error
    bool readExact(HANDLE hSerial, uint8_t* dst, DWORD expectedBytes, DWORD overallTimeoutMs = 300) {
        DWORD totalRead = 0;
        ULONGLONG startTick = GetTickCount64();

        while (totalRead < expectedBytes) {
            if (GetTickCount64() - startTick >= overallTimeoutMs) {
                return false;
            }

            DWORD bytesRead = 0;
            if (!ReadFile(hSerial, dst + totalRead, expectedBytes - totalRead, &bytesRead, NULL)) {
                return false;
            }
            if (bytesRead == 0) {
                return false; // COMMTIMEOUTS expired
            }
            totalRead += bytesRead;
        }
        return true;
    }

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
            std::cout << "[RPSerial] CreateFileA failed on " << portName
                      << " (GetLastError=" << GetLastError() << ")\n";
            return INVALID_HANDLE_VALUE;
        }

        DCB dcb = {0};
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(hSerial, &dcb)) {
            dcb.BaudRate = this->baudRate; // Never use 1200
            dcb.ByteSize = 8;
            dcb.StopBits = ONESTOPBIT;
            dcb.Parity   = NOPARITY;
            dcb.fBinary  = TRUE;
            dcb.fOutxCtsFlow = FALSE;
            dcb.fOutxDsrFlow = FALSE;
            dcb.fOutX    = FALSE;
            dcb.fInX     = FALSE;
            dcb.fDtrControl = DTR_CONTROL_ENABLE;
            dcb.fRtsControl = RTS_CONTROL_ENABLE;

            if (!SetCommState(hSerial, &dcb)) {
                std::cout << "[RPSerial] SetCommState failed (GetLastError=" << GetLastError() << ")\n";
            }
        } else {
            std::cout << "[RPSerial] GetCommState failed (GetLastError=" << GetLastError() << ")\n";
        }

        // Explicitly force usbser.sys to emit USB SET_CONTROL_LINE_STATE (DTR=1, RTS=1)
        EscapeCommFunction(hSerial, SETDTR);
        EscapeCommFunction(hSerial, SETRTS);

        return hSerial;
    }

    // scans Windows for the hardcoded RP2040 Hardware ID
    std::vector<std::string> getCandidatePorts() {
        std::vector<std::string> matchingPorts;
        HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) return matchingPorts;

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        std::regex comRegex(R"((COM\d+))", std::regex_constants::icase);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
            char hardwareId[512] = {0};
            if (!SetupDiGetDeviceRegistryPropertyA(
                    hDevInfo, &devInfoData, SPDRP_HARDWAREID, NULL,
                    (PBYTE)hardwareId, sizeof(hardwareId) - 2, NULL)) {
                continue;
            }

            // SPDRP_HARDWAREID is REG_MULTI_SZ (null-separated strings, ending in \0\0)
            bool vidPidMatched = false;
            for (const char* p = hardwareId; *p != '\0'; p += std::strlen(p) + 1) {
                std::string entry(p);
                for (char& c : entry) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (entry.find("VID_2E8A&PID_1697") != std::string::npos) {
                    vidPidMatched = true;
                    break;
                }
            }

            if (vidPidMatched) {
                char friendlyName[256] = {0};
                if (SetupDiGetDeviceRegistryPropertyA(
                        hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL,
                        (PBYTE)friendlyName, sizeof(friendlyName) - 1, NULL)) {
                    std::smatch match;
                    std::string friendlyStr(friendlyName);
                    if (std::regex_search(friendlyStr, match, comRegex)) {
                        matchingPorts.push_back("\\\\.\\" + match[1].str());
                    }
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

    // Clears Windows driver queues and drains any leftover bytes already in transit
    void drainRxBuffer(HANDLE hSerial) {
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        setSerialTimeouts(hSerial, MAXDWORD, 0, 0);
        uint8_t trash[256];
        DWORD trashRead = 0;
        while (ReadFile(hSerial, trash, sizeof(trash), &trashRead, NULL) && trashRead > 0) {}
    }

public:
    double gains[2] = {0.0, 0.0};
    double offsets[2] = {0.0, 0.0};

    RPSerial(int baudRate, UIhandler& ui) : UI(ui) {
        this->baudRate = baudRate;

        // Give UI access only to setTimeFrame()
        UI.setTimeFrameCallback([this](double newTimeFrame) {
            return this->setTimeFrame(newTimeFrame);
        });
    }

     ~RPSerial() {
        UI.setTimeFrameCallback(nullptr); // Safe cleanup if serial is destroyed before UI
    }

    // scans for rp2040
    void scanForRp(const std::atomic<bool>& isRunningRef) {
        while (!rpConnected && isRunningRef.load()) {
            const std::vector<std::string> ports = getCandidatePorts();
            if (ports.empty()) {
                std::cout << "[RPSerial] No matching VID_2E8A&PID_1697 ports found.\n";
            }

            for (const auto& portName : ports) {
                if (!isRunningRef.load()) return;

                HANDLE hSerial = openAndConfigureSerial(portName);
                if (hSerial == INVALID_HANDLE_VALUE) continue;

                // Give idle/suspended USB port + DTR line time to settle
                Sleep(50);

                // Stop any leftover stream if the previous PC session crashed
                setSerialTimeouts(hSerial, 50, 50, 50);
                sendCommand(hSerial, streamStopByte, 0);
                Sleep(25); // Let RP2040 process stop and clear any USB_PACKET_TIMEOUT_US

                drainRxBuffer(hSerial);

                // Send 5-byte Ping command (0x05, 0, 0, 0, 0) and read 4-byte Pong
                setSerialTimeouts(hSerial, 50, 200, 50);
                if (!sendCommand(hSerial, pingByte, 0)) {
                    std::cout << "[RPSerial] Ping WriteFile failed on " << portName
                              << " (GetLastError=" << GetLastError() << ")\n";
                } else {
                    uint8_t responseBuf[4] = {0};
                    if (readExact(hSerial, responseBuf, 4, 300)) {
                        if (responseBuf[0] == 0x01 && responseBuf[1] == 0x06 &&
                            responseBuf[2] == 0x09 && responseBuf[3] == 0x07) {
                            std::cout << "Detected RP2040 at " << portName << "\n";
                            rpPortName = portName;
                            rpConnected = true;
                            CloseHandle(hSerial);
                            return;
                        } else {
                            std::cout << "[RPSerial] Unexpected pong bytes on " << portName << ": "
                                      << std::hex << (int)responseBuf[0] << " " << (int)responseBuf[1] << " "
                                      << (int)responseBuf[2] << " " << (int)responseBuf[3] << std::dec << "\n";
                        }
                    } else {
                        std::cout << "[RPSerial] Ping timed out waiting for 4-byte pong on " << portName << "\n";
                    }
                }

                CloseHandle(hSerial);
            }

            for (int i = 0; i < 10 && isRunningRef.load(); ++i) {
                Sleep(100);
            }
        }
    }

    // checks whether the current RP2040 port is still active or not
    bool isConnected() {
        if (!rpConnected || rpPortName.empty()) {
            return false;
        }
        if (isPortPresent(rpPortName)) {
            return true;
        }
        rpConnected = false;
        rpPortName.clear();
        return false;
    }

    // Fetches the constants from the RP2040
    bool updateGainsAndOffsets() {
        if (!rpConnected || rpPortName.empty()) return false;

        HANDLE hSerial = openAndConfigureSerial(rpPortName);
        if (hSerial == INVALID_HANDLE_VALUE) return false;

        drainRxBuffer(hSerial);
        setSerialTimeouts(hSerial, 50, 250, 50);

        if (!sendCommand(hSerial, calibConstReqByte, 0)) {
            std::cout << "Error requesting RP2040 for gain and offset constants\n";
            CloseHandle(hSerial);
            return false;
        }

        uint8_t rxBuffer[32] = {0};
        bool ok = readExact(hSerial, rxBuffer, sizeof(rxBuffer), 400);
        CloseHandle(hSerial);

        if (ok) {
            std::memcpy(&gains[0],   &rxBuffer[0],  8);
            std::memcpy(&gains[1],   &rxBuffer[8],  8);
            std::memcpy(&offsets[0], &rxBuffer[16], 8);
            std::memcpy(&offsets[1], &rxBuffer[24], 8);

            std::cout << "Gains: [" << gains[0] << ", " << gains[1] << "]\n";
            std::cout << "Offsets: [" << offsets[0] << ", " << offsets[1] << "]\n";
            return true;
        }

        return false;
    }


    // requests rp2040 to initiate continuous ADC data streaming
    bool requestADCstream(bool voltmeterMode = false) {
        if (!rpConnected || rpPortName.empty()) return false;

        if (hStream != INVALID_HANDLE_VALUE) {
            CloseHandle(hStream);
            hStream = INVALID_HANDLE_VALUE;
        }

        hStream = openAndConfigureSerial(rpPortName);
        if (hStream == INVALID_HANDLE_VALUE) return false;

        drainRxBuffer(hStream);
        setSerialTimeouts(hStream, MAXDWORD, 0, 50);

        uint8_t cmd = voltmeterMode ? voltmeterStreamByte : oscilloscopeStreamByte;
        if (!sendCommand(hStream, cmd, 0)) {
            std::cout << "Error requesting RP2040 for ADC stream\n";
            CloseHandle(hStream);
            hStream = INVALID_HANDLE_VALUE;
            return false;
        }

        streamRXBuffer.clear();
        return true;
    }

    // informs the RP2040 about the desired time frame for the oscilloscope. 
    // The RP2040 will then adjust its min/max decimation factor accordingly.
    bool setTimeFrame(double timeFrame) {
        HANDLE target = hStream;
        bool openedTemp = false;

        if (target == INVALID_HANDLE_VALUE) {
            if (!rpConnected || rpPortName.empty()) return false;
            target = openAndConfigureSerial(rpPortName);
            if (target == INVALID_HANDLE_VALUE) return false;
            setSerialTimeouts(target, 50, 100, 50);
            openedTemp = true;
        }

        timeFrame = timeFrame * 1000.0; // Convert ms to microseconds

        bool ok = sendCommand(target, timeFrameSetByte, timeFrame);
        if (openedTemp) CloseHandle(target);
        return ok;
    }

    // stops the ADC stream and closes the serial handle
    void stopADCstream() {
        if (hStream != INVALID_HANDLE_VALUE) {
            sendCommand(hStream, streamStopByte, 0);
            CloseHandle(hStream);
            hStream = INVALID_HANDLE_VALUE;
            streamRXBuffer.clear();
        }
    }


    const size_t USB_HEADER_WORDS = 3; // [0]=0x1388, [1]=voltageMode, [2]=number of data words

    // Reads available data from RP2040. Intended to be called rapidly in a while loop
    void readADCChunkToSharedBuffer(bool& modeChanged, uint8_t& newMode) {
        if (hStream == INVALID_HANDLE_VALUE) return;

        uint8_t tempBuf[8192]; 
        DWORD bytesRead = 0;
        
        // Read whatever data arrived since the last loop iteration
        if (ReadFile(hStream, tempBuf, sizeof(tempBuf), &bytesRead, NULL) && bytesRead > 0) {
            streamRXBuffer.insert(streamRXBuffer.end(), tempBuf, tempBuf + bytesRead);
        }

        while (streamRXBuffer.size() > USB_HEADER_WORDS * sizeof(uint16_t)) {
            // Check for little-endian 5000 (0x1388 -> 0x88, 0x13)
            if (streamRXBuffer[0] != 0x88 || streamRXBuffer[1] != 0x13) {
                // Not aligned to a frame boundary; drop 1 byte and keep scanning
                streamRXBuffer.erase(streamRXBuffer.begin());
                continue;
            }

            const size_t USB_DATA_WORDS = streamRXBuffer[4] | (streamRXBuffer[5] << 8);
            const size_t USB_FRAME_WORDS = USB_HEADER_WORDS + USB_DATA_WORDS;
            const size_t USB_FRAME_BYTES = USB_FRAME_WORDS * sizeof(uint16_t);

            // Copy safely into aligned uint16_t vector
            std::vector<uint16_t> packet(USB_FRAME_WORDS);
            std::memcpy(packet.data(), streamRXBuffer.data(), USB_FRAME_BYTES);

            uint16_t voltageMode = packet[1];

            // handle corrupted data 
            if (voltageMode > 1) {
                streamRXBuffer.erase(streamRXBuffer.begin());
                continue;
            }

            // detect voltage mode changes
            if(prevVoltageMode != voltageMode || isFirstMode){
                newMode = voltageMode;
                modeChanged = true;
                isFirstMode = false;
            } else {
                modeChanged = false;
            }

            // fetch correct gains and offsets
            double gain = gains[voltageMode]; // divide by 1000 so that we get answers in volts
            double offset = offsets[voltageMode];

            std::vector<float> voltArray; // stores the actual input voltages 
            voltArray.reserve(USB_DATA_WORDS); // number of voltage points (two consicutive data are min/max pairs)

            for (int i = 3; i < USB_FRAME_WORDS; i++) {
                voltArray.push_back(static_cast<float>((packet[i] * gain + offset)));
            }

            printf("Received %zu voltage points. First 2 points: %.2f, %.2f\n", voltArray.size(), voltArray[0], voltArray[1]);

            UI.shareNewFrame(voltArray.data(), voltArray.size()); // Send data to UI

            /*
            std::ostringstream jsMessage; // holds signal analytics message in a comma-saperated format
            jsMessage << "signalAnalytics:"
                        << currentVoltage << ","
                        << Vmax << ","
                        << Vmin << ","
                        << Vpp << ","
                        << Vrms << ","
                        << freq << ","
                        << periodMS << ","
                        << dutyCycle;

            UI.sendMessageToJS(jsMessage.str());
            */

            streamRXBuffer.erase(streamRXBuffer.begin(), streamRXBuffer.begin() + USB_FRAME_BYTES); // Remove consumed frame from RX buffer
            prevVoltageMode = voltageMode;
        }

    }
};