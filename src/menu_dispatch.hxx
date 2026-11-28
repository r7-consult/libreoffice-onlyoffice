#pragma once

// Handles clicks on the plugin entries of the editor context menu.
// Items carry the CommandURL "vnd.pluginhost:<pluginId>:<itemId>"; the protocol handler registered in
// ProtocolHandler.xcu routes such URLs here, and the click is forwarded to the plugin page as
// {type:"event", name:"onContextMenuClick", data:{id}}.
// Draft: not compiled.

#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase.hxx>
#include <rtl/ustring.hxx>

namespace pluginhost
{
// SDK builds (no LIBO_INTERNAL_ONLY) have no global OUString: only rtl::OUString exists (rtl/ustring.hxx).
using ::rtl::OUString;

class MenuDispatch : public cppu::WeakImplHelper<css::frame::XDispatchProvider, css::frame::XDispatch,
                                                  css::lang::XInitialization, css::lang::XServiceInfo>
{
public:
    explicit MenuDispatch(const css::uno::Reference<css::uno::XComponentContext>& ctx);

    // XInitialization (called by the protocol handler machinery with the frame)
    void SAL_CALL initialize(const css::uno::Sequence<css::uno::Any>& args) override;

    // XDispatchProvider
    css::uno::Reference<css::frame::XDispatch> SAL_CALL queryDispatch(
        const css::util::URL& url, const OUString& targetFrameName, sal_Int32 searchFlags) override;
    css::uno::Sequence<css::uno::Reference<css::frame::XDispatch>> SAL_CALL queryDispatches(
        const css::uno::Sequence<css::frame::DispatchDescriptor>& requests) override;

    // XDispatch
    void SAL_CALL dispatch(const css::util::URL& url, const css::uno::Sequence<css::beans::PropertyValue>& args) override;
    void SAL_CALL addStatusListener(const css::uno::Reference<css::frame::XStatusListener>&, const css::util::URL&) override {}
    void SAL_CALL removeStatusListener(const css::uno::Reference<css::frame::XStatusListener>&, const css::util::URL&) override {}

    // XServiceInfo
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& name) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    static OUString getImplementationName_static();
    static css::uno::Sequence<OUString> getSupportedServiceNames_static();
    static css::uno::Reference<css::uno::XInterface> SAL_CALL Create(const css::uno::Reference<css::uno::XComponentContext>& ctx);

private:
    css::uno::Reference<css::uno::XComponentContext> m_xContext;
};
}
