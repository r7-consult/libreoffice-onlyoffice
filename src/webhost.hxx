#pragma once

// Platform-neutral browser host used by PluginPanel.
// Implementations: webhost_win.cxx (WebView2). Linux and macOS are stubs
// (stage 10) and are not compiled on Windows.

#include <functional>
#include <string>

namespace pluginhost
{
class WebHost
{
public:
    using MessageHandler = std::function<void(const std::string& jsonUtf8)>;
    virtual ~WebHost() = default;

    // Opens the page at fileOrHttpUrl. Safe to call before the engine is ready;
    // the navigation is queued.
    virtual bool navigate(const std::string& url) = 0;

    // Sends a JSON message to the page (JS side receives it in the message event).
    virtual void postMessage(const std::string& jsonUtf8) = 0;

    // Positions the browser inside its parent child window (client coordinates).
    virtual void setBounds(int x, int y, int width, int height) = 0;

    virtual void setVisible(bool visible) = 0;

    // Moves the browser under a new native parent handle (stage 3).
    virtual void reparent(void* newParentNativeHandle) = 0;

    virtual void setMessageHandler(MessageHandler handler) = 0;

    // Called when the renderer process fails (stage 7). Runs on the LibreOffice main thread.
    using FailureHandler = std::function<void()>;
    virtual void setFailureHandler(FailureHandler handler) = 0;

    // Serves folder at https://<hostName>/ (read-only, deny cross-origin). Plugin pages are then
    // loaded from an https origin instead of file://, so plugins cannot read each other's files.
    // Must be called before navigate(). Default: no mapping (use file URLs).
    virtual void mapFolder(const std::string& hostName, const std::string& folder)
    {
        (void)hostName;
        (void)folder;
    }

    // Script run before each document's own scripts (the plugin runtime).
    virtual void addDocumentScript(const std::string& scriptPath) = 0;

    // True when the engine has finished initialising and the page is loaded.
    virtual bool isReady() const = 0;
};

// Creates the platform implementation for the given native parent handle.
// Returns nullptr if the platform is not supported or initialisation fails synchronously.
// userDataDir is the per-plugin data folder (profile isolation).
WebHost* createWebHost(void* nativeParentHandle, const std::string& userDataDir);
}
