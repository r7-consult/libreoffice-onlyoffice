#include "document_bridge.hxx"

#include "host_service.hxx"
#include "json_mini.hxx"
#include "log.hxx"

#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XDesktop.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <rtl/ustring.hxx>

#include <stdexcept>

namespace pluginhost
{
using css::uno::Reference;
using css::uno::UNO_QUERY;

namespace
{
// The Writer view cursor of the active document. Its selection is the user's selection.
Reference<css::text::XTextViewCursor> activeWriterCursor()
{
    const auto& ctx = HostService::instance().context();
    if (!ctx.is())
        throw std::runtime_error("component context is not set");

    Reference<css::lang::XMultiComponentFactory> smgr = ctx->getServiceManager();
    Reference<css::frame::XDesktop> desktop(
        smgr->createInstanceWithContext("com.sun.star.frame.Desktop", ctx), UNO_QUERY);
    if (!desktop.is())
        throw std::runtime_error("no desktop");

    Reference<css::frame::XModel> model(desktop->getCurrentComponent(), UNO_QUERY);
    if (!model.is())
        throw std::runtime_error("no active document");
    Reference<css::frame::XController> controller = model->getCurrentController();
    Reference<css::text::XTextViewCursorSupplier> supplier(controller, UNO_QUERY);
    if (!supplier.is())
        throw std::runtime_error("active document is not a text document");
    Reference<css::text::XTextViewCursor> cursor(supplier->getViewCursor(), UNO_QUERY);
    if (!cursor.is())
        throw std::runtime_error("no view cursor");
    return cursor;
}

std::string argString(const JsonValue& args, size_t index)
{
    if (!args.isArray() || index >= args.array.size() || !args.array[index].isString())
        throw std::runtime_error("expected string argument");
    return args.array[index].str;
}
}

std::string callDocumentMethod(const std::string& name, const std::string& argsJson)
{
    JsonValue args;
    if (!parseJson(argsJson, args))
        throw std::runtime_error("arguments are not valid JSON");

    if (name == "GetSelectedText")
    {
        Reference<css::text::XTextViewCursor> cursor = activeWriterCursor();
        return "\"" + jsonEscape(cursor->getString().toUtf8().getStr()) + "\"";
    }
    if (name == "PasteText")
    {
        const std::string text = argString(args, 0);
        Reference<css::text::XTextViewCursor> cursor = activeWriterCursor();
        // setString replaces the selection, or inserts at the caret when nothing is selected.
        cursor->setString(rtl::OUString::fromUtf8(text));
        return "true";
    }
    if (name == "LogTest")
    {
        // Test-only: the hello fixture reports its check results through the log file.
        logEvent("plugin-test", argString(args, 0).c_str(), argString(args, 1));
        return "true";
    }
    throw std::runtime_error("method not supported: " + name);
}
}
