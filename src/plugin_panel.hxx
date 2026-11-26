#pragma once

#include <com/sun/star/awt/XWindow.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/awt/XWindowListener.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/ui/XSidebarPanel.hpp>
#include <com/sun/star/ui/XToolPanel.hpp>
#include <com/sun/star/ui/XUIElement.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/basemutex.hxx>
#include <cppuhelper/compbase.hxx>
#include <osl/mutex.hxx>

#include <string>

namespace pluginhost
{
// A sidebar panel with one native child window (systemchildwindow).
// The window hosts the plugin browser owned by HostService (stage 2-3).
class PluginPanel : private cppu::BaseMutex,
                    public cppu::WeakComponentImplHelper<css::ui::XUIElement, css::ui::XToolPanel, css::ui::XSidebarPanel>
{
public:
    PluginPanel(const css::uno::Reference<css::uno::XComponentContext>& ctx,
                const css::uno::Reference<css::awt::XWindow>& xParentWindow,
                const OUString& resourceURL);

    // XUIElement
    css::uno::Reference<css::frame::XFrame> SAL_CALL getFrame() override;
    OUString SAL_CALL getResourceURL() override;
    sal_Int16 SAL_CALL getType() override;
    css::uno::Reference<css::uno::XInterface> SAL_CALL getRealInterface() override;

    // XToolPanel
    css::uno::Reference<css::awt::XWindow> SAL_CALL getWindow() override;
    css::uno::Reference<css::accessibility::XAccessible> SAL_CALL createAccessible(
        const css::uno::Reference<css::accessibility::XAccessible>& rParentAccessible) override;

    // XSidebarPanel
    css::ui::LayoutSize SAL_CALL getHeightForWidth(sal_Int32 nWidth) override;
    sal_Int32 SAL_CALL getMinimalWidth() override;

protected:
    void SAL_CALL disposing() override;

private:
    void attachPlugin(const css::uno::Reference<css::awt::XWindow>& xChild, sal_Int32 width, sal_Int32 height);

    css::uno::Reference<css::uno::XComponentContext> m_xContext;
    css::uno::Reference<css::awt::XWindow> m_xWindow;
    OUString m_sResourceURL;
    css::uno::Reference<css::awt::XWindowListener> m_xResizeListener;
    std::string m_pluginId;
    bool m_attached = false;
};
}
