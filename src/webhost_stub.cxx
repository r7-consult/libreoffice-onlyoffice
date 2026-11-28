// macOS placeholder. Returns no browser, so the panel logs "webhost|unavailable"
// and the plugin is not shown. Not built on Linux: Linux uses WebKitGTK 4.1 in
// src/webhost_linux_webkitgtk.cxx (compiled on non-Windows; see CMakeLists.txt).
//
// Planned macOS implementation (not written, see docs/TEST_MATRIX_LINUX_MACOS.md):
//   macOS: WKWebView attached to the NSView* of systemchildwindow.
// CEF remains the fallback for Linux (docs/DECISION_LOG.md item 14), not written.

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
