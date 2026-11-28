// Linux browser host: WebKitGTK (webkit2gtk-4.1) in a GtkPlug on the X11 window that LibreOffice gives as the native
// parent. Implements WebHost (webhost.hxx) the way webhost_win.cxx does for WebView2.
//
// DRAFT, NOT COMPILED. Two things must be verified on the Linux host before this is trusted (PLAN_NEXT phase 6):
//   1. The embedding: gtk_plug_new(XID) works only on X11 or XWayland. On native Wayland it returns null and
//      createWebHost returns nullptr (the panel then logs "webhost|unavailable"). The check is the decision point.
//   2. The message-handler signal: script-message-received passes WebKitJavascriptResult, which WebKitGTK marks
//      deprecated in newer releases. Switch to script-message-with-reply-received if the build warns.
//
// Page model (matches the Windows host):
//   - plugin pages are https://<pluginId>.plugin.invalid/... in the panel. The host rewrites that to
//     pluginhost://<host>/... and serves it from the mapped folder through a custom URI scheme.
//   - Navigation to anything else (file:, http:, other schemes) is refused.
//   - window.chrome.webview is provided by a document-start script (kPageShim), so extension/content/runtime.js runs
//     unchanged. Page -> host: window.webkit.messageHandlers.pluginhost.postMessage(text). Host -> page:
//     window.__phDeliver(text), which calls the 'message' listeners with {data: text}.

#ifndef _WIN32

#include "webhost.hxx"

#include "json_mini.hxx"
#include "log.hxx"
#include "paths.hxx"

#include <gdk/gdkx.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace pluginhost
{
namespace
{
const char kScheme[] = "pluginhost";
const char kHttpsPrefix[] = "https://";

// Runs before every page script, so runtime.js sees window.chrome.webview exactly as on WebView2.
const char kPageShim[] = R"PH(
(function () {
  var listeners = [];
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    postMessage: function (text) {
      window.webkit.messageHandlers.pluginhost.postMessage(String(text));
    },
    addEventListener: function (type, fn) {
      if (type === "message") listeners.push(fn);
    }
  };
  Object.defineProperty(window, "__phDeliver", {
    value: function (text) {
      var ev = { data: text };
      for (var i = 0; i < listeners.length; i++) listeners[i](ev);
    }
  });
})();
)PH";

const char* mimeFor(const std::string& path)
{
    const std::size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "js" || ext == "mjs") return "text/javascript";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    return "application/octet-stream";
}

