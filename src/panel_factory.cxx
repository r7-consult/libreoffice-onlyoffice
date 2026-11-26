#include "panel_factory.hxx"

#include "host_service.hxx"
#include "log.hxx"
#include "plugin_panel.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/container/NoSuchElementException.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/awt/XWindow.hpp>
#include <cppuhelper/supportsservice.hxx>

namespace pluginhost
{
using css::awt::XWindow;
using css::beans::PropertyValue;
using css::container::NoSuchElementException;
using css::lang::IllegalArgumentException;
using css::uno::Reference;
using css::uno::Sequence;
using css::uno::XComponentContext;
using css::uno::XInterface;
using css::ui::XUIElement;

namespace
{
const char* const kUrlPrefix = "private:resource/toolpanel/org.example.pluginhost/";
}

PanelFactory::PanelFactory(const Reference<XComponentContext>& ctx)
    : m_xContext(ctx)
{
    HostService::instance().setContext(ctx);
}

Reference<XUIElement> SAL_CALL PanelFactory::createUIElement(
    const OUString& rResourceURL, const Sequence<PropertyValue>& rArgs)
{
    osl::MutexGuard guard(m_aMutex);

    if (!rResourceURL.startsWith(kUrlPrefix))
        throw NoSuchElementException(rResourceURL, *this);

    Reference<XWindow> xParentWindow;
    Reference<css::frame::XFrame> xFrame; // frame of the document that shows this sidebar
    for (const PropertyValue& arg : rArgs)
    {
        if (arg.Name == "ParentWindow")
            xParentWindow.set(arg.Value, css::uno::UNO_QUERY);
        else if (arg.Name == "Frame")
            xFrame.set(arg.Value, css::uno::UNO_QUERY);
    }
    if (!xParentWindow.is())
    {
        logEvent("factory", "error", "no ParentWindow in creation arguments");
        throw IllegalArgumentException("No parent window provided for the plugin host panel.", *this, 2);
    }

    logEvent("factory", "createUIElement", rResourceURL.toUtf8().getStr());
    return new PluginPanel(m_xContext, xParentWindow, xFrame, rResourceURL);
}

OUString SAL_CALL PanelFactory::getImplementationName()
{
    return getImplementationName_static();
}

sal_Bool SAL_CALL PanelFactory::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

Sequence<OUString> SAL_CALL PanelFactory::getSupportedServiceNames()
{
    return getSupportedServiceNames_static();
}

OUString PanelFactory::getImplementationName_static()
{
    return OUString("org.example.pluginhost.PanelFactory");
}

Sequence<OUString> PanelFactory::getSupportedServiceNames_static()
{
    return Sequence<OUString>{ OUString("org.example.pluginhost.PanelFactory") };
}

Reference<XInterface> SAL_CALL PanelFactory::Create(const Reference<XComponentContext>& ctx)
{
    return static_cast<cppu::OWeakObject*>(new PanelFactory(ctx));
}
}
