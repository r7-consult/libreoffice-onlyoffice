// Linux browser host: WebKitGTK (webkit2gtk-4.1) in a GtkPlug on the X11 window that LibreOffice gives as the native
// parent. Implements WebHost (webhost.hxx) the way webhost_win.cxx does for WebView2.
//
// DRAFT, NOT COMPILED. Reviewed against the WebKitGTK/GTK3/GLib sources (see review_webhost.md). To verify on Linux:
//   1. The embedding. gtk_plug_new*() never returns null, so the real checks are: GDK display is X11 (not native
//      Wayland), the XID is not a GDK-owned window of this process (the gtk3 VCL plugin hands out the XID of its own
//      frame window, which GtkPlug cannot embed into), and gtk_plug_get_embedded(). Any failure makes createWebHost
//      return nullptr (the panel then logs "webhost|unavailable"). Works with the qt5/kf5 VCL plugin on xcb, where
//      the handle is a native child window; see review_webhost.md for the gtk3 core patch that would make it work.
//   2. script-message-received passes WebKitJavascriptResult in the webkit2gtk-4.1 API (JSCValue only in the 2022
//      API, 6.0); it is not deprecated here.
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

#include <algorithm>
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
    if (ext == "ico") return "image/x-icon";
    if (ext == "webp") return "image/webp";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf") return "font/ttf";
    if (ext == "wasm") return "application/wasm"; // streaming compile needs this exact type
    if (ext == "txt") return "text/plain";
    if (ext == "xml") return "application/xml";
    return "application/octet-stream";
}

// Host names are case-insensitive (WebView2 maps them that way); WebKit keeps the case of a custom-scheme host.
std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return s;
}

// GtkPlug needs GTK on an X11 display. GTK is already running when the gtk3 VCL plugin is in use. Under the qt5/kf5
// plugin it is not, so initialise it here, forced to X11 (GtkPlug does not exist on Wayland), without touching the
// process locale (gtk_init would call setlocale()).
bool ensureX11Gtk()
{
    if (!gdk_display_get_default())
    {
        gtk_disable_setlocale();
        gdk_set_allowed_backends("x11");
        if (!gtk_init_check(nullptr, nullptr))
            return false;
    }
    GdkDisplay* display = gdk_display_get_default();
    return display && GDK_IS_X11_DISPLAY(display);
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
    bool isReady() const override { return m_view != nullptr; }

    // GTK and WebKit callbacks (static, user data = this). Public so the file-level setup can connect them.
    static void onScriptMessage(WebKitUserContentManager* ucm, WebKitJavascriptResult* result, gpointer self);
    static void onSchemeRequest(WebKitURISchemeRequest* request, gpointer self);
    static gboolean onDecidePolicy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                   WebKitPolicyDecisionType type, gpointer self);
    static void onLoadChanged(WebKitWebView* view, WebKitLoadEvent event, gpointer self);
    static void onProcessTerminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason, gpointer self);