// True when a path (already unescaped, without the leading '/') has no parent-directory segment.
bool safeRelativePath(const std::string& rel)
{
    std::size_t start = 0;
    while (start <= rel.size())
    {
        const std::size_t end = rel.find('/', start);
        const std::string part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

class WebHostWebKit : public WebHost
{
public:
    WebHostWebKit(void* xid, const std::string& userDataDir);
    ~WebHostWebKit() override;

    bool navigate(const std::string& url) override;
    void postMessage(const std::string& jsonUtf8) override;
    void setBounds(int x, int y, int width, int height) override;
    void setVisible(bool visible) override;
    void reparent(void* newParentNativeHandle) override;
    void setMessageHandler(MessageHandler handler) override { m_onMessage = std::move(handler); }
    void setFailureHandler(FailureHandler handler) override { m_onFailure = std::move(handler); }
    void addDocumentScript(const std::string& scriptPath) override;
    void mapFolder(const std::string& hostName, const std::string& folder) override;
    bool isReady() const override { return m_plug != nullptr; }

    // GTK and WebKit callbacks (static, user data = this). Public so the file-level setup can connect them.
    static void onScriptMessage(WebKitUserContentManager* ucm, WebKitJavascriptResult* result, gpointer self);
    static void onSchemeRequest(WebKitURISchemeRequest* request, gpointer self);
    static gboolean onDecidePolicy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                   WebKitPolicyDecisionType type, gpointer self);
    static void onLoadChanged(WebKitWebView* view, WebKitLoadEvent event, gpointer self);
    static gboolean onProcessCrashed(WebKitWebView* view, gpointer self);

private:
    GtkWidget* makePlug(void* xid);
    void runScript(const std::string& script);
    void deliverPending();
    std::string localUrl(const std::string& url) const;

    GtkWidget* m_plug = nullptr;
    WebKitWebView* m_view = nullptr;
    WebKitWebContext* m_context = nullptr;
    WebKitUserContentManager* m_ucm = nullptr;
    MessageHandler m_onMessage;
    FailureHandler m_onFailure;
    std::deque<std::string> m_pendingPosts;
    bool m_loaded = false;
    std::map<std::string, std::string> m_mappings; // host name -> folder with trailing separator
};

WebHostWebKit::WebHostWebKit(void* xid, const std::string& userDataDir)
{
    g_mkdir_with_parents(userDataDir.c_str(), 0700);
    // Profile isolation: each plugin gets its own data and cache folders, as on WebView2.
    WebKitWebsiteDataManager* data = webkit_website_data_manager_new(
        "base-data-directory", joinPath(userDataDir, "data").c_str(),
        "base-cache-directory", joinPath(userDataDir, "cache").c_str(), nullptr);
    m_context = webkit_web_context_new_with_website_data_manager(data);
    g_object_unref(data);

    // Plugin pages are served from this scheme. Treated as secure and CORS-enabled, like an https origin.
    webkit_web_context_register_uri_scheme(m_context, kScheme, &WebHostWebKit::onSchemeRequest, this, nullptr);
    WebKitSecurityManager* security = webkit_web_context_get_security_manager(m_context);
    webkit_security_manager_register_uri_scheme_as_secure(security, kScheme);
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, kScheme);

    m_ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(m_ucm, "pluginhost");
    g_signal_connect(m_ucm, "script-message-received::pluginhost", G_CALLBACK(&WebHostWebKit::onScriptMessage), this);
    WebKitUserScript* shim = webkit_user_script_new(kPageShim, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                    WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(m_ucm, shim);
    webkit_user_script_unref(shim);

    m_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", m_context,
                                          "user-content-manager", m_ucm, nullptr));
    WebKitSettings* settings = webkit_web_view_get_settings(m_view);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);

    g_signal_connect(m_view, "decide-policy", G_CALLBACK(&WebHostWebKit::onDecidePolicy), this);
    g_signal_connect(m_view, "load-changed", G_CALLBACK(&WebHostWebKit::onLoadChanged), this);
    g_signal_connect(m_view, "web-process-crashed", G_CALLBACK(&WebHostWebKit::onProcessCrashed), this);

    m_plug = makePlug(xid);
    if (!m_plug)
    {
        logEvent("webhost", "error", "WebKitGTK needs an X11 or XWayland window; native Wayland is not supported here");
        gtk_widget_destroy(GTK_WIDGET(m_view));
        m_view = nullptr;
    }
}

WebHostWebKit::~WebHostWebKit()
{
    if (m_view)
        g_signal_handlers_disconnect_by_data(m_view, this);
    if (m_ucm)
        g_signal_handlers_disconnect_by_data(m_ucm, this);
    if (m_plug)
        gtk_widget_destroy(m_plug); // destroys the view it contains
    if (m_ucm)
        g_object_unref(m_ucm);
    if (m_context)
        g_object_unref(m_context);
}

GtkWidget* WebHostWebKit::makePlug(void* xid)
{
    if (!xid)
        return nullptr;
    const Window window = static_cast<Window>(reinterpret_cast<std::uintptr_t>(xid));
    GtkWidget* plug = gtk_plug_new(window);
    if (!plug)
        return nullptr;
    gtk_container_add(GTK_CONTAINER(plug), GTK_WIDGET(m_view));
    gtk_widget_show_all(plug);
    return plug;
}

