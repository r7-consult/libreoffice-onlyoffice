#include "plugin_panel.hxx"

#include "context_menu.hxx"
#include "host_service.hxx"
#include "log.hxx"
#include "paths.hxx"
#include "plugin_config.hxx"

#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/WindowClass.hpp>
#include <com/sun/star/awt/WindowDescriptor.hpp>
#include <com/sun/star/awt/WindowAttribute.hpp>
#include <com/sun/star/awt/XToolkit.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/awt/XSystemDependentWindowPeer.hpp>
#include <com/sun/star/awt/PosSize.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XDesktop.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/SystemDependent.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/ui/UIElementType.hpp>
#include <rtl/process.h>

#include <com/sun/star/awt/XWindowListener.hpp>
#include <cppuhelper/implbase.hxx>
#include <cstdlib>
#include <utility>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pluginhost
{
using css::awt::WindowDescriptor;
using css::awt::XToolkit;
using css::awt::XWindow;
using css::awt::XWindowPeer;
using css::uno::Reference;
using css::uno::XComponentContext;
using css::uno::XInterface;
using css::ui::LayoutSize;

namespace
{
// Creates a native child window of the parent panel window.
// Service name "systemchildwindow" is handled in toolkit/source/awt/vclxtoolkit.cxx.
Reference<XWindow> createChildWindow(const Reference<XComponentContext>& ctx, const Reference<XWindow>& parent)
{
    Reference<XWindowPeer> xParentPeer(parent, css::uno::UNO_QUERY);
    if (!xParentPeer.is())
        return {};

    Reference<XToolkit> xToolkit = css::awt::Toolkit::create(ctx);
    WindowDescriptor aDesc;
    aDesc.Type = css::awt::WindowClass_SIMPLE;
    aDesc.WindowServiceName = "systemchildwindow";
    aDesc.Parent = xParentPeer;
    aDesc.WindowAttributes = css::awt::WindowAttribute::BORDER;
    return Reference<XWindow>(xToolkit->createWindow(aDesc), css::uno::UNO_QUERY);
}

// Native handle of a systemchildwindow via XSystemDependentWindowPeer.
// getWindowHandle returns an Any. Verified in toolkit/source/awt/vclxsystemdependentwindow.cxx:
// Windows: sal_IntPtr(HWND); macOS: sal_IntPtr(NSView*); X11: css::awt::SystemDependentXWindow (not handled yet).
void* nativeWindowHandle(const Reference<XWindow>& xWindow)
{
    Reference<css::awt::XSystemDependentWindowPeer> xDep(xWindow, css::uno::UNO_QUERY);
    if (!xDep.is())
        return nullptr;
    css::uno::Sequence<sal_Int8> aProcessId(16);
    rtl_getGlobalProcessId(reinterpret_cast<sal_uInt8*>(aProcessId.getArray()));
#if defined(_WIN32)
    const sal_Int16 nType = css::lang::SystemDependent::SYSTEM_WIN32;
#elif defined(__APPLE__)
    const sal_Int16 nType = css::lang::SystemDependent::SYSTEM_MAC;
#else
    const sal_Int16 nType = css::lang::SystemDependent::SYSTEM_XWINDOW;
#endif
    const css::uno::Any aHandle = xDep->getWindowHandle(aProcessId, nType);
    sal_Int64 nHandle = 0; // sal_IntPtr is 64-bit on x64; >>= widens a 32-bit value too
    if (!(aHandle >>= nHandle) || nHandle == 0)
        return nullptr;
    return reinterpret_cast<void*>(static_cast<sal_IntPtr>(nHandle));
}

// Keeps the browser the size of the panel window when the sidebar is resized.
class ResizeListener : public cppu::WeakImplHelper<css::awt::XWindowListener>
{
public:
    explicit ResizeListener(std::string pluginId)
        : m_pluginId(std::move(pluginId))
    {
    }
    void SAL_CALL windowResized(const css::awt::WindowEvent& e) override
    {
        if (WebHost* web = HostService::instance().find(m_pluginId))
            web->setBounds(0, 0, e.Width, e.Height);
    }
    void SAL_CALL windowMoved(const css::awt::WindowEvent&) override {}
    void SAL_CALL windowShown(const css::lang::EventObject&) override {}
    void SAL_CALL windowHidden(const css::lang::EventObject&) override {}
    void SAL_CALL disposing(const css::lang::EventObject&) override {}

private:
    std::string m_pluginId;
};

// Plugin shown by this prototype panel: PLUGINHOST_PLUGIN (folder name) if set,
// otherwise the first valid package under pluginsRoot().
bool choosePlugin(PluginConfig& out)
{
    const char* forced = std::getenv("PLUGINHOST_PLUGIN");
    const std::vector<PluginConfig> all = scanPlugins(pluginsRoot());
    for (const PluginConfig& cfg : all)
    {
        if (forced && *forced)
        {
            const std::string folder = cfg.installDir.substr(cfg.installDir.find_last_of('\\') + 1);
            if (folder != forced)
                continue;
        }
        out = cfg;
        return true;
    }
    return false;
}

std::string folderName(const std::string& installDir)
{
    return installDir.substr(installDir.find_last_of('\\') + 1);
}

// Attaches the context menu interceptor to the document of the frame that owns this panel.
// Each document window has its own sidebar and so its own panel: every document gets the entry.
void attachContextMenuToFrame(const Reference<css::frame::XFrame>& frame)
{
    try
    {
        if (frame.is() && frame->getController().is())
            attachContextMenu(frame->getController());
        else
            logEvent("panel", "error", "no frame controller; context menu not attached");
    }
    catch (...)
    {
        logEvent("panel", "error", "context menu attach failed");
    }
}
}

PluginPanel::PluginPanel(const Reference<XComponentContext>& ctx,
                         const Reference<XWindow>& xParentWindow,
                         const Reference<css::frame::XFrame>& xFrame,
                         const OUString& resourceURL)
    : cppu::BaseMutex()
    , WeakComponentImplHelper(m_aMutex)
    , m_xContext(ctx)
    , m_xFrame(xFrame)
    , m_sResourceURL(resourceURL)
{
    m_xWindow = createChildWindow(ctx, xParentWindow);
    if (!m_xWindow.is())
    {
        logEvent("panel", "error", "systemchildwindow was not created");
        return;
    }
    const css::awt::Rectangle aSize = xParentWindow->getPosSize();
    m_xWindow->setPosSize(0, 0, aSize.Width, aSize.Height, css::awt::PosSize::POSSIZE);
    m_xWindow->setVisible(true);
    logEvent("panel", "created", m_sResourceURL.toUtf8().getStr());

    attachPlugin(m_xWindow, aSize.Width, aSize.Height);
    attachContextMenuToFrame(m_xFrame);
}

void PluginPanel::attachPlugin(const Reference<XWindow>& xChild, sal_Int32 width, sal_Int32 height)
{
    PluginConfig cfg;
    if (!choosePlugin(cfg))
    {
        logEvent("panel", "error", "no plugin package under " + pluginsRoot());
        return;
    }
    m_pluginId = folderName(cfg.installDir);
    // Served from a virtual https host mapped to the plugin folder (see WebHost::mapFolder).
    const std::string pageUrl = "https://" + m_pluginId + ".plugin.invalid/" + cfg.variationUrls.front();

    void* hwnd = nativeWindowHandle(xChild);
    if (!hwnd)
    {
        logEvent("panel", "error", "no native window handle (stage 2 is Windows-only)");
        return;
    }
    m_attached = true;
    HostService::instance().acquire(m_pluginId, hwnd, pageUrl, pluginDataDir(m_pluginId), cfg.installDir, width, height);
    m_xResizeListener = new ResizeListener(m_pluginId);
    xChild->addWindowListener(m_xResizeListener);
}

css::uno::Reference<css::frame::XFrame> SAL_CALL PluginPanel::getFrame()
{
    return m_xFrame;
}

OUString SAL_CALL PluginPanel::getResourceURL()
{
    return m_sResourceURL;
}

sal_Int16 SAL_CALL PluginPanel::getType()
{
    return css::ui::UIElementType::TOOLPANEL;
}

Reference<XInterface> SAL_CALL PluginPanel::getRealInterface()
{
    osl::MutexGuard guard(m_aMutex);
    return static_cast<css::ui::XToolPanel*>(this);
}

Reference<XWindow> SAL_CALL PluginPanel::getWindow()
{
    osl::MutexGuard guard(m_aMutex);
    return m_xWindow;
}

Reference<css::accessibility::XAccessible> SAL_CALL PluginPanel::createAccessible(
    const Reference<css::accessibility::XAccessible>&)
{
    return {};
}

LayoutSize SAL_CALL PluginPanel::getHeightForWidth(sal_Int32)
{
    // DeckLayouter requires 0 <= Minimum <= Preferred <= Maximum (sfx2/source/sidebar/DeckLayouter.cxx).
    // A large Maximum lets the deck give the plugin all spare height.
    return LayoutSize(300, 600, 100000);
}

sal_Int32 SAL_CALL PluginPanel::getMinimalWidth()
{
    return 200;
}

void SAL_CALL PluginPanel::disposing()
{
    osl::MutexGuard guard(m_aMutex);
    logEvent("panel", "disposed", m_sResourceURL.toUtf8().getStr());
    // Stage 3: the session stays alive; only the view is hidden and detached.
    if (m_xWindow.is() && m_xResizeListener.is())
        m_xWindow->removeWindowListener(m_xResizeListener);
    m_xResizeListener.clear();
    if (m_attached)
        HostService::instance().release(m_pluginId); // parks the browser before the window dies
    m_attached = false;
    Reference<css::lang::XComponent> xComp(m_xWindow, css::uno::UNO_QUERY);
    m_xWindow.clear();
    if (xComp.is())
        xComp->dispose();
}
}
