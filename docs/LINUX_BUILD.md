# Linux build: first compile and first run

Status: **NOT RUN YET.** Every script, the container file and the workflow were written on Windows without a Linux
host or a compiler. Nothing below is known to work. Each step is marked "not run yet" until someone runs it and
writes the result in `docs/RESULTS.md`.

Scope: Linux only (decision 8 in `docs/DECISION_LOG.md`), x86_64 first. aarch64 is accepted by the pack script but
nobody has thought about it further.

## 1. Read this before you start

- **LibreOffice version.** `extension/description.xml` requires LibreOffice >= 7.6. Ubuntu 22.04 (jammy) ships 7.3.x.
  On jammy the library can compile, but `unopkg add` will refuse the package. Use Ubuntu 24.04, a LibreOffice from
  the Document Foundation `.deb` packages, or another office >= 7.6 for the install and run steps. (Jammy and
  "7.3.x" are from memory, check with `apt-cache policy libreoffice-dev`.)
- **The web view may not appear at all.** `src/webhost_linux_webkitgtk.cxx` puts WebKitGTK in a `GtkPlug` on the X11
  window the office gives as the panel parent (decision 14). Under the default **gtk3** VCL plugin that window id
  belongs to the office's own GDK window, and `GtkPlug` cannot embed into it. Native Wayland gives no X11 window.
  Expected to work only with the **Qt/KDE** VCL plugin (`kf5`/`qt5`) on X11 or XWayland. In the other cases the host
  logs `webhost|error|...` and the panel stays empty. This is the first thing the Linux run must settle
  (`docs/REVIEW_2026-11-28.md` item 2.1; `docs/TEST_MATRIX_LINUX_MACOS.md` rows 1 to 4).
- **Plan for compile errors.** About 3500 lines of C++ were never compiled. Fix compile errors only, one line each in
  `docs/RESULTS.md`, no features.

## 2. Prerequisites

| What | Package (Debian/Ubuntu) | Minimum |
|---|---|---|
| C++17 compiler, make | `build-essential` | GCC 9 or newer (jammy has 11); not checked |
| CMake | `cmake` | 3.20 (`src/CMakeLists.txt`); jammy has 3.22 |
| Ninja (optional, else Make) | `ninja-build` | any |
| pkg-config | `pkg-config` | any |
| LibreOffice SDK and headers | `libreoffice-dev`, `libreoffice-dev-common` | same version as the office you will run |
| Office program folder (types, unopkg) | `libreoffice-core`, `libreoffice-common` | same |
| GTK 3 | `libgtk-3-dev` | pkg-config module `gtk+-3.0` |
| WebKitGTK, libsoup 3 flavour | `libwebkit2gtk-4.1-dev` | pkg-config module `webkit2gtk-4.1` |
| X11 headers (`gdk/gdkx.h`, `gtk/gtkx.h`) | `libx11-dev` | any |
| Tools | `python3`, `zip`, `unzip`, `curl`, `git` | any |
| Node.js | `nodejs` | the tests use plain CommonJS; Node 16 or newer is a safe guess, jammy's Node 12 is untested |

```
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libreoffice-dev libreoffice-dev-common libreoffice-core libreoffice-common \
    libgtk-3-dev libwebkit2gtk-4.1-dev libx11-dev python3 nodejs zip unzip curl git
```

To run the extension you also need an office with Writer and Calc and, for the embedding question above, a Qt VCL
plugin package. Package names differ by distribution; not checked. A container with the build packages is in
`docker/Dockerfile.linux` (not built yet).

The defaults in the scripts are the Debian/Ubuntu layout: `LO_SDK=/usr/lib/libreoffice/sdk`,
`LO_PROGRAM=/usr/lib/libreoffice/program`. Export both if your office lives elsewhere.

## 3. Command sequence (all steps: not run yet)

Run from the repository root (`plugin-host/`). Use `bash scripts/...` so that file modes do not matter.