private:
    GtkWidget* makePlug(void* xid);
    void park();
    void runScript(const std::string& script);
    void deliverPending();
    std::string localUrl(const std::string& url) const;

    GtkWidget* m_plug = nullptr; // X11 embedding window while a panel exists
    GtkWidget* m_park = nullptr; // offscreen window that holds the view while no panel exists
    WebKitWebView* m_view = nullptr; // we own one reference; containers add their own
    bool m_reloading = false;
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
    if (!ensureX11Gtk())
    {
        logEvent("webhost", "error", "WebKitGTK embedding needs GTK on an X11 or XWayland display; native Wayland is not supported");
        return;
    }
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
    if (!webkit_user_content_manager_register_script_message_handler(m_ucm, "pluginhost"))
        logEvent("webhost", "error", "script message handler 'pluginhost' was not registered");
    g_signal_connect(m_ucm, "script-message-received::pluginhost", G_CALLBACK(&WebHostWebKit::onScriptMessage), this);
    WebKitUserScript* shim = webkit_user_script_new(kPageShim, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                                    WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(m_ucm, shim);
    webkit_user_script_unref(shim);

    // g_object_ref_sink: the view is a floating GtkWidget; we keep one strong reference so it survives being
    // moved between containers (plug <-> parking window) and is never destroyed by a container we drop.
    m_view = WEBKIT_WEB_VIEW(g_object_ref_sink(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", m_context,
                                                            "user-content-manager", m_ucm, nullptr)));
    WebKitSettings* settings = webkit_web_view_get_settings(m_view);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);

    g_signal_connect(m_view, "decide-policy", G_CALLBACK(&WebHostWebKit::onDecidePolicy), this);
    g_signal_connect(m_view, "load-changed", G_CALLBACK(&WebHostWebKit::onLoadChanged), this);
    // web-process-crashed is deprecated since 2.20; web-process-terminated (since 2.20) replaces it.
    g_signal_connect(m_view, "web-process-terminated", G_CALLBACK(&WebHostWebKit::onProcessTerminated), this);

    m_plug = makePlug(xid);
    if (!m_plug)
    {
        logEvent("webhost", "error", "cannot embed WebKitGTK in the given native window (see earlier log lines)");
        gtk_widget_destroy(GTK_WIDGET(m_view));
        g_object_unref(m_view);
        m_view = nullptr; // isReady() is false, createWebHost returns nullptr
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
    if (m_park)
        gtk_widget_destroy(m_park);
    if (m_view)
        g_object_unref(m_view);
    if (m_ucm)
        g_object_unref(m_ucm);
    if (m_context)
        g_object_unref(m_context);
}

// Embeds the view in a GtkPlug on the given X11 window. Returns nullptr, leaving the view parentless, on failure.
GtkWidget* WebHostWebKit::makePlug(void* xid)
{
    GdkDisplay* display = gdk_display_get_default();
    if (!xid || !display || !GDK_IS_X11_DISPLAY(display))
        return nullptr;
    const Window window = static_cast<Window>(reinterpret_cast<std::uintptr_t>(xid));
    // The gtk3 VCL plugin hands out the XID of a GDK window it owns (its frame window). GtkPlug can only embed into a
    // GtkSocket in that case ("Can't create GtkPlug as child of non-GtkSocket") and would become a stray top-level.
    if (gdk_x11_window_lookup_for_display(display, window))
    {
        logEvent("webhost", "error", "native window belongs to this process' GTK (gtk3 VCL plugin); GtkPlug cannot embed into it");
        return nullptr;
    }
    // gtk_plug_new* never returns null; a bad XID or a non-X11 display only shows up as "not embedded".
    GtkWidget* plug = gtk_plug_new_for_display(display, window);
    if (!plug)
        return nullptr;
    if (!gtk_plug_get_embedded(GTK_PLUG(plug)))
    {
        gtk_widget_destroy(plug);
        return nullptr;
    }
    gtk_container_add(GTK_CONTAINER(plug), GTK_WIDGET(m_view));
    gtk_widget_show_all(plug);
    return plug;
}

// Keeps the view alive, hidden from the screen, while no panel window exists (the Windows host parks on a hidden HWND).
void WebHostWebKit::park()
{
    if (!m_park)
        m_park = gtk_offscreen_window_new();
    if (gtk_widget_get_parent(GTK_WIDGET(m_view)) != m_park)
        gtk_container_add(GTK_CONTAINER(m_park), GTK_WIDGET(m_view));
    gtk_widget_show_all(m_park);
}

void WebHostWebKit::reparent(void* newParentNativeHandle)
{
    if (!m_view)
        return;
    GtkWidget* view = GTK_WIDGET(m_view);
    // Take the view out of its container first: destroying a container destroys its children, and GTK refuses to add
    // a widget that still has a parent. Our own reference keeps the view alive in between.
    if (GtkWidget* parent = gtk_widget_get_parent(view))
        gtk_container_remove(GTK_CONTAINER(parent), view);
    if (m_plug)
    {
        gtk_widget_destroy(m_plug);
        m_plug = nullptr;
    }
    // nullptr = the panel is going away: park the browser. Otherwise embed it in the new panel window.
    if (newParentNativeHandle)
    {
        m_plug = makePlug(newParentNativeHandle);
        if (!m_plug)
            logEvent("webhost", "error", "reparent: cannot embed in the new parent window; the view stays parked");
    }
    if (!m_plug)
        park();
}