void WebHostWebKit::reparent(void* newParentNativeHandle)
{
    if (!m_view)
        return;
    // GTK refuses to add a widget that is still in another container, so take the view out first.
    // Our own reference keeps the view alive while it is between containers.
    g_object_ref(m_view);
    if (m_plug)
    {
        gtk_container_remove(GTK_CONTAINER(m_plug), GTK_WIDGET(m_view));
        gtk_widget_destroy(m_plug);
        m_plug = nullptr;
    }
    GtkWidget* next = makePlug(newParentNativeHandle);
    g_object_unref(m_view);
    if (!next)
    {
        logEvent("webhost", "error", "reparent: no X11 window for the new parent; the panel stops");
        gtk_widget_destroy(GTK_WIDGET(m_view));
        m_view = nullptr;
        return;
    }
    m_plug = next;
}

std::string WebHostWebKit::localUrl(const std::string& url) const
{
    if (url.compare(0, sizeof kHttpsPrefix - 1, kHttpsPrefix) != 0)
        return url;
    const std::string rest = url.substr(sizeof kHttpsPrefix - 1);
    const std::size_t end = rest.find('/');
    const std::string host = rest.substr(0, end);
    if (m_mappings.find(host) == m_mappings.end())
        return url;
    return std::string(kScheme) + "://" + rest;
}

bool WebHostWebKit::navigate(const std::string& url)
{
    if (!m_view)
        return false;
    webkit_web_view_load_uri(m_view, localUrl(url).c_str());
    return true;
}

void WebHostWebKit::runScript(const std::string& script)
{
    if (m_view)
        webkit_web_view_run_javascript(m_view, script.c_str(), nullptr, nullptr, nullptr);
}

void WebHostWebKit::postMessage(const std::string& jsonUtf8)
{
    if (!m_loaded)
    {
        m_pendingPosts.push_back(jsonUtf8); // delivered when the page has loaded
        return;
    }
    runScript("window.__phDeliver(\"" + jsonEscape(jsonUtf8) + "\");");
}

void WebHostWebKit::deliverPending()
{
    while (!m_pendingPosts.empty())
    {
        const std::string text = m_pendingPosts.front();
        m_pendingPosts.pop_front();
        postMessage(text);
    }
}

void WebHostWebKit::setBounds(int, int, int width, int height)
{
    // The plug fills the X window that LibreOffice places, so only the size applies here.
    if (m_view)
        gtk_widget_set_size_request(GTK_WIDGET(m_view), width, height);
}

void WebHostWebKit::setVisible(bool visible)
{
    if (m_view)
        gtk_widget_set_visible(GTK_WIDGET(m_view), visible ? TRUE : FALSE);
}

