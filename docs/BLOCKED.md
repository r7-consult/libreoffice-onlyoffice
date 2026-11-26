# Blockers

## B1 Administrator rights
Visual Studio Build Tools must be installed from an elevated prompt. The development session runs without administrator rights, and `vswhere.exe` is absent.
**Needed from a person:** run the Build Tools installer as administrator with these components: `Microsoft.VisualStudio.Workload.VCTools`, `Microsoft.VisualStudio.Component.Windows11SDK.22621`, `Microsoft.VisualStudio.Component.VC.ATL`, `Microsoft.VisualStudio.Component.VC.CLI.Support`.

## B2 LibreOffice binaries and SDK not downloadable
`https://download.documentfoundation.org/` and `https://dev-www.libreoffice.org/` returned no response (HTTP 000) from the development machine. Without them:
- the extension cannot be compiled (no `types.rdb`, no `cppumaker`, no UNO SDK headers);
- no stage that runs LibreOffice can be executed.
**Needed from a person:** place the LibreOffice release and its SDK for two branches in `C:\LO-A` and `C:\LO-B` (and `C:\LO-A-SDK`, `C:\LO-B-SDK`), then fill `config\versions.json`.

## B3 Repository publishing
Only the documents branch `lo-plugins-docs` was pushed to `r7-consult/libreoffice-onlyoffice`. This plugin-host repository is local only. Pushing it needs a decision from the owner about where it lives.

## Not blocked, but unverified
- `Sidebar.xcu` / `Factories.xcu` structure was modelled on `officecfg/.../Sidebar.xcu` and `odk/examples/cpp/custompanel`, not tested in a running LibreOffice.
- `cppumaker` argument layout (`ure-link-lib`, `types\offapi.rdb`) is assumed from the SDK's usual layout and must be checked once the SDK is present.
- `css::awt::Toolkit::create` usage follows the custompanel example; confirm against the SDK headers.
