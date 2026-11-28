#pragma once

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/ui/XUIElementFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase.hxx>
#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>

namespace pluginhost
{
// SDK builds (no LIBO_INTERNAL_ONLY) have no global OUString: only rtl::OUString exists (rtl/ustring.hxx).
using ::rtl::OUString;

// Creates PluginPanel instances for resource URLs of the form
// private:resource/toolpanel/org.example.pluginhost/<PanelId>
class PanelFactory : public cppu::WeakImplHelper<css::ui::XUIElementFactory, css::lang::XServiceInfo>
{
public:
    explicit PanelFactory(const css::uno::Reference<css::uno::XComponentContext>& ctx);

    // XUIElementFactory
    css::uno::Reference<css::ui::XUIElement> SAL_CALL createUIElement(
        const OUString& rResourceURL, const css::uno::Sequence<css::beans::PropertyValue>& rArgs) override;

    // XServiceInfo
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& rServiceName) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    static OUString getImplementationName_static();
    static css::uno::Sequence<OUString> getSupportedServiceNames_static();
    static css::uno::Reference<css::uno::XInterface> SAL_CALL Create(
        const css::uno::Reference<css::uno::XComponentContext>& ctx);

private:
    css::uno::Reference<css::uno::XComponentContext> m_xContext;
    osl::Mutex m_aMutex;
};
}