void WebHostWebKit::addDocumentScript(const std::string& scriptPath)
{
    std::ifstream in(scriptPath, std::ios::binary);
    if (!in)
    {
        logEvent("webhost", "error", "document script not found: " + scriptPath);
        return;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    WebKitUserScript* script = webkit_user_script_new(buf.str().c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(m_ucm, script);
    webkit_user_script_unref(script);
}

void WebHostWebKit::mapFolder(const std::string& hostName, const std::string& folder)
{
    m_mappings[hostName] = joinPath(folder, "");
}

void WebHostWebKit::onScriptMessage(WebKitUserContentManager*, WebKitJavascriptResult* result, gpointer self)
{
    auto* host = static_cast<WebHostWebKit*>(self);
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    if (!value || !jsc_value_is_string(value))
        return;
    char* text = jsc_value_to_string(value);
    if (text && host->m_onMessage)
        host->m_onMessage(text);
    g_free(text);
}

void WebHostWebKit::onSchemeRequest(WebKitURISchemeRequest* request, gpointer self)
{
    auto* host = static_cast<WebHostWebKit*>(self);
    GError* error = nullptr;

    // Every failure goes through one exit: report an error to WebKit, then free what we own.
    auto fail = [&](GIOErrorEnum code, const char* message, GError* existing) {
        GError* e = existing ? existing : g_error_new_literal(G_IO_ERROR, code, message);
        webkit_uri_scheme_request_finish_error(request, e);
        g_error_free(e);
    };

    const char* uri = webkit_uri_scheme_request_get_uri(request);
    GUri* parsed = uri ? g_uri_parse(uri, G_URI_FLAGS_NONE, &error) : nullptr;
    if (!parsed)
    {
        fail(G_IO_ERROR_INVALID_ARGUMENT, "bad uri", error);
        return;
    }
    const char* hostCStr = g_uri_get_host(parsed);
    const std::string hostName = hostCStr ? hostCStr : "";
    const char* pathCStr = g_uri_get_path(parsed);
    gchar* unescaped = g_uri_unescape_string(pathCStr ? pathCStr : "/", nullptr);
    std::string rel = unescaped ? unescaped : "/";
    g_free(unescaped);
    g_uri_unref(parsed);

    const auto mapping = host->m_mappings.find(hostName);
    if (mapping == host->m_mappings.end())
    {
        fail(G_IO_ERROR_NOT_FOUND, "host not mapped", nullptr);
        return;
    }
    if (!rel.empty() && rel[0] == '/')
        rel.erase(0, 1);
    if (rel.empty())
        rel = "index.html";
    if (!safeRelativePath(rel))
    {
        fail(G_IO_ERROR_PERMISSION_DENIED, "path outside the plugin folder", nullptr);
        return;
    }

    const std::string full = mapping->second + rel;
    GFile* file = g_file_new_for_path(full.c_str());
    GFileInputStream* stream = g_file_read(file, nullptr, &error);
    GFileInfo* info = stream ? g_file_query_info(file, "standard::size", G_FILE_QUERY_INFO_NONE, nullptr, nullptr) : nullptr;
    if (!stream || !info)
    {
        fail(G_IO_ERROR_NOT_FOUND, "file not found", error);
    }
    else
    {
        webkit_uri_scheme_request_finish(request, G_INPUT_STREAM(stream), g_file_info_get_size(info), mimeFor(rel));
    }
    if (info)
        g_object_unref(info);
    if (stream)
        g_object_unref(stream);
    g_object_unref(file);
}

gboolean WebHostWebKit::onDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                                       WebKitPolicyDecisionType type, gpointer)
{
    // Only top-level navigation and new windows are decided here. Everything else keeps its default.
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION)
        return FALSE;
    WebKitNavigationAction* action =
        webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const char* uri = action ? webkit_uri_request_get_uri(webkit_navigation_action_get_request(action)) : nullptr;
    const std::string target = uri ? uri : "";
    const std::string prefix = std::string(kScheme) + "://";
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && target.compare(0, prefix.size(), prefix) == 0)
    {
        webkit_policy_decision_use(decision);
        return TRUE;
    }
    logEvent("webhost", "denied", "navigation outside the plugin host: " + target);
    webkit_policy_decision_ignore(decision);
    return TRUE;
}

void WebHostWebKit::onLoadChanged(WebKitWebView*, WebKitLoadEvent event, gpointer self)
{
    auto* host = static_cast<WebHostWebKit*>(self);
    if (event == WEBKIT_LOAD_FINISHED)
    {
        host->m_loaded = true;
        host->deliverPending();
    }
}

gboolean WebHostWebKit::onProcessCrashed(WebKitWebView*, gpointer self)
{
    auto* host = static_cast<WebHostWebKit*>(self);
    if (host->m_onFailure)
        host->m_onFailure();
    return TRUE;
}
}

WebHost* createWebHost(void* nativeParentHandle, const std::string& userDataDir)
{
    if (!nativeParentHandle)
        return nullptr;
    auto* host = new WebHostWebKit(nativeParentHandle, userDataDir);
    if (!host->isReady())
    {
        delete host;
        return nullptr;
    }
    return host;
}
}

#endif // !_WIN32
