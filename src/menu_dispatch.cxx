#include "menu_dispatch.hxx"

#include "json_mini.hxx"
#include "log.hxx"
#include "protocol_adapter.hxx"

#include <com/sun/star/util/URL.hpp>
#include <cppuhelper/supportsservice.hxx>

namespace pluginhost
{
using css::uno::Reference;
using css::uno::Sequence;

namespace
{
// A char array, not a char pointer: in SDK builds OUString::startsWith accepts a string literal only as a const
// char array (ConstCharArrayDetector), not as a const char*.
const char kScheme[] = "vnd.pluginhost:";
}

MenuDispatch::MenuDispatch(const Reference<css::uno::XComponentContext>& ctx)
    : m_xContext(ctx)
{
}

void SAL_CALL MenuDispatch::initialize(const Sequence<css::uno::Any>&) {}

Reference<css::frame::XDispatch> SAL_CALL MenuDispatch::queryDispatch(const css::util::URL& url, const OUString&, sal_Int32)
{
    if (url.Complete.startsWith(kScheme))
        return this;
    return {};
}

Sequence<Reference<css::frame::XDispatch>> SAL_CALL MenuDispatch::queryDispatches(
    const Sequence<css::frame::DispatchDescriptor>& requests)
{
    Sequence<Reference<css::frame::XDispatch>> out(requests.getLength());
    for (sal_Int32 i = 0; i < requests.getLength(); ++i)
        out.getArray()[i] = queryDispatch(requests[i].FeatureURL, requests[i].FrameName, requests[i].SearchFlags);
    return out;
}

void SAL_CALL MenuDispatch::dispatch(const css::util::URL& url, const Sequence<css::beans::PropertyValue>&)
{
    // vnd.pluginhost:<pluginId>:<itemId>  (plugin ids are folder names without ':')
    const std::string full = url.Complete.toUtf8().getStr();
    const std::string rest = full.substr(std::string(kScheme).size());
    const size_t colon = rest.find(':');
    if (colon == std::string::npos)
    {
        logEvent("ctxmenu", "click_error", full);
        return;
    }
    const std::string pluginId = rest.substr(0, colon);
    const std::string itemId = rest.substr(colon + 1);
    logEvent("ctxmenu", "click", pluginId + "|" + itemId);
    ProtocolAdapter::instance().sendEvent(pluginId, "onContextMenuClick", "{\"id\":\"" + jsonEscape(itemId) + "\"}");
}

OUString SAL_CALL MenuDispatch::getImplementationName()
{
    return getImplementationName_static();
}

sal_Bool SAL_CALL MenuDispatch::supportsService(const OUString& name)
{
    return cppu::supportsService(this, name);
}

Sequence<OUString> SAL_CALL MenuDispatch::getSupportedServiceNames()
{
    return getSupportedServiceNames_static();
}

OUString MenuDispatch::getImplementationName_static()
{
    return OUString("org.example.pluginhost.MenuDispatch");
}

Sequence<OUString> MenuDispatch::getSupportedServiceNames_static()
{
    // No brace initialisation: the initializer_list constructor of Sequence exists only in internal builds.
    Sequence<OUString> names(1);
    names.getArray()[0] = "com.sun.star.frame.ProtocolHandler";
    return names;
}

Reference<css::uno::XInterface> SAL_CALL MenuDispatch::Create(const Reference<css::uno::XComponentContext>& ctx)
{
    return static_cast<cppu::OWeakObject*>(new MenuDispatch(ctx));
}
}
