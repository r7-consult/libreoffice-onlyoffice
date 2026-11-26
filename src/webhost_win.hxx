#pragma once

#ifdef _WIN32

#include "webhost.hxx"

#include <windows.h>
#include <deque>
#include <string>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;
struct ICoreWebView2WebMessageReceivedEventArgs;

namespace pluginhost
{
// WebView2 (Evergreen Runtime) hosted in a Win32 child window.
// All calls must come from the LibreOffice main thread: WebView2 completion
// callbacks run on the thread that created the environment.
class WebHostWin : public WebHost
{
public:
    WebHostWin(HWND parent, std::string userDataDir);
    ~WebHostWin() override;

    bool navigate(const std::string& url) override;
    void postMessage(const std::string& jsonUtf8) override;
    void setBounds(int x, int y, int width, int height) override;
    void setVisible(bool visible) override;
    void reparent(void* newParentNativeHandle) override;
    void setMessageHandler(MessageHandler handler) override;
    bool isReady() const override { return m_web != nullptr; }

private:
    void onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env);
    void onControllerCreated(HRESULT hr, ICoreWebView2Controller* controller);
    void onWebMessage(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args);
    void flushPending();

    HWND m_parent;
    std::string m_userDataDir;
    MessageHandler m_onMessage;
    ICoreWebView2Environment* m_env = nullptr;
    ICoreWebView2Controller* m_controller = nullptr;
    ICoreWebView2* m_web = nullptr;
    std::deque<std::string> m_pendingPosts;
    std::string m_pendingUrl;
    bool m_webMessageHooked = false;
    unsigned long m_messageCount = 0;
    long long m_webMessageToken = 0;
};
}

#endif // _WIN32
