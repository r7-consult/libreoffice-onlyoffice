#ifdef _WIN32

#include "webhost_win.hxx"

#include "log.hxx"

#include <WebView2.h>
#include <objbase.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <utility>

// CreateCoreWebView2EnvironmentWithOptions is declared in WebView2.h and resolved from
// WebView2LoaderStatic.lib (no WebView2Loader.dll to ship).

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
    : m_alive(std::make_shared<bool>(true))
    , m_parent(parent)
    , m_userDataDir(std::move(userDataDir))
{
    const std::wstring dataDir = toWide(m_userDataDir);
    auto onEnv = makeCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [this, alive = m_alive](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (!*alive)
                return S_OK; // host already destroyed
            onEnvironmentCreated(hr, env);
            return S_OK;
        });
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr, onEnv);
    onEnv->Release();
    logEvent("webhost", "env_requested", "hr=" + std::to_string(static_cast<long>(hr)));
    if (FAILED(hr))
        logEvent("webhost", "runtime_missing",
                 "WebView2 Runtime not found or not usable; install the Evergreen Runtime from Microsoft");
}

WebHostWin::~WebHostWin()
{
    *m_alive = false; // pending COM callbacks become no-ops
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
        [this, alive = m_alive](HRESULT h, ICoreWebView2Controller* c) -> HRESULT {
            if (!*alive)
            {
                if (c)
                    c->Close(); // host destroyed while the controller was being created
                return S_OK;
            }
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
        [this, alive = m_alive](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            if (!*alive)
                return S_OK;
            onWebMessage(sender, args);
            return S_OK;
        });
    m_web->add_WebMessageReceived(onMsg, &m_webMessageToken);
    m_webMessageHooked = true;
    onMsg->Release();

    // Renderer crash or hang: WebView2 reports ProcessFailed on the same thread.
    auto onFailed = makeCallback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
        [this, alive = m_alive](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            if (!*alive)
                return S_OK;
            onProcessFailed(args);
            return S_OK;
        });
    m_web->add_ProcessFailed(onFailed, &m_processFailedToken);
    onFailed->Release();

    logEvent("webhost", "ready");
    for (const auto& m : m_pendingMappings)
        applyMapping(m.first, m.second);
    m_pendingMappings.clear();
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

namespace
{
// Hidden window that keeps the browser alive while no panel exists. A sidebar panel window
// is destroyed with its deck; without parking, WebView2 (a child of that window) dies with it.
HWND parkingWindow()
{
    static HWND hwnd = CreateWindowExW(0, L"STATIC", L"PluginHostParking", WS_POPUP, 0, 0, 1, 1,
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return hwnd;
}
}

void WebHostWin::reparent(void* newParentNativeHandle)
{
    // nullptr = park (panel going away). Otherwise move under the new panel window.
    m_parent = newParentNativeHandle ? static_cast<HWND>(newParentNativeHandle) : parkingWindow();
    if (m_controller)
    {
        const HRESULT hr = m_controller->put_ParentWindow(m_parent);
        logEvent("webhost", "reparent", "hr=" + std::to_string(static_cast<long>(hr)));
    }
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

void WebHostWin::mapFolder(const std::string& hostName, const std::string& folder)
{
    if (m_web)
        applyMapping(hostName, folder);
    else
        m_pendingMappings.emplace_back(hostName, folder); // applied when the engine is ready
}

void WebHostWin::applyMapping(const std::string& hostName, const std::string& folder)
{
    ICoreWebView2_3* web3 = nullptr;
    if (FAILED(m_web->QueryInterface(IID_PPV_ARGS(&web3))) || !web3)
    {
        logEvent("webhost", "error", "virtual host mapping needs WebView2 runtime >= 1.0.774");
        return;
    }
    const HRESULT hr = web3->SetVirtualHostNameToFolderMapping(
        toWide(hostName).c_str(), toWide(folder).c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    web3->Release();
    logEvent("webhost", "mapped", hostName + "|hr=" + std::to_string(static_cast<long>(hr)));
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

void WebHostWin::onProcessFailed(ICoreWebView2ProcessFailedEventArgs* args)
{
    COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
    if (args)
        args->get_ProcessFailedKind(&kind);
    logEvent("webhost", "process_failed", "kind=" + std::to_string(static_cast<int>(kind)));
    switch (kind)
    {
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE:
        // Documented recovery: the browser process is fine, only reload the page. No session restart.
        logEvent("webhost", "renderer_failed");
        if (m_web)
            m_web->Reload();
        break;
    case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED:
        // The whole browser is gone: the controller is unusable; re-created on next attach.
        logEvent("webhost", "renderer_failed");
        if (m_onFailure)
            m_onFailure();
        break;
    default:
        break; // utility/GPU process: WebView2 recovers by itself
    }
}

WebHost* createWebHost(void* nativeParentHandle, const std::string& userDataDir)
{
    if (!nativeParentHandle)
        return nullptr;
    return new WebHostWin(static_cast<HWND>(nativeParentHandle), userDataDir);
}
}

#endif // _WIN32
