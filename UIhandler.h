#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>
#include <string>
#include <chrono>

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

    BYTE* rawBuffer = nullptr;
    wil::com_ptr<ICoreWebView2SharedBuffer> sharedBuffer;

    size_t currentFloatOffset = 0; 
    const size_t maxSamplesPerSharedBuffer = 140000;
    std::chrono::steady_clock::time_point lastSyncTime;

    #define WM_SCOPE_SYNC (WM_USER + 1)

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
                    
                    std::wstring msg = L"sync:" + std::to_wstring(offset) + L":" + std::to_wstring(count);
                    webview->PostWebMessageAsString(msg.c_str());
                }
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }

    void setupSharedBuffer() {
        if (!env12 || !webview17 || rawBuffer) return; // Prevent double initialization

        UINT64 bufferSize = maxSamplesPerSharedBuffer * sizeof(float);
        if (SUCCEEDED(env12->CreateSharedBuffer(bufferSize, &sharedBuffer))) {
            sharedBuffer->get_Buffer(&rawBuffer);
            
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
                                            if (std::wstring(message.get()) == L"ready") {
                                                // JS is ready, now safe to send shared buffer
                                                setupSharedBuffer();
                                            }
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

public:
    UIhandler() {}

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
            0, CLASS_NAME, L"Viscillate App", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
            nullptr, nullptr, hInstance, this 
        );

        if (!hWnd) return;

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        InitializeWebView();

        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void shareNewFrame(const float* samples, size_t count) {
        if (!rawBuffer || !webview) return;

        size_t writeOffset = currentFloatOffset;

        if (currentFloatOffset + count > maxSamplesPerSharedBuffer) {
            currentFloatOffset = 0; 
            writeOffset = 0;
        }

        size_t byteOffset = currentFloatOffset * sizeof(float);
        memcpy(rawBuffer + byteOffset, samples, count * sizeof(float));
        currentFloatOffset += count;

        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSyncTime).count();

        if (elapsedMs >= 10) { 
            // Send starting offset in wParam and count in lParam
            PostMessageW(hWnd, WM_SCOPE_SYNC, static_cast<WPARAM>(writeOffset), static_cast<LPARAM>(count)); 
            lastSyncTime = now;
        }
    }
};