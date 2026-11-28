#include "context_menu.hxx"

#include "bridge_native.hxx" // utf8ToOUString (SDK-safe std::string -> OUString)
#include "log.hxx"
#include "host_service.hxx"

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexContainer.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/ui/ContextMenuExecuteEvent.hpp>
#include <com/sun/star/ui/ContextMenuInterceptorAction.hpp>
#include <com/sun/star/ui/XContextMenuInterception.hpp>
#include <com/sun/star/ui/XContextMenuInterceptor.hpp>
#include <cppuhelper/implbase.hxx>

#include <chrono>
#include <map>
#include <mutex>
#include <set>

namespace pluginhost
{
using css::uno::Any;
using css::uno::Reference;
using css::uno::UNO_QUERY;

namespace
{
std::map<std::string, std::vector<std::pair<std::string, std::string>>>& itemCache()
{
    static std::map<std::string, std::vector<std::pair<std::string, std::string>>> m;
    return m;
}

ContextMenuTiming& timing()
{
    static ContextMenuTiming t;
    return t;
}

double nowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

class Interceptor : public cppu::WeakImplHelper<css::ui::XContextMenuInterceptor>
{
public:
    css::ui::ContextMenuInterceptorAction SAL_CALL notifyContextMenuExecute(
        const css::ui::ContextMenuExecuteEvent& rEvent) override
    {
        timing().lastRequestMs = nowMs();
        css::ui::ContextMenuInterceptorAction action = css::ui::ContextMenuInterceptorAction_IGNORED;
        try
        {
            // ActionTrigger is NOT a global service: only the menu's own container creates it
            // (framework/source/fwe/classes/rootactiontriggercontainer.cxx).
            Reference<css::lang::XMultiServiceFactory> factory(rEvent.ActionTriggerContainer, UNO_QUERY);
            Reference<css::container::XIndexContainer> container(rEvent.ActionTriggerContainer, UNO_QUERY);
            bool hasItems = false;
            for (const auto& kv : itemCache())
                hasItems = hasItems || !kv.second.empty();
            if (factory.is() && container.is() && hasItems)
            {
                sal_Int32 pos = container->getCount();
                Reference<css::uno::XInterface> separator(factory->createInstance("com.sun.star.ui.ActionTriggerSeparator"));
                if (separator.is())
                    container->insertByIndex(pos++, Any(separator));
                for (const auto& kv : itemCache())
                {
                    for (const auto& item : kv.second)
                    {
                        Reference<css::beans::XPropertySet> trigger(
                            factory->createInstance("com.sun.star.ui.ActionTrigger"), UNO_QUERY);
                        if (!trigger.is())
                            continue;
                        trigger->setPropertyValue("Text", Any(utf8ToOUString(item.second)));
                        trigger->setPropertyValue("CommandURL",
                            Any(utf8ToOUString("vnd.pluginhost:" + kv.first + ":" + item.first)));
                        container->insertByIndex(pos++, Any(trigger));
                        action = css::ui::ContextMenuInterceptorAction_EXECUTE_MODIFIED;
                    }
                }
            }
        }
        catch (...)
        {
            logEvent("ctxmenu", "error");
        }
        timing().lastReturnMs = nowMs();
        logEvent("ctxmenu", "returned", std::to_string(timing().lastReturnMs - timing().lastRequestMs));
        return action;
    }
};
}

void setContextMenuItems(const std::string& pluginId, std::vector<std::pair<std::string, std::string>> items)
{
    itemCache()[pluginId] = std::move(items);
}

void attachContextMenu(const Reference<css::frame::XController>& controller)
{
    Reference<css::ui::XContextMenuInterception> interception(controller, UNO_QUERY);
    if (!interception.is())
        return;
    // A panel is re-created on every deck switch; register once per controller or entries multiply.
    // (Address of the controller's XInterface; a recycled address is a draft-level risk.)
    static std::set<css::uno::XInterface*> attached;
    css::uno::XInterface* key = Reference<css::uno::XInterface>(controller, UNO_QUERY).get();
    if (!attached.insert(key).second)
        return;
    interception->registerContextMenuInterceptor(new Interceptor());
    logEvent("ctxmenu", "attached");
}

ContextMenuTiming lastContextMenuTiming()
{
    return timing();
}
}
