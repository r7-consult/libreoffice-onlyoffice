#include "test_runner.hxx"

#include "command_engine.hxx"

#include <com/sun/star/beans/UnknownPropertyException.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <cppuhelper/supportsservice.hxx>

#include <cstdlib>
#include <cstring>

namespace pluginhost
{
using css::uno::Any;
using css::uno::Reference;
using css::uno::Sequence;
using css::uno::XComponentContext;
using css::uno::XInterface;

namespace
{
const char kServiceName[] = "org.example.pluginhost.TestRunner";
const char kPluginId[] = "__test__";

bool testServiceEnabled()
{
    const char* v = std::getenv("PLUGINHOST_TEST_SERVICE");
    return v && std::strcmp(v, "1") == 0;
}
}

TestRunner::TestRunner(const Reference<XComponentContext>& ctx)
    : m_xContext(ctx)
{
}

Reference<css::beans::XIntrospectionAccess> SAL_CALL TestRunner::getIntrospection()
{
    // No introspection: callers use hasMethod and invoke, which is how pyuno calls XInvocation objects.
    return Reference<css::beans::XIntrospectionAccess>();
}

Any SAL_CALL TestRunner::invoke(const OUString& aFunctionName, const Sequence<Any>& aParams,
                                Sequence<sal_Int16>& aOutParamIndex, Sequence<Any>& aOutParam)
{
    const Reference<XInterface> self(static_cast<cppu::OWeakObject*>(this));
    aOutParamIndex = Sequence<sal_Int16>();
    aOutParam = Sequence<Any>();
    if (aFunctionName != "run")
        throw css::lang::IllegalArgumentException(OUString("TestRunner: unknown method ") + aFunctionName, self, 0);
    if (aParams.getLength() != 2)
        throw css::lang::IllegalArgumentException(OUString("TestRunner.run takes (source, scopeJson)"), self, 0);

    OUString source;
    OUString scope;
    if (!(aParams[0] >>= source) || !(aParams[1] >>= scope))
        throw css::lang::IllegalArgumentException(OUString("TestRunner.run arguments must be strings"), self, 0);

    const CommandOutcome out = CommandEngine::instance().run(
        kPluginId, rtl::OUStringToOString(source, RTL_TEXTENCODING_UTF8).getStr(),
        rtl::OUStringToOString(scope, RTL_TEXTENCODING_UTF8).getStr());
    if (!out.ok)
        throw css::lang::IllegalArgumentException(OUString("TestRunner.run: ") + OUString::fromUtf8(out.error), self, 0);
    return Any(OUString::fromUtf8(out.resultJson));
}

void SAL_CALL TestRunner::setValue(const OUString& aPropertyName, const Any&)
{
    const Reference<XInterface> self(static_cast<cppu::OWeakObject*>(this));
    throw css::beans::UnknownPropertyException(OUString("TestRunner has no property ") + aPropertyName, self);
}

Any SAL_CALL TestRunner::getValue(const OUString& aPropertyName)
{
    const Reference<XInterface> self(static_cast<cppu::OWeakObject*>(this));
    throw css::beans::UnknownPropertyException(OUString("TestRunner has no property ") + aPropertyName, self);
}

sal_Bool SAL_CALL TestRunner::hasMethod(const OUString& aName)
{
    return aName == "run";
}

sal_Bool SAL_CALL TestRunner::hasProperty(const OUString&)
{
    return false;
}

sal_Bool SAL_CALL TestRunner::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

OUString SAL_CALL TestRunner::getImplementationName()
{
    return getImplementationName_static();
}

Sequence<OUString> SAL_CALL TestRunner::getSupportedServiceNames()
{
    return getSupportedServiceNames_static();
}

OUString TestRunner::getImplementationName_static()
{
    return OUString(kServiceName);
}

Sequence<OUString> TestRunner::getSupportedServiceNames_static()
{
    return Sequence<OUString>{ OUString(kServiceName) };
}

Reference<XInterface> SAL_CALL TestRunner::Create(const Reference<XComponentContext>& ctx)
{
    if (!testServiceEnabled())
        return Reference<XInterface>();
    return static_cast<cppu::OWeakObject*>(new TestRunner(ctx));
}
}
