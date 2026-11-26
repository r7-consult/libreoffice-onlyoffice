#include "host_service.hxx"

#include "log.hxx"
#include "protocol_adapter.hxx"

namespace pluginhost
{
HostService& HostService::instance()
{
    static HostService s;
    return s;
}

WebHost* HostService::createSession(const std::string& pluginId, Session& s, void* parentHandle, int width, int height)
{
    s.web.reset(createWebHost(parentHandle, s.dataDir));
    if (!s.web)
    {
        logEvent("webhost", "unavailable", pluginId);
        return nullptr;
    }
    s.createCount++;
    logEvent("webhost", "created", pluginId + "|count=" + std::to_string(s.createCount));

    s.web->setMessageHandler([pluginId](const std::string& json) {
        ProtocolAdapter::instance().onPageMessage(pluginId, json);
    });
    s.web->setFailureHandler([pluginId]() {
        HostService::instance().markFailed(pluginId);
    });
    s.web->addDocumentScript(ProtocolAdapter::runtimeScriptPath());
    s.web->navigate(s.pageUrl);
    s.web->setBounds(0, 0, width, height);
    s.web->setVisible(true);
    s.failed = false;
    return s.web.get();
}

WebHost* HostService::acquire(const std::string& pluginId, void* parentHandle, const std::string& pageUrl,
                              const std::string& dataDir, int width, int height)
{
    auto it = m_sessions.find(pluginId);
    if (it == m_sessions.end())
    {
        Session s;
        s.pageUrl = pageUrl;
        s.dataDir = dataDir;
        it = m_sessions.emplace(pluginId, std::move(s)).first;
        return createSession(pluginId, it->second, parentHandle, width, height);
    }

    Session& s = it->second;
    if (!s.web || s.failed)
        return createSession(pluginId, s, parentHandle, width, height);

    // Reuse: move the existing browser under the new parent. No reload.
    s.web->reparent(parentHandle);
    s.web->setBounds(0, 0, width, height);
    s.web->setVisible(true);
    logEvent("webhost", "reattached", pluginId);
    return s.web.get();
}

void HostService::release(const std::string& pluginId)
{
    auto it = m_sessions.find(pluginId);
    if (it == m_sessions.end() || !it->second.web)
        return;
    it->second.web->setVisible(false);
    logEvent("webhost", "hidden", pluginId);
}

WebHost* HostService::restart(const std::string& pluginId, void* parentHandle)
{
    auto it = m_sessions.find(pluginId);
    if (it == m_sessions.end())
        return nullptr;
    it->second.web.reset();
    logEvent("webhost", "restart", pluginId);
    return createSession(pluginId, it->second, parentHandle, 0, 0);
}

WebHost* HostService::find(const std::string& pluginId)
{
    auto it = m_sessions.find(pluginId);
    return it == m_sessions.end() ? nullptr : it->second.web.get();
}

void HostService::markFailed(const std::string& pluginId)
{
    auto it = m_sessions.find(pluginId);
    if (it == m_sessions.end())
        return;
    it->second.failed = true;
    logEvent("webhost", "renderer_failed", pluginId);
}

std::vector<std::string> HostService::pluginIds() const
{
    std::vector<std::string> ids;
    for (const auto& kv : m_sessions)
        ids.push_back(kv.first);
    return ids;
}
}
