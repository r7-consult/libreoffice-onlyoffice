#pragma once

#ifdef _WIN32

#include "webhost.hxx"

#include <windows.h>
#include <deque>
#include <memory>
#include <string>
#include <vector>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;
struct ICoreWebView2WebMessageReceivedEventArgs;
struct ICoreWebView2ProcessFailedEventArgs;

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
    void setFailureHandler(FailureHandler handler) override;
    void addDocumentScript(const std::string& scriptPath) override;
    bool isReady() const override { return m_web != nullptr; }

private:
    void onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env);
    void onControllerCreated(HRESULT hr, ICoreWebView2Controller* controller);
    void onWebMessage(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args);
    void onProcessFailed(ICoreWebView2ProcessFailedEventArgs* args);
    void registerDocumentScript(const std::string& source);
    void flushPending();

    std::shared_ptr<bool> m_alive; // callbacks check this instead of trusting a raw this
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
    FailureHandler m_onFailure;
    std::vector<std::string> m_documentScripts; // sources, registered once the engine is ready
    long long m_processFailedToken = 0;
    long long m_webMessageToken = 0;
};
}

#endif // _WIN32
