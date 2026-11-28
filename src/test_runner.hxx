#pragma once

// Test-only UNO service org.example.pluginhost.TestRunner (PLAN_FULL_API section 5, F5). Used by tests/run_member.py.
//
// It implements XInvocation, so pyuno calls it as runner.run(source, scopeJson). run() evaluates source in the
// engine under the pluginId "__test__" and returns the result as JSON text. There is no permission check for
// this id, so the service exists only when the environment variable PLUGINHOST_TEST_SERVICE is "1". Otherwise
// createInstance returns nothing. Built only with PH_TEST_HOOKS (see docs/TEST_RUNNER.md).

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/script/XInvocation.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase.hxx>
#include <rtl/ustring.hxx>

namespace pluginhost
{
// Outside LibreOffice's own build (LIBO_INTERNAL_ONLY) there is no global OUString: rtl/ustring.hxx declares only
// rtl::OUString. This file and test_runner.cxx use the short name.
using ::rtl::OUString;

class TestRunner : public cppu::WeakImplHelper<css::script::XInvocation, css::lang::XServiceInfo>
{
public:
    explicit TestRunner(const css::uno::Reference<css::uno::XComponentContext>& ctx);

    // XInvocation. Only "run" (two string arguments: source, scopeJson) is a method.
    css::uno::Reference<css::beans::XIntrospectionAccess> SAL_CALL getIntrospection() override;
    css::uno::Any SAL_CALL invoke(const OUString& aFunctionName, const css::uno::Sequence<css::uno::Any>& aParams,
                                  css::uno::Sequence<sal_Int16>& aOutParamIndex,
                                  css::uno::Sequence<css::uno::Any>& aOutParam) override;
    void SAL_CALL setValue(const OUString& aPropertyName, const css::uno::Any& aValue) override;
    css::uno::Any SAL_CALL getValue(const OUString& aPropertyName) override;
    sal_Bool SAL_CALL hasMethod(const OUString& aName) override;
    sal_Bool SAL_CALL hasProperty(const OUString& aName) override;

    // XServiceInfo
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& rServiceName) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    static OUString getImplementationName_static();
    static css::uno::Sequence<OUString> getSupportedServiceNames_static();
    // Returns an empty reference unless PLUGINHOST_TEST_SERVICE is "1".
    static css::uno::Reference<css::uno::XInterface> SAL_CALL Create(
        const css::uno::Reference<css::uno::XComponentContext>& ctx);

private:
    css::uno::Reference<css::uno::XComponentContext> m_xContext;
};
}
