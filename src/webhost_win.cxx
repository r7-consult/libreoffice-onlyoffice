#ifdef _WIN32

#include "webhost_win.hxx"

#include "log.hxx"

#include <WebView2.h>
#include <objbase.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <utility>

// Loader entry point from WebView2LoaderStatic.lib (no runtime DLL load).
extern "C" HRESULT WINAPI CreateCoreWebView2EnvironmentWithOptions(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

namespace pluginhost
{
namespace
{
// Minimal COM callback: forwards Invoke(...) to a std::function.
// Args must match the Invoke signature of the WebView2 handler interface exactly.
template <class Iface, class... Args>
class Callback final : public Iface
{
public:
    explicit Callback(std::function<HRESULT(Args...)> fn)
        : m_fn(std::move(fn))
    {
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return static_cast<ULONG>(r);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(Iface))
        {
            *ppv = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override
    {
        return m_fn(args...);
    }

private:
    ~Callback() = default;
    LONG m_ref = 1;
    std::function<HRESULT(Args...)> m_fn;
};

template <class Iface, class... Args>
Iface* makeCallback(std::function<HRESULT(Args...)> fn)
{
    return new Callback<Iface, Args...>(std::move(fn));
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
}

WebHostWin::WebHostWin(HWND parent, std::string userDataDir)
    : m_parent(parent)
    , m_userDataDir(std::move(userDataDir))
{
    const std::wstring dataDir = toWide(m_userDataDir);
    auto onEnv = makeCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            onEnvironmentCreated(hr, env);
            return S_OK;
        });
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr, onEnv);
    onEnv->Release();
    logEvent("webhost", "env_requested", "hr=" + std::to_string(static_cast<long>(hr)));
}

WebHostWin::~WebHostWin()
{
    if (m_web && m_webMessageHooked)
        m_web->remove_WebMessageReceived(m_webMessageToken);
    if (m_web && m_processFailedToken)
        m_web->remove_ProcessFailed(m_processFailedToken);
    if (m_controller)
        m_controller->Close();
    if (m_web)
        m_web->Release();
    if (m_controller)
        m_controller->Release();
    if (m_env)
        m_env->Release();
    logEvent("webhost", "destroyed");
}

void WebHostWin::onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env)
{
    if (FAILED(hr) || !env)
    {
        logEvent("webhost", "error", "environment hr=" + std::to_string(static_cast<long>(hr)));
        return;
    }
    m_env = env;
    m_env->AddRef();

    auto onCtrl = makeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
        [this](HRESULT h, ICoreWebView2Controller* c) -> HRESULT {
            onControllerCreated(h, c);
            return S_OK;
        });
    m_env->CreateCoreWebView2Controller(m_parent, onCtrl);
    onCtrl->Release();
}

void WebHostWin::onControllerCreated(HRESULT hr, ICoreWebView2Controller* controller)
{
    if (FAILED(hr) || !controller)
    {
        logEvent("webhost", "error", "controller hr=" + std::to_string(static_cast<long>(hr)));
        return;
    }
    m_controller = controller;
    m_controller->AddRef();
    m_controller->get_CoreWebView2(&m_web);
    if (!m_web)
    {
        logEvent("webhost", "error", "no CoreWebView2");
        return;
    }

    RECT bounds{};
    GetClientRect(m_parent, &bounds);
    m_controller->put_Bounds(bounds);
    m_controller->put_IsVisible(TRUE);

    // Messages from the page: window.chrome.webview.postMessage(string)
    auto onMsg = makeCallback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
        [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            onWebMessage(sender, args);
            return S_OK;
        });
    m_web->add_WebMessageReceived(onMsg, &m_webMessageToken);
    m_webMessageHooked = true;
    onMsg->Release();

    // Renderer crash or hang: WebView2 reports ProcessFailed on the same thread.
    auto onFailed = makeCallback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
        [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
            onProcessFailed();
            return S_OK;
        });
    m_web->add_ProcessFailed(onFailed, &m_processFailedToken);
    onFailed->Release();

    logEvent("webhost", "ready");
    for (const std::string& script : m_documentScripts)
        registerDocumentScript(script);
    flushPending();
}

void WebHostWin::onWebMessage(ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args)
{
    if (!args)
        return;
    LPWSTR raw = nullptr;
    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw)
    {
        const std::string text = toUtf8(raw);
        CoTaskMemFree(raw);
        // Probe requirement: log every 5th page message so the stage 2 gate can read the counter.
        if (++m_messageCount % 5 == 0)
            logEvent("webhost", "tick", text);
        if (m_onMessage)
            m_onMessage(text);
    }
}

bool WebHostWin::navigate(const std::string& url)
{
    if (!m_web)
    {
        m_pendingUrl = url;
        return true;
    }
    const std::wstring w = toWide(url);
    return SUCCEEDED(m_web->Navigate(w.c_str()));
}

void WebHostWin::postMessage(const std::string& jsonUtf8)
{
    if (!m_web)
    {
        m_pendingPosts.push_back(jsonUtf8);
        return;
    }
    const std::wstring w = toWide(jsonUtf8);
    m_web->PostWebMessageAsJson(w.c_str());
}

void WebHostWin::flushPending()
{
    if (!m_pendingUrl.empty())
    {
        navigate(m_pendingUrl);
        m_pendingUrl.clear();
    }
    while (!m_pendingPosts.empty())
    {
        postMessage(m_pendingPosts.front());
        m_pendingPosts.pop_front();
    }
}

void WebHostWin::setBounds(int x, int y, int width, int height)
{
    if (!m_controller)
        return;
    RECT r{ x, y, x + width, y + height };
    m_controller->put_Bounds(r);
}

void WebHostWin::setVisible(bool visible)
{
    if (m_controller)
        m_controller->put_IsVisible(visible ? TRUE : FALSE);
}

void WebHostWin::reparent(void* newParentNativeHandle)
{
    // Stage 3 measures this; the first prototype only records the request.
    m_parent = static_cast<HWND>(newParentNativeHandle);
    logEvent("webhost", "reparent_requested");
}

void WebHostWin::setMessageHandler(MessageHandler handler)
{
    m_onMessage = std::move(handler);
}

void WebHostWin::setFailureHandler(FailureHandler handler)
{
    m_onFailure = std::move(handler);
}

void WebHostWin::addDocumentScript(const std::string& scriptPath)
{
    std::ifstream in(scriptPath, std::ios::binary);
    if (!in)
    {
        logEvent("webhost", "error", "script not found: " + scriptPath);
        return;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();
    m_documentScripts.push_back(source);
    if (m_web)
        registerDocumentScript(source);
}

void WebHostWin::registerDocumentScript(const std::string& source)
{
    const std::wstring w = toWide(source);
    auto onAdded = makeCallback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler, HRESULT, LPCWSTR>(
        [](HRESULT hr, LPCWSTR) -> HRESULT {
            logEvent("webhost", "script_added", "hr=" + std::to_string(static_cast<long>(hr)));
            return S_OK;
        });
    m_web->AddScriptToExecuteOnDocumentCreated(w.c_str(), onAdded);
    onAdded->Release();
}

void WebHostWin::onProcessFailed()
{
    logEvent("webhost", "renderer_failed");
    if (m_onFailure)
        m_onFailure();
}

WebHost* createWebHost(void* nativeParentHandle, const std::string& userDataDir)
{
    if (!nativeParentHandle)
        return nullptr;
    return new WebHostWin(static_cast<HWND>(nativeParentHandle), userDataDir);
}
}

#endif // _WIN32
