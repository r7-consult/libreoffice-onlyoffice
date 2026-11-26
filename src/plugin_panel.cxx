#include "plugin_panel.hxx"

#include "log.hxx"

#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/WindowClass.hpp>
#include <com/sun/star/awt/WindowDescriptor.hpp>
#include <com/sun/star/awt/WindowAttribute.hpp>
#include <com/sun/star/awt/XToolkit.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/ui/UIElementType.hpp>
#include <com/sun/star/awt/PosSize.hpp>
#include <com/sun/star/awt/XSystemDependentWindowPeer.hpp>
#include <com/sun/star/lang/SystemDependent.hpp>
#include <rtl/process.h>

#include <cstdlib>
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
}

namespace
{
#ifdef _WIN32
// HWND of a systemchildwindow, obtained through XSystemDependentWindowPeer (Windows: HWND bytes).
void* nativeWindowHandle(const Reference<XWindow>& xWindow)
{
    Reference<css::awt::XSystemDependentWindowPeer> xDep(xWindow, css::uno::UNO_QUERY);
    if (!xDep.is())
        return nullptr;
    css::uno::Sequence<sal_Int8> aProcessId(16);
    rtl_getGlobalProcessId(reinterpret_cast<sal_uInt8*>(aProcessId.getArray()));
    const css::uno::Sequence<sal_Int8> aHandle =
        xDep->getWindowHandle(aProcessId, css::lang::SystemDependent::SYSTEM_WIN32);
    if (aHandle.getLength() < static_cast<sal_Int32>(sizeof(HWND)))
        return nullptr;
    HWND hwnd = nullptr;
    std::memcpy(&hwnd, aHandle.getConstArray(), sizeof(HWND));
    return hwnd;
}

// Directory of pluginhost.dll (the unpacked extension root).
std::string moduleDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleDirectory), &module);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring w(path);
    const size_t slash = w.find_last_of(L'\\');
    w = (slash == std::wstring::npos) ? std::wstring() : w.substr(0, slash + 1);
    std::string utf8(w.size() * 3, '\0');
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &utf8[0],
                                      static_cast<int>(utf8.size()), nullptr, nullptr);
    utf8.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return utf8;
}

// Per-plugin WebView2 user data folder, under %LOCALAPPDATA%\PluginHost.
std::string pluginDataDirectory()
{
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) : std::string(".");
    dir += "\\PluginHost\\webview2-probe";
    return dir;
}

// Stage 2 probe page shipped in the extension's content folder.
std::string probePageUrl()
{
    std::string dir = moduleDirectory();
    for (char& c : dir)
        if (c == '\\')
            c = '/';
    return "file:///" + dir + "content/probe.html";
}
#endif
}

PluginPanel::PluginPanel(const Reference<XComponentContext>& ctx,
                         const Reference<XWindow>& xParentWindow,
                         const OUString& resourceURL)
    : cppu::BaseMutex()
    , WeakComponentImplHelper(m_aMutex)
    , m_xContext(ctx)
    , m_sResourceURL(resourceURL)
{
    m_xWindow = createChildWindow(ctx, xParentWindow);
    if (m_xWindow.is())
    {
        const css::awt::Rectangle aSize = xParentWindow->getPosSize();
        m_xWindow->setPosSize(0, 0, aSize.Width, aSize.Height, css::awt::PosSize::POSSIZE);
        m_xWindow->setVisible(true);
        logEvent("panel", "created", m_sResourceURL.toUtf8().getStr());
        attachWebHost(m_xWindow, aSize.Width, aSize.Height);
    }
    else
    {
        logEvent("panel", "error", "systemchildwindow was not created");
    }
}

void PluginPanel::attachWebHost(const Reference<XWindow>& xChild, sal_Int32 width, sal_Int32 height)
{
#ifdef _WIN32
    void* hwnd = nativeWindowHandle(xChild);
    if (!hwnd)
    {
        logEvent("panel", "error", "no native window handle");
        return;
    }
    const std::string dataDir = pluginDataDirectory();
    m_webHost.reset(createWebHost(hwnd, dataDir));
    if (!m_webHost)
        return;
    m_webHost->navigate(probePageUrl());
    m_webHost->setBounds(0, 0, width, height);
#else
    (void)xChild;
    (void)width;
    (void)height;
    logEvent("panel", "skipped", "web host is implemented for Windows only (stage 2)");
#endif
}

css::uno::Reference<css::frame::XFrame> SAL_CALL PluginPanel::getFrame()
{
    return {};
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
    // Fill the whole deck height; the sidebar decides the final size.
    return LayoutSize(-1, -1, -1);
}

sal_Int32 SAL_CALL PluginPanel::getMinimalWidth()
{
    return 200;
}

void SAL_CALL PluginPanel::disposing()
{
    osl::MutexGuard guard(m_aMutex);
    logEvent("panel", "disposed", m_sResourceURL.toUtf8().getStr());
    m_webHost.reset();
    Reference<css::lang::XComponent> xComp(m_xWindow, css::uno::UNO_QUERY);
    m_xWindow.clear();
    if (xComp.is())
        xComp->dispose();
}
}
