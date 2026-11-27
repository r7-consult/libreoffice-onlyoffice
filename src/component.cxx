// UNO component entry points for pluginhost.dll.
// Structure follows odk/examples/cpp/custompanel/ctp_services.cxx.

#include "menu_dispatch.hxx"
#include "panel_factory.hxx"

#include <cppuhelper/implementationentry.hxx>
#include <uno/lbnames.h>

namespace
{
const char* const kFactoryImpl = "org.example.pluginhost.PanelFactory";

cppu::ImplementationEntry s_aServiceEntries[] =
{
    {
        pluginhost::PanelFactory::Create,
        pluginhost::PanelFactory::getImplementationName_static,
        pluginhost::PanelFactory::getSupportedServiceNames_static,
        ::cppu::createSingleComponentFactory,
        nullptr,
        0
    },
    {
        pluginhost::MenuDispatch::Create,
        pluginhost::MenuDispatch::getImplementationName_static,
        pluginhost::MenuDispatch::getSupportedServiceNames_static,
        ::cppu::createSingleComponentFactory,
        nullptr,
        0
    },
    { nullptr, nullptr, nullptr, nullptr, nullptr, 0 }
};
}

extern "C"
{
SAL_DLLPUBLIC_EXPORT void* SAL_CALL component_getFactory(const char* pImplName, void* pServiceManager, void* pRegistryKey)
{
    (void)kFactoryImpl;
    return ::cppu::component_getFactoryHelper(pImplName, pServiceManager, pRegistryKey, s_aServiceEntries);
}

SAL_DLLPUBLIC_EXPORT void SAL_CALL component_getImplementationEnvironment(char const** ppEnvTypeName, uno_Environment**)
{
    *ppEnvTypeName = CPPU_CURRENT_LANGUAGE_BINDING_NAME;
}
}
