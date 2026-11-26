// Non-Windows placeholder (stage 10). Returns no browser, so the panel logs
// "webhost|unavailable" and the plugin is not shown.
//
// Planned implementations (not written, see docs/TEST_MATRIX_LINUX_MACOS.md):
//   Linux (X11 / XWayland): CEF in windowed mode attached to the XID of systemchildwindow.
//   macOS: WKWebView attached to the NSView* of systemchildwindow.

#ifndef _WIN32

#include "webhost.hxx"

#include "log.hxx"

namespace pluginhost
{
WebHost* createWebHost(void* nativeParentHandle, const std::string& userDataDir)
{
    (void)nativeParentHandle;
    (void)userDataDir;
    logEvent("webhost", "unavailable", "no browser implementation on this platform yet");
    return nullptr;
}
}

#endif // !_WIN32
