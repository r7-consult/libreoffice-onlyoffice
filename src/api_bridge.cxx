// Native side of the Api shim (stage 5). Each name below is one native operation;
// api_shim.js maps the ten public Api methods onto these operations.
//   calc.activeSheetName            () -> "Sheet1"
//   calc.getRange                   (sheet, addr) -> [[...]]
//   calc.setRange                   (sheet, addr, rows) -> true
//   writer.paragraphCount           () -> n
//   writer.paragraphText            (index) -> "..."
//   writer.appendText               (text) -> true
// Errors are thrown as std::runtime_error and surface as JS TypeError.
//
// Draft: not compiled. Verify the UNO calls against the LibreOffice SDK headers.

#include "json_mini.hxx"
#include "host_service.hxx"

#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XDesktop.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/sheet/XCalculatable.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/table/XCellRangeData.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/document/XUndoManager.hpp>
#include <com/sun/star/document/XUndoManagerSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/util/XModifiable.hpp>
#include <rtl/ustring.hxx>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pluginhost
{
using css::uno::Any;
using css::uno::Reference;
using css::uno::Sequence;
using css::uno::UNO_QUERY;
using css::uno::UNO_QUERY_THROW;
using css::uno::XInterface;

namespace
{
Reference<css::frame::XModel> activeModel()
{
    const auto& ctx = HostService::instance().context();
    if (!ctx.is())
        throw std::runtime_error("component context is not set");
    Reference<css::frame::XDesktop> desktop(
        ctx->getServiceManager()->createInstanceWithContext("com.sun.star.frame.Desktop", ctx), UNO_QUERY_THROW);
    Reference<css::frame::XModel> model(desktop->getCurrentComponent(), UNO_QUERY);
    if (!model.is())
        throw std::runtime_error("no active document");
    return model;
}

Reference<css::sheet::XSpreadsheet> sheetByName(const std::string& name)
{
    Reference<css::sheet::XSpreadsheetDocument> doc(activeModel(), UNO_QUERY_THROW);
    Reference<css::sheet::XSpreadsheets> sheets = doc->getSheets();
    const rtl::OUString ou = rtl::OUString::fromUtf8(name);
    if (!sheets->hasByName(ou))
        throw std::runtime_error("no sheet named " + name);
    return Reference<css::sheet::XSpreadsheet>(sheets->getByName(ou), UNO_QUERY_THROW);
}

Reference<css::table::XCellRange> rangeOf(const std::string& sheetName, const std::string& addr)
{
    Reference<css::sheet::XSpreadsheet> sheet = sheetByName(sheetName);
    return Reference<css::table::XCellRange>(sheet->getCellRangeByName(rtl::OUString::fromUtf8(addr)), UNO_QUERY_THROW);
}

std::string quoteJson(const std::string& s)
{
    return "\"" + jsonEscape(s) + "\"";
}

// JSON scalar -> UNO Any for XCellRangeData.
Any anyFromJson(const JsonValue& v)
{
    switch (v.kind)
    {
    case JsonValue::Kind::Number: return Any(v.number);
    case JsonValue::Kind::Bool: return Any(v.boolean);
    case JsonValue::Kind::String: return Any(rtl::OUString::fromUtf8(v.str));
    default: return Any();
    }
}

std::string jsonFromAny(const Any& a)
{
    double d = 0.0;
    if (a >>= d)
    {
        std::ostringstream s;
        s.precision(17);
        s << d;
        return s.str();
    }
    rtl::OUString ou;
    if (a >>= ou)
        return quoteJson(ou.toUtf8().getStr());
    return "null";
}

Reference<css::text::XText> writerBody()
{
    Reference<css::text::XTextDocument> doc(activeModel(), UNO_QUERY_THROW);
    return doc->getText();
}

// Paragraphs of the body, as text ranges (XEnumerationAccess yields paragraphs).
std::vector<Reference<css::text::XTextRange>> writerParagraphs()
{
    std::vector<Reference<css::text::XTextRange>> out;
    Reference<css::container::XEnumerationAccess> access(writerBody(), UNO_QUERY_THROW);
    Reference<css::container::XEnumeration> en = access->createEnumeration();
    while (en->hasMoreElements())
        out.push_back(Reference<css::text::XTextRange>(en->nextElement(), UNO_QUERY));
    return out;
}
}

std::string nativeCall(const std::string& name, const std::string& argsJson)
{
    JsonValue args;
    if (!parseJson(argsJson, args) || !args.isArray())
        throw std::runtime_error("arguments must be a JSON array");
    auto str = [&](size_t i) -> std::string {
        if (i >= args.array.size() || !args.array[i].isString())
            throw std::runtime_error("argument " + std::to_string(i) + " must be a string");
        return args.array[i].str;
    };

    if (name == "calc.activeSheetName")
    {
        Reference<css::frame::XModel> model = activeModel();
        Reference<css::sheet::XSpreadsheetView> view(model->getCurrentController(), UNO_QUERY_THROW);
        Reference<css::sheet::XSpreadsheet> sheet(view->getActiveSheet(), UNO_QUERY_THROW);
        Reference<css::container::XNamed> named(sheet, UNO_QUERY_THROW);
        return quoteJson(named->getName().toUtf8().getStr());
    }

    if (name == "calc.getRange")
    {
        Reference<css::table::XCellRangeData> data(rangeOf(str(0), str(1)), UNO_QUERY_THROW);
        const Sequence<Sequence<Any>> rows = data->getDataArray();
        std::string out = "[";
        for (sal_Int32 r = 0; r < rows.getLength(); ++r)
        {
            if (r)
                out += ",";
            out += "[";
            for (sal_Int32 c = 0; c < rows[r].getLength(); ++c)
            {
                if (c)
                    out += ",";
                out += jsonFromAny(rows[r][c]);
            }
            out += "]";
        }
        return out + "]";
    }

    if (name == "calc.setRange")
    {
        if (args.array.size() < 3 || !args.array[2].isArray())
            throw std::runtime_error("setRange needs (sheet, addr, rows)");
        const JsonValue& jrows = args.array[2];
        Sequence<Sequence<Any>> rows(static_cast<sal_Int32>(jrows.array.size()));
        for (size_t r = 0; r < jrows.array.size(); ++r)
        {
            const JsonValue& jrow = jrows.array[r];
            if (!jrow.isArray())
                throw std::runtime_error("rows must be arrays");
            Sequence<Any> row(static_cast<sal_Int32>(jrow.array.size()));
            for (size_t c = 0; c < jrow.array.size(); ++c)
                row[static_cast<sal_Int32>(c)] = anyFromJson(jrow.array[c]);
            rows[static_cast<sal_Int32>(r)] = row;
        }
        Reference<css::table::XCellRangeData> data(rangeOf(str(0), str(1)), UNO_QUERY_THROW);
        data->setDataArray(rows);
        return "true";
    }

    if (name == "writer.paragraphCount")
        return std::to_string(writerParagraphs().size());

    if (name == "writer.paragraphText")
    {
        if (args.array.empty() || args.array[0].kind != JsonValue::Kind::Number)
            throw std::runtime_error("paragraphText needs an index");
        const auto paras = writerParagraphs();
        const size_t i = static_cast<size_t>(args.array[0].number);
        if (i >= paras.size())
            throw std::runtime_error("paragraph index out of range");
        return quoteJson(paras[i]->getString().toUtf8().getStr());
    }

    if (name == "writer.appendText")
    {
        Reference<css::text::XText> body = writerBody();
        Reference<css::text::XTextCursor> cursor = body->createTextCursor();
        cursor->gotoEnd(false);
        body->insertString(cursor, rtl::OUString::fromUtf8(str(0)), false);
        return "true";
    }

    throw std::runtime_error("unknown native operation: " + name);
}

// Undo and locking helpers used by CommandEngine.
void undoLastAction()
{
    try
    {
        Reference<css::document::XUndoManagerSupplier> supplier(activeModel(), UNO_QUERY);
        if (supplier.is())
            supplier->getUndoManager()->undo();
    }
    catch (...)
    {
        // Nothing to undo, or no document: the command already failed.
    }
}

int undoActionCount()
{
    try
    {
        Reference<css::document::XUndoManagerSupplier> supplier(activeModel(), UNO_QUERY);
        if (supplier.is())
            return supplier->getUndoManager()->getAllUndoActionTitles().getLength();
    }
    catch (...)
    {
    }
    return 0;
}

void lockActiveDocument(bool lock)
{
    try
    {
        Reference<css::frame::XModel> model = activeModel();
        if (lock)
            model->lockControllers();
        else
            model->unlockControllers();
    }
    catch (...)
    {
    }
}

void recalculateActiveDocument()
{
    try
    {
        Reference<css::sheet::XCalculatable> calc(activeModel(), UNO_QUERY);
        if (calc.is())
            calc->calculateAll();
    }
    catch (...)
    {
    }
}
}