std::string WebHostWebKit::localUrl(const std::string& url) const
{
    if (url.compare(0, sizeof kHttpsPrefix - 1, kHttpsPrefix) != 0)
        return url;
    const std::string rest = url.substr(sizeof kHttpsPrefix - 1);
    const std::size_t end = rest.find('/');
    const std::string host = lowerAscii(rest.substr(0, end));
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
    if (!m_view)
        return;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    // webkit_web_view_run_javascript is deprecated since 2.40. No callback: the result is not needed.
    webkit_web_view_evaluate_javascript(m_view, script.c_str(), static_cast<gssize>(script.size()), nullptr, nullptr,
                                        nullptr, nullptr, nullptr);
#else
    webkit_web_view_run_javascript(m_view, script.c_str(), nullptr, nullptr, nullptr);
#endif
}

void WebHostWebKit::postMessage(const std::string& jsonUtf8)
{
    if (!m_loaded)
    {
        m_pendingPosts.push_back(jsonUtf8); // delivered when the page has loaded
        return;
    }
    // Guarded: on an error page or before the shim ran, __phDeliver does not exist.
    runScript("window.__phDeliver&&window.__phDeliver(\"" + jsonEscape(jsonUtf8) + "\");");
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
    // The plug sits at (0,0) of the panel's X window (PluginPanel always passes 0,0), so only the size applies.
    // Nothing resizes an XEmbed-less plug for us (there is no GtkSocket), hence the explicit window resize.
    if (!m_view)
        return;
    width = std::max(width, 1);
    height = std::max(height, 1);
    gtk_widget_set_size_request(GTK_WIDGET(m_view), width, height);
    if (m_plug)
        gtk_window_resize(GTK_WINDOW(m_plug), width, height);
}

void WebHostWebKit::setVisible(bool visible)
{
    if (!m_view)
        return;
    gtk_widget_set_visible(GTK_WIDGET(m_view), visible ? TRUE : FALSE);
    if (m_plug) // an empty plug window would otherwise stay mapped over the panel
        gtk_widget_set_visible(m_plug, visible ? TRUE : FALSE);
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
    WebKitUserScript* script = webkit_user_script_new(buf.str().c_str(), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                                      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(m_ucm, script);
    webkit_user_script_unref(script);
}

void WebHostWebKit::mapFolder(const std::string& hostName, const std::string& folder)
{
    m_mappings[lowerAscii(hostName)] = joinPath(folder, "");
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
    const std::string hostName = lowerAscii(hostCStr ? hostCStr : "");
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
    if (event == WEBKIT_LOAD_STARTED)
        host->m_loaded = false; // a new document (navigate, reload): hold messages until it has loaded
    else if (event == WEBKIT_LOAD_FINISHED)
    {
        host->m_loaded = true;
        host->m_reloading = false;
        host->deliverPending();
    }
}

// Same policy as the Windows host: a renderer failure reloads the page; if the reload fails too, report it so the
// session is re-created on the next attach.
void WebHostWebKit::onProcessTerminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason, gpointer self)
{
    auto* host = static_cast<WebHostWebKit*>(self);
    logEvent("webhost", "process_failed", "reason=" + std::to_string(static_cast<int>(reason)));
#if WEBKIT_CHECK_VERSION(2, 34, 0)
    if (reason == WEBKIT_WEB_PROCESS_TERMINATED_BY_API)
        return; // we asked for it
#endif
    host->m_loaded = false;
    if (!host->m_reloading)
    {
        host->m_reloading = true;
        logEvent("webhost", "renderer_failed");
        webkit_web_view_reload(view);
        return;
    }
    if (host->m_onFailure)
        host->m_onFailure();
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
