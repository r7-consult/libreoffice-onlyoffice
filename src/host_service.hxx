#pragma once

// Owns plugin browser sessions. A session outlives the sidebar panel that shows it:
// PluginPanel attaches the session to its native window and detaches it when the
// panel is destroyed. The page is not reloaded on detach or reattach (stage 3).

#include "webhost.hxx"

#include <com/sun/star/uno/XComponentContext.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pluginhost
{
class HostService
{
public:
    static HostService& instance();

    // Must be called once from the component factory with the component context.
    void setContext(const css::uno::Reference<css::uno::XComponentContext>& ctx) { m_ctx = ctx; }
    const css::uno::Reference<css::uno::XComponentContext>& context() const { return m_ctx; }

    // Returns the session browser for pluginId, creating it on first use.
    // parentHandle is the native handle of the panel's child window (HWND on Windows).
    WebHost* acquire(const std::string& pluginId, void* parentHandle, const std::string& pageUrl,
                     const std::string& dataDir, int width, int height);

    // Hides the browser and detaches it from its parent. The page keeps running.
    void release(const std::string& pluginId);

    // Recreates the browser after a renderer failure (stage 7). Page state is lost.
    WebHost* restart(const std::string& pluginId, void* parentHandle);

    WebHost* find(const std::string& pluginId);

    // Called by the browser when its renderer process failed (stage 7).
    void markFailed(const std::string& pluginId);

    // Plugin ids of sessions created so far (for logs and tests).
    std::vector<std::string> pluginIds() const;

private:
    HostService() = default;

    struct Session
    {
        std::unique_ptr<WebHost> web;
        std::string pageUrl;
        std::string dataDir;
        bool failed = false;
        int createCount = 0;
    };

    WebHost* createSession(const std::string& pluginId, Session& s, void* parentHandle, int width, int height);

    css::uno::Reference<css::uno::XComponentContext> m_ctx;
    std::map<std::string, Session> m_sessions;
};
}
