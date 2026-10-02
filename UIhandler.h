#pragma once

#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>
#include <string>
#include <chrono>
#include <dwmapi.h>
#include <vector>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")

using namespace Microsoft::WRL;

class UIhandler {
private:
    HWND hWnd = nullptr;
    wil::com_ptr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    wil::unique_couninitialize_call comInit;
    wil::com_ptr<ICoreWebView2Environment12> env12;
    wil::com_ptr<ICoreWebView2_17> webview17;

    BYTE* JSsharedBuffer = nullptr; // shared memory buffer between C++ and JS
    wil::com_ptr<ICoreWebView2SharedBuffer> sharedBuffer;

    size_t currentFloatOffset = 0; 
    const size_t maxSamplesPerSharedBuffer = 280000; // max samples the shared memory buffer can hold
    std::chrono::steady_clock::time_point lastSyncTime; // last time when JS was notified about buffer through IPC

    // windows message queues for sending IPC messages to JS from outside main thread
    #define WM_SCOPE_SYNC (WM_USER + 1)
    #define WM_SEND_JS_MSG (WM_USER + 2)

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        UIhandler* pThis = nullptr;
        if (message == WM_NCCREATE) {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            pThis = reinterpret_cast<UIhandler*>(pCreate->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->hWnd = hWnd;
        } else {
            pThis = reinterpret_cast<UIhandler*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        if (pThis) return pThis->WindowProc(hWnd, message, wParam, lParam);
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    LRESULT WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_SIZE:
                if (controller != nullptr) {
                    RECT bounds;
                    GetClientRect(hWnd, &bounds);
                    controller->put_Bounds(bounds);
                }
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                break;
            case WM_SCOPE_SYNC:
                if (webview) {
                    // Extract offset and count passed via wParam / lParam
                    size_t offset = static_cast<size_t>(wParam);
                    size_t count = static_cast<size_t>(lParam);
                    
                    std::wstring msg = L"sync:" + std::to_wstring(offset) + L":" + std::to_wstring(count); // send ad colon-saperated string
                    webview->PostWebMessageAsString(msg.c_str());
                }
                break;
            case WM_SEND_JS_MSG:
                if (webview && lParam) {
                    // Cast the pointer back to a wstring
                    std::wstring* msg = reinterpret_cast<std::wstring*>(lParam);
                    
                    // Send to JavaScript
                    webview->PostWebMessageAsString(msg->c_str());
                    
                    // Free the dynamically allocated memory
                    delete msg; 
                }
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }

    void setupSharedBuffer() {
        if (!env12 || !webview17 || JSsharedBuffer) return; // Prevent double initialization

        UINT64 bufferSize = maxSamplesPerSharedBuffer * sizeof(float);
        if (SUCCEEDED(env12->CreateSharedBuffer(bufferSize, &sharedBuffer))) {
            sharedBuffer->get_Buffer(&JSsharedBuffer);
            
            webview17->PostSharedBufferToScript(
                sharedBuffer.get(),
                COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY,
                L"{}"
            );
        }
    }

    void InitializeWebView() {
        CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    environment = env;
                    env->QueryInterface(IID_PPV_ARGS(&env12));
                    
                    env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                            controller = ctrl;
                            controller->get_CoreWebView2(&webview);
                            webview->QueryInterface(IID_PPV_ARGS(&webview17));

                            RECT bounds;
                            GetClientRect(hWnd, &bounds);
                            controller->put_Bounds(bounds);

                            // Listen for messages from JavaScript
                            webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        wil::unique_cotaskmem_string message;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                                            handleJSIPCmsg(std::wstring(message.get()));
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            ComPtr<ICoreWebView2_3> webview3;
                            if (SUCCEEDED(webview.As(&webview3))) {
                                wchar_t exePath[MAX_PATH];
                                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                                std::wstring path(exePath);
                                std::wstring UIFolder = path.substr(0, path.find_last_of(L"\\/")) + L"\\UI";

                                webview3->SetVirtualHostNameToFolderMapping(
                                    L"viscillate.local",
                                    UIFolder.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                );

                                webview->Navigate(L"http://viscillate.local/index.html");
                            }
                            return S_OK;
                        }).Get());
                    return S_OK;
                }).Get());
    }

    // splits a wide string into parts based on a delimiter
    std::vector<std::wstring> splitWstring(const std::wstring& str, wchar_t delimiter) {
        std::vector<std::wstring> tokens;
        size_t start = 0;
        size_t end = str.find(delimiter);

        while (end != std::wstring::npos) {
            tokens.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(delimiter, start);
        }

        // Add the remaining token
        tokens.push_back(str.substr(start));

        return tokens;
    }

    // handles incoming IPC messages from JS
    void handleJSIPCmsg(std::wstring msg){
        std::vector<std::wstring> msgParts = splitWstring(msg, L':');
        if (msgParts[0] == L"ready") {
            setupSharedBuffer(); // JS is ready, now safe to send shared buffer
            isJSinitialized = true;
        } else if (msgParts[0] == L"graphSettings") {
            // Handle graph settings update
            graphTimeFrame = std::stoi(msgParts[1]);
            graphVoltageLimit = std::stof(msgParts[2]);
            graphVoltageOffset = std::stof(msgParts[3]);

            std::wcout << L"Graph Settings Updated - Time Frame: " << graphTimeFrame 
                       << L", Voltage Limit: " << graphVoltageLimit 
                       << L", Voltage Offset: " << graphVoltageOffset << std::endl;
        }
    }

