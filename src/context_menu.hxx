#pragma once

// Context menu items contributed by plugins (stage 6, draft).
//
// The page sends {type:"contextMenu", items:[{id,text}]} to say which items it adds.
// The interceptor reads only this cache, so it never waits for the page
// (synchronous interceptor; see plan risk T-9).

#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/uno/Reference.hxx>

#include <string>
#include <utility>
#include <vector>

namespace pluginhost
{
// Replaces the item list for pluginId. Called from ProtocolAdapter.
void setContextMenuItems(const std::string& pluginId, std::vector<std::pair<std::string, std::string>> items);

// Registers the interceptor on a controller. Safe to call more than once per controller.
void attachContextMenu(const css::uno::Reference<css::frame::XController>& controller);

// Measured for the stage 6 gate: last request and return times (milliseconds).
struct ContextMenuTiming
{
    double lastRequestMs = 0;
    double lastReturnMs = 0;
};
ContextMenuTiming lastContextMenuTiming();

// Click handling is in src/menu_dispatch.cxx. Items use CommandURL "vnd.pluginhost:<pluginId>:<itemId>".
}