```
bash scripts/fetch_quickjs.sh        # third_party/quickjs-ng (checks a SHA-256, see the script header)
bash scripts/gen_uno_headers.sh      # build/gen  (cppumaker, all types, takes a few minutes)
bash scripts/build.sh                # Release build; PH_TEST_HOOKS=ON for a test build (default OFF)
bash scripts/pack.sh                 # dist/pluginhost-linux_x86_64.oxt
bash scripts/install.sh -p "$HOME/pluginhost-profile"
```

Settings are environment variables: `LO_SDK`, `LO_PROGRAM`, `PH_GEN_DIR`, `PH_QUICKJS_DIR`, `PH_BUILD_DIR`,
`PH_TEST_HOOKS`, `PH_JOBS` (see each script header). Every script prints each command before it runs it and stops at
the first error.

## 4. Where the outputs are

| Output | Path |
|---|---|
| Generated UNO headers | `build/gen/com/sun/star/...` |
| Shared library | `build/bin/pluginhost.so` (`PREFIX ""` and `LIBRARY_OUTPUT_DIRECTORY` in `src/CMakeLists.txt`). `build.sh` prints the path it found. |
| Staging folder for the package | `build/oxt/` (and the file list in `build/oxt-files.txt`) |
| Package | `dist/pluginhost-linux_x86_64.oxt` |

`pack.sh` writes the Linux versions of `META-INF/manifest.xml`, `pluginhost.components` and `description.xml` into
the staging folder only. The tracked files stay the Windows ones. The library file name goes into the `uri` of the
components file exactly as built: LibreOffice passes that `uri` to `osl::Module` unchanged and adds no `lib` prefix
or `.so` suffix (`cppuhelper/source/shlib.cxx`).

## 5. Install into a private test profile and start the office (not run yet)

```
bash scripts/install.sh -p "$HOME/pluginhost-profile"       # unopkg add --force, no root, no --shared
mkdir -p "$HOME/.local/share/PluginHost/plugins"
cp -r tests/plugins/hello "$HOME/.local/share/PluginHost/plugins/"
export PLUGINHOST_LOG="$HOME/pluginhost.log"
export PLUGINHOST_PLUGIN=hello
soffice -env:UserInstallation=file://$HOME/pluginhost-profile --norestore --writer
```

- Close every office that uses the same profile before `install.sh` or `uninstall.sh`.
- `bash scripts/uninstall.sh -p "$HOME/pluginhost-profile"` removes the extension (identifier `org.example.pluginhost`).
- Plugin folder default: `$XDG_DATA_HOME/PluginHost/plugins`, else `~/.local/share/PluginHost/plugins`
  (`src/paths.cxx`). `PLUGINHOST_PLUGINS_DIR` overrides it.
- Open the sidebar deck "Plugin Host". Look in `$PLUGINHOST_LOG` for lines in the form `area|event|detail`:
  `panel|created`, `webhost|created`, or `webhost|error|...`.
- To try the Qt plugin: `SAL_USE_VCLPLUGIN=kf5 soffice ...` (or `qt5`), under an X11 or XWayland session
  (`GDK_BACKEND=x11`). To see the gtk3 failure: `SAL_USE_VCLPLUGIN=gtk3`.
- Pin the locale for tests: `LC_ALL=en_US.UTF-8` or `C.UTF-8` (the default sheet name `Sheet1` is English).

## 6. Run the tests that need no office (not run yet)

```
bash scripts/run_stage_tests.sh
```

It runs every `tests/test_*.js` with `node`, `py_compile` on `tests/*.py` and `scripts/*.py`, and
`tests/check_xml.py`. `test_allowlist.js` and the schema part of `check_xml.py` print SKIPPED unless `LO_SRC` points
to a LibreOffice source tree. Single tests: `node tests/test_runtime.js`.

The stage tests `tests/test_stage1.py` to `test_stage7.py` are **not** run by this script. They start a real office
and were written for Windows. What a Linux port needs:

1. `tests/harness.py`: `soffice.exe` becomes `soffice`; the profile URL `file:///` + path becomes `file://` + an
   absolute path (otherwise four slashes); the "run `scripts\env.ps1`" messages.
2. A shell replacement for `scripts/env.ps1` and `scripts/run_all.ps1`: set `LO_PROGRAM`, `PLUGINHOST_PROFILE_DIR`,
   `PLUGINHOST_PLUGINS_DIR` (copy `tests/plugins/hello`), `PLUGINHOST_PLUGIN=hello`, `PLUGINHOST_TEST_MODE=1`,
   `PLUGINHOST_LOG`.
3. A Python with the `uno` module: the office's own Python (not shipped by the Debian packages) or
   `python3-uno` from the distribution with `LO_PROGRAM` on `sys.path`.
4. A display: a real X11 session, or Xvfb. Note that the sidebar needs a window; the embedding limits above decide
   whether stages 2 to 7 can pass at all.
5. `tests/test_stage7.py`: replace the PowerShell kill of `msedgewebview2.exe` by a kill of the WebKit child
   processes of `soffice.bin` (for example `WebKitWebProcess`) and check that `webhost|renderer_failed` is logged
   (the Linux host logs it in `onProcessTerminated`).
6. `tests/run_member.py` already picks `soffice` and `/usr/lib/libreoffice/program` on Linux; it needs
   `-DPH_TEST_HOOKS=ON`, and its UNO socket start-up is unverified.
7. Fix `tests/check_xml.py` and `tests/test_allowlist.js` defaults (`C:\Oracle\lo`) by setting `LO_SRC`.

## 7. Known not to work yet

- **Nothing has been compiled or run on any OS for the C++ side** (`docs/RESULTS.md`, `docs/FULL_API_STATUS.md`).
  Expect compile and link errors.
- **Linux web view embedding**: see section 1. Decision 14 (WebKitGTK, CEF as fallback) depends on the embedding
  check on a Linux host.
- **`src/CMakeLists.txt` is not yet Linux ready**: see the "CMake changes" list in the build-kit report
  (`-fPIC` for QuickJS, hidden visibility, library output folder, `LINUX`/`GCC` defines).
- **Not built/ready**: the first-use permission prompt (ADR-002), the "Plugin stopped" view, the Linux side of the
  stage tests, `api_bridge` fixes still listed as planned in `docs/REVIEW_2026-11-28.md`.
- **F3 to F8** of the full-API plan (generic bridge compile, conversion layer, headless runner, Document Builder
  expectations, recipes) wait for the Linux build (`docs/FULL_API_STATUS.md`, decisions 10 and 11).
- **TestRunner** (`docs/TEST_RUNNER.md`): written, not compiled; threading and `createSingleComponentFactory` behaviour
  are unverified.
- macOS is out of scope. Flatpak and Snap offices cannot load the host WebKitGTK.

## 8. What to report back after the first compile

Paste verbatim, not summarised:

1. `uname -a`, `lsb_release -a`, `g++ --version`, `cmake --version`, `apt-cache policy libreoffice-dev libwebkit2gtk-4.1-dev`.
2. The output of each script up to the first failure (`fetch.log`, `gen.log`, `build.log` if you used the workflow
   or `tee`). For compile errors: the **first 50 error lines, exactly as printed**, with the file and line. Do not
   fix several unrelated errors before reporting the first batch.
3. Whether the QuickJS SHA-256 matched (the script prints the value).
4. After a successful build: the output of `scripts/pack.sh` (file list) and `unzip -p dist/*.oxt pluginhost.components`.
5. First run: output of `unopkg list`, the contents of `$PLUGINHOST_LOG`, which VCL plugin and session type
   (`echo $XDG_SESSION_TYPE`, `SAL_USE_VCLPLUGIN`), and a yes/no for "panel visible" and "plugin page visible".
6. Any `Gtk-ERROR` or `GLib-GObject-WARNING` line printed to the terminal (criterion 8).
7. Result of `bash scripts/run_stage_tests.sh` (the summary block).