public:
    std::atomic<bool> isJSinitialized = false; // flag to indicate is javascript is initialized

    // UI graph settings
    int graphTimeFrame = 100; // in ms
    float graphVoltageLimit = 50.0f; // in volts
    float graphVoltageOffset = 0.0f; // in volts

    const int maxPointsPerGraphFrame = 1000; // max number of points on a single frame of the graph
    const int maxFrameRate = 50; // max number of frames per second for the graph

    UIhandler() {}

    // launches the UI in webview2
    void launch() {
        comInit = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);

        HINSTANCE hInstance = GetModuleHandle(nullptr);
        const wchar_t CLASS_NAME[] = L"ViscillateAppWindow";

        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = UIhandler::StaticWndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wcex);

        hWnd = CreateWindowExW(
            0, CLASS_NAME, 
            L"Viscillate",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
            nullptr, nullptr, hInstance, this 
        );

        if (!hWnd) return;

        // use dark mode title bar
        BOOL useDarkMode = TRUE;
        DwmSetWindowAttribute(hWnd, 20, &useDarkMode, sizeof(useDarkMode));

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        InitializeWebView();

        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // shares new data with JS using the shared buffer
    // changes in buffer are done immedietly, but JS is notified about the change in a fixed interval to avoid overwhelming IPC calls
    void shareNewFrame(const float* samples, size_t count) {
        if (!JSsharedBuffer || !webview) return;

        size_t writeOffset = currentFloatOffset;

        if (currentFloatOffset + count > maxSamplesPerSharedBuffer) {
            currentFloatOffset = 0; 
            writeOffset = 0;
        }

        size_t byteOffset = currentFloatOffset * sizeof(float);
        memcpy(JSsharedBuffer + byteOffset, samples, count * sizeof(float));
        currentFloatOffset += count;

        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSyncTime).count();

        if (elapsedMs >= 17) { // roughly 60Hz
            // Send starting offset in wParam and count in lParam
            PostMessageW(hWnd, WM_SCOPE_SYNC, static_cast<WPARAM>(writeOffset), static_cast<LPARAM>(count)); 
            lastSyncTime = now;
        }
    }

    // Sends a wide string to JavaScript
    void sendMessageToJS(const std::wstring& message) {
        if (!hWnd) return;
        
        std::wstring* msgPtr = new std::wstring(message); // Allocate string on the heap so it survives the cross-thread jump
        
        PostMessageW(hWnd, WM_SEND_JS_MSG, 0, reinterpret_cast<LPARAM>(msgPtr)); // Send it to the WindowProc queue to be processed on the UI thread
    }

    // Sends a standard UTF-8 string to JavaScript (auto-converts to wide string)
    void sendMessageToJS(const std::string& message) {
        if (message.empty() || !hWnd) return;
        
        // Convert std::string to std::wstring
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &message[0], (int)message.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &message[0], (int)message.size(), &wstr[0], size_needed);
        
        sendMessageToJS(wstr); // Pass to the wide-string version
    }
};