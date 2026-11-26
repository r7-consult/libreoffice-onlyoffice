# План: прототип П-1 варианта «А+» (для автономного исполнителя Haiku)

Основание: [LIBREOFFICE_PLUGIN_HOST_OPTION_A_PLUS.md](./LIBREOFFICE_PLUGIN_HOST_OPTION_A_PLUS.md), раздел 11 (прототип П-1) и раздел 4 (архитектура). Прочитай эти два раздела перед началом.

**Важно.** Расширение собирается против **готового LibreOffice и его SDK**, а не против полной сборки ядра. Полная сборка из `C:\Oracle\lo` (см. `PLAN_BUILD_WINDOWS.md`) для П-1 **не нужна**. Исходники ядра нужны только как справочник (`odk/examples/cpp/custompanel`, `toolkit/source/awt/`).

Исполнитель работает один, без присмотра. Каждый этап: **Задачи -> Проверка (gate) -> Если не прошло**. К следующему этапу — только после gate.

## 0. Правила (нарушать нельзя)

1. Рабочий каталог проекта: `C:\Oracle\plugin-host` (новая папка, своя `git init`, identity `Developer Team <developer@team>`). В репозиторий `C:\Oracle\lo` ничего не писать, кроме чтения. Коммиты разрешены **только** в `C:\Oracle\plugin-host`, по одному на этап, сообщение: `stage N: <кратко>`.
2. Язык: C++17 (MSVC), тесты на Python из LibreOffice (`program\python.exe`), страницы плагинов на чистом JS без сборщиков.
3. Не вводить пароли, не обходить UAC и прокси, не скачивать ничего вне списка раздела 2. Недоступное — **блокер** (раздел 15).
4. Один файл исходника — не больше 400 строк. Новую функцию — сразу вместе с тестом. После каждого изменения: сборка, затем тест. Не накапливай непроверенный код.
5. Не более 3 попыток на одну и ту же ошибку. Потом: запись в `docs/ISSUES.md` (симптом, что пробовал, лог), этап помечается `PARTIAL`, переход к следующему этапу, если он не зависит от сломанного. Если зависит — блокер.
6. Не менять формулировки критериев, не ослаблять тесты, чтобы они прошли. Не писать «OK», не видя вывод проверки.
7. Каждый этап завершай записью в `docs/PROGRESS.md`: `этап N: OK|PARTIAL|BLOCKED, дата, что сделано, ссылка на лог`. При перезапуске читай этот файл и продолжай с первого незавершённого этапа.
8. Хук окружения блокирует часть PowerShell-конструкций (`${...}`, скобки сразу после слова, `&&`). Пиши простые команды по одной, сложное — в `.ps1`/`.py` файлы в `scripts/`. Команды дольше 2 минут — в фоне, лог читать хвостом.
9. Не угадывай API. Если не уверен в сигнатуре UNO, ищи в `C:\Oracle\lo\offapi\com\sun\star\...*.idl` и в примере `odk/examples/cpp/custompanel`.
10. Все пути, версии и номера портов — только из `config/versions.json` (создаётся на этапе 0). Не вписывай их в код вручную.

## 1. Цель и результат

**Цель:** проверить 8 критериев выхода П-1 (раздел 11 документа А+) на Windows и подготовить всё для проверки Linux и macOS человеком.

**Результат (все файлы в `C:\Oracle\plugin-host`):**
- расширение `dist/pluginhost-windows-x64.oxt`;
- тесты `tests/` и их запуск одной командой `scripts/run_all.ps1`;
- `docs/RESULTS.md` — таблица 8 критериев со значениями измерений и статусом `PASS | FAIL | NOT VERIFIED` для каждого;
- `docs/TEST_MATRIX_LINUX_MACOS.md` — инструкция для человека;
- `docs/PROGRESS.md`, `docs/ISSUES.md`.

Критерии 1 (macOS, Linux), 8 (GTK) на этой машине проверить нельзя — они получают статус `NOT VERIFIED`, а не `PASS`.

## 2. Что нужно (источники и версии)

| Компонент | Источник |
|---|---|
| LibreOffice, **две** последние стабильные ветки (например 25.8.x и предыдущая) + **SDK** к каждой | https://download.documentfoundation.org/libreoffice/stable/ |
| Visual Studio 2022 Build Tools (C++, Windows SDK, ATL) | уже должны стоять (см. `PLAN_BUILD_WINDOWS.md`, этап 2) |
| CMake, Ninja | https://github.com/Kitware/CMake/releases, https://github.com/ninja-build/ninja/releases |
| WebView2 SDK (NuGet-пакет `Microsoft.Web.WebView2`) | https://www.nuget.org/packages/Microsoft.Web.WebView2 |
| WebView2 Runtime | предустановлен в Windows 11; проверить |
| QuickJS-ng (исходники, тег выпуска) | https://github.com/quickjs-ng/quickjs/releases |
| Плагин-пример `helloworld`/`wordscounter` | https://github.com/ONLYOFFICE/sdkjs-plugins (Apache-2.0) |

Версии выбрать на этапе 0 и записать точно (с датой выбора) в `config/versions.json`. Брать **стабильные** выпуски, возраст не меньше 2 недель.

## 3. Этап 0. Окружение

Задачи:
1. Создать `C:\Oracle\plugin-host`, `git init`, каталоги `src/ tests/ scripts/ docs/ config/ third_party/ dist/ build/ logs/`.
2. Проверить: `cl.exe` (через `vcvars64.bat` из Build Tools), `cmake --version`, `ninja --version`, наличие WebView2 Runtime (папка `C:\Program Files (x86)\Microsoft\EdgeWebView\Application\`).
3. Скачать и установить **в разные каталоги** две версии LibreOffice (`C:\LO-A`, `C:\LO-B`) через административную распаковку MSI: `msiexec /a <файл>.msi /qn TARGETDIR=C:\LO-A` (не требует перезаписи системной установки). Так же распаковать SDK в `C:\LO-A-SDK`, `C:\LO-B-SDK`.
4. Записать в `config/versions.json`: версии LibreOffice, пути, версия SDK, версия WebView2 SDK, версия QuickJS-ng, найденный `program\python.exe` и `program\soffice.exe`, `program\types.rdb`, `program\types\offapi.rdb`.
5. Скачать QuickJS-ng в `third_party/quickjs-ng/` (исходники без истории), WebView2 SDK в `third_party/webview2/`.
6. Для каждого LibreOffice: `soffice.exe --version`.
7. Скрипт `scripts/env.ps1`: читает `versions.json` и задаёт переменные окружения для сборки.

Gate: обе версии `soffice.exe --version` печатают версию; `cl`, `cmake`, `ninja` отвечают; `versions.json` заполнен без пустых полей.
Если не прошло: нет прав (B1), нет доступа к источнику (B2), нет Build Tools (B3). Раздел 15.

## 4. Этап 1. Скелет расширения и пустая панель

Образец: `C:\Oracle\lo\odk\examples\cpp\custompanel` (файлы `ctp_panel.*`, `ctp_factory.*`, `Factories.xcu`, `*WindowState.xcu`, `Sidebar.xcu`/`description.xml`) — прочитай целиком до начала.

Задачи:
1. CMake-проект `src/CMakeLists.txt` (Ninja, x64, MSVC, `/std:c++17`, `/MD`). Заголовки UNO генерировать шагом сборки `cppumaker.exe -Gc -BUCR -O build/gen <types.rdb> <offapi.rdb>` (из SDK `bin`). Линковать `sal`, `cppu`, `cppuhelper`, `salhelper` (import-библиотеки из SDK `lib`).
2. Компонент UNO (DLL) по образцу примера: `PanelFactory` (`XUIElementFactory`) + `PluginPanel` (`XToolPanel`, `XSidebarPanel`). В панели — окно с текстом `Plugin Host`.
3. Файлы расширения: `extension/description.xml` (идентификатор `org.example.pluginhost`, платформа `windows_x86_64`), `META-INF/manifest.xml`, `PluginHost.components`, `Sidebar.xcu` (колода `PluginHostDeck`, панель, контексты Writer/Calc), `Factories.xcu`.
4. Скрипт `scripts/pack.ps1` собирает OXT (`zip`) в `dist/`.
5. Скрипты `scripts/install.ps1` и `scripts/uninstall.ps1`: `unopkg.exe add|remove` с **отдельным профилем** `-env:UserInstallation=file:///C:/Oracle/plugin-host/profile-A` (и `-B`). Системный профиль пользователя не трогать.
6. Логирование: библиотека `src/log.*` пишет в файл из переменной `PLUGINHOST_LOG` строки `время|модуль|событие|данные`.
7. Тестовый каркас `tests/harness.py` (запуск через `program\python.exe` соответствующей версии LibreOffice):
   - запустить `soffice.exe --norestore --nologo --accept="socket,host=localhost,port=<из versions.json>;urp;" -env:UserInstallation=...`;
   - подключиться через `uno`, создать документ Writer (`private:factory/swriter`) и Calc;
   - открыть колоду диспетчером `.uno:SidebarDeck.PluginHostDeck`;
   - дождаться записи `panel|created` в логе (таймаут 30 с);
   - завершить `soffice` в конце теста, даже при ошибке.

Gate: `tests/test_stage1.py` проходит на **обеих** версиях LibreOffice: расширение устанавливается, лог содержит `panel|created` для Writer и Calc.
Если не прошло: сверить с примером `custompanel` построчно (имена интерфейсов, регистрация фабрики, префикс URL `private:resource/toolpanel/...`).

## 5. Этап 2. Браузер в панели (критерий 1, Windows)

Задачи:
1. `src/webhost_win.*` — класс `WebHost` (интерфейс `src/webhost.hxx`: `create(parentHandle)`, `navigate(url)`, `postMessage(json)`, `setBounds`, `show/hide`, `reparent(newParent)`, `destroy`, обратный вызов `onMessage(json)`).
2. Нативное окно: из панели создать `systemchildwindow` через `XToolkit::createWindow` (см. `toolkit/source/awt/vclxtoolkit.cxx`, тип `systemchildwindow`) и получить HWND через `XSystemDependentWindowPeer::getWindowHandle(processId, SystemDependentWindow::SYSTEM_WIN32)`.
3. WebView2: `CreateCoreWebView2EnvironmentWithOptions` (каталог данных в профиле расширения), затем контроллер с родительским HWND. Весь вызов — в главном потоке LibreOffice, обратные вызовы WebView2 обрабатываются его циклом сообщений.
4. Страница `extension/content/probe.html`: заголовок `PROBE`, счётчик `n`, который растёт раз в секунду, и `window.chrome.webview.postMessage({type:"tick", n})`.
5. Хост пишет в лог `webhost|navigated|title=PROBE` и каждые 5 тиков `webhost|tick|n=...`.
6. Размер: `setBounds` при изменении размера панели (событие `XWindowListener::windowResized`).

Gate: `tests/test_stage2.py` (Writer и Calc): после открытия колоды в логе есть `webhost|navigated|title=PROBE` и не менее 3 строк `tick` с растущим `n`.
Если не прошло: проверить, что HWND ненулевой и это дочернее окно (`IsWindow`, `GetParent`); WebView2 Runtime установлен; цикл сообщений не блокируется.

## 6. Этап 3. Жизненный цикл панели без перезапуска (критерий 2)

Задачи:
1. `src/host_service.*` — синглтон (UNO-служба `org.example.pluginhost.HostService`) владеет экземплярами `WebHost` и состоянием плагина. `PluginPanel` только **подключает** (`reparent` к своему HWND) и **отключает** (`hide` + reparent на скрытое окно-хранилище) готовый экземпляр, не уничтожая его.
2. Состояние плагина: счётчик из `probe.html` обязан сохраняться при пересоздании панели.
3. Тест `tests/test_stage3.py`: открыть колоду, переключиться на другую колоду (`.uno:SidebarDeck.PropertyDeck`) и обратно 20 раз; сменить контекст (Writer: выделить текст/поставить курсор в таблицу); закрыть и открыть документ.
4. Проверки по логу: событие `webhost|created` для плагина ровно **1** раз за сеанс до закрытия всех документов; счётчик `n` не сбрасывался; нет `webhost|destroyed` между переключениями.

Gate: тест проходит на обеих версиях. Критерий 2 получает `PASS` только если нет ни одного перезапуска.
Если не прошло: сначала попробовать другой способ смены родителя (`SetParent` + `put_ParentWindow` + `NotifyParentWindowPositionChanged`). Если после 3 попыток перезапуск неизбежен — записать в ISSUES, критерий 2 = `FAIL` с описанием, реализовать запасной путь «перезагрузка с восстановлением состояния» и перейти дальше.

## 7. Этап 4. ProtocolAdapter и внедряемая среда

Справочник по протоколу ONLYOFFICE: каталог `sdkjs-plugins` (папка `v1/` — файл `plugins.js` и `plugins-ui.js`). Свой код пишется с нуля, **не копируя** код AGPL-проектов (`sdkjs`, `onlyoffice.github.io`). Из `sdkjs-plugins` (Apache-2.0) разрешено брать только плагины-примеры как тестовые данные.

Задачи:
1. `src/protocol_adapter.*`: чтение `config.json` плагина (поля: `name`, `guid`, `version`, `variations[].url`, `isVisual`, `isSystem`, `EditorsSupport`, `type`), хранилище плагинов в профиле пользователя.
2. Внедряемая среда `extension/content/runtime.js` (инжектируется скриптом при создании документа): объект `window.Asc.plugin` с `init`, `button`, `executeMethod`, `callCommand`, `info`, событиями; обмен через `window.chrome.webview.postMessage`.
3. Рукопожатие: `initialize` → `plugin_init` → `init`. Методы: `GetSelectedText`, `PasteText`.
4. Проверка источника сообщений: принимать только от своего origin плагина (отдельный `SetVirtualHostNameToFolderMapping` на плагин, имя хоста `<guid>.plugin.local`).
5. Тестовый плагин `tests/plugins/hello/` (`config.json`, `index.html`, `code.js`): после `init` вызывает `executeMethod("GetSelectedText")` и пишет результат в страницу и лог; кнопка вставляет текст через `PasteText`.
6. Попытаться взять из `sdkjs-plugins` `helloworld` и `wordscounter`. Если источник недоступен — только свой `hello`, пометить критерий «пример плагина без изменений» как `NOT VERIFIED`.

Gate: `tests/test_stage4.py`: в Writer выделить `abc`, плагин возвращает `abc`; вставка `XYZ` меняется в документе (проверить через UNO чтение текста). Сообщение от чужого origin отбрасывается (отдельный негативный тест).

## 8. Этап 5. CommandEngine: QuickJS-ng, 10 методов Api, сторож (критерии 3, 4)

Задачи:
1. Подключить QuickJS-ng как статическую библиотеку из `third_party/quickjs-ng/` (CMake, без глобальных состояний: один `JSRuntime` на плагин, один `JSContext`).
2. `src/command_engine.*`: `callCommand(fn)` — плагин передаёт **строку исходного кода** функции; движок вычисляет её в контексте, где глобален `Api`. Результат — только JSON (`JSON.stringify`), возвращается плагину как `onCommandCallback`.
3. Обёртка выполнения (всё в главном потоке): `XUndoManager::enterUndoContext("Plugin")` → `XModel::lockControllers()` → выполнить → `unlockControllers()` → `leaveUndoContext()`. При исключении — `leaveUndoContext` всё равно вызвать (RAII-класс), документ не оставлять заблокированным.
4. `Api` на C++ с **прямыми** вызовами UNO (без `Invocation`). Минимум 10 методов:
   - Calc: `Api.GetActiveSheet()`, `ApiWorksheet.GetRange(addr)`, `ApiRange.SetValue(value)` (скаляр и **двумерный массив**), `ApiRange.GetValue()`, `ApiWorksheet.GetName()`;
   - Writer: `Api.GetDocument()`, `ApiDocument.GetElement(i)`, `ApiDocument.GetElementsCount()`, `ApiParagraph.AddText(s)`, `ApiParagraph.GetText()`.
   Запись диапазона — через `XCellRangeData::setDataArray` одним вызовом (не по ячейке).
5. Сторожевой таймер: `JS_SetInterruptHandler` проверяет время; по умолчанию лимит 5 с (настраивается); при превышении — исключение `TimeoutError`, откат контекста Undo, запись `command|timeout` в лог.
6. Тестовая точка: UNO-метод `HostService.executeForTest(pluginGuid, scriptText)` возвращает JSON. Нужна только для тестов; защищена флагом конфигурации `TestHooksEnabled`, по умолчанию выключен.
7. Тесты `tests/test_stage5.py` (Calc):
   - **Критерий 3:** команда записывает двумерный массив 100×100 (10 000 ячеек); измерить время `executeForTest` — ≤ 1 с (медиана из 5 прогонов, холодный прогон не считать); выполнить `.uno:Undo` **один раз** — все 10 000 ячеек пусты;
   - **Критерий 4:** команда `while(true){}` — прерывается за лимит + не более 1 с; после этого LibreOffice отвечает на UNO-вызов и документ не заблокирован (`hasControllersLocked() == false`).

Gate: оба теста проходят на обеих версиях; числа измерений записаны в `docs/RESULTS.md`.
Если не проходит скорость: измерить, где время (`logs`), сначала убрать лишние вызовы UNO на ячейку; не меняй порог.

## 9. Этап 6. Контекстное меню (критерий 7)

Задачи:
1. `XContextMenuInterceptor` (`com.sun.star.ui.XContextMenuInterception`), регистрация на контроллере каждого документа через слушатель создания документов (`XDocumentEventListener` на `theGlobalEventBroadcaster`).
2. Перехватчик **синхронный**: он только читает кэш пунктов, полученных от плагина ранее (`Asc.plugin.event_onContextMenuShow` через ProtocolAdapter, заранее при смене выделения), и добавляет пункт «Plugin: Hello». Сетевых или блокирующих ожиданий нет. Если кэш пуст — запрос плагину уходит асинхронно, меню показывается без пункта.
3. Нажатие на пункт → событие плагину `onContextMenuClick` (id пункта).
4. Измерение: метка времени `ctxmenu|requested` (вход в `notifyContextMenuExecute`) и `ctxmenu|returned`.

Gate: `tests/test_stage6.py`: открыть меню средствами UNO (`dispatch .uno:ExecuteContextMenu` невозможен — вызывать перехватчик напрямую через тестовую точку `HostService.simulateContextMenu()`); время между `requested` и `returned` ≤ 300 мс (медиана из 20). Пункт присутствует.
Примечание: полная проверка отображения меню — ручная, добавь в `TEST_MATRIX_LINUX_MACOS.md` раздел «ручные проверки Windows».

## 10. Этап 7. Сбои и изоляция (критерий 5)

Задачи:
1. Подписка на `ProcessFailed` и `BrowserProcessExited` WebView2. При падении процесса отрисовки: лог `webhost|renderer_failed`, панель показывает текст «Плагин остановился» и кнопку «Перезапустить» (стандартный элемент awt), которая пересоздаёт браузер.
2. Нативный код устойчив: все обратные вызовы WebView2 обёрнуты в `try/catch (...)`, все исключения UNO на границе тоже.
3. Тест `tests/test_stage7.py`: открыть панель, найти процесс отрисовки WebView2, принадлежащий дереву процессов этого `soffice` (по `Get-CimInstance Win32_Process`, фильтр по `ParentProcessId` и командной строке `--webview-exe-name=soffice.exe`), завершить его (`Stop-Process`). Проверить: `soffice` жив; UNO отвечает; лог `renderer_failed`; после вызова `HostService.restartPlugin(guid)` снова появляются `tick`.

Gate: тест проходит на обеих версиях.

## 11. Этап 8. Установка на две версии без пересборки (критерий 6)

Задачи:
1. Один и тот же `dist/pluginhost-windows-x64.oxt` (без пересборки между версиями) установить в профили A и B.
2. Запустить `scripts/run_all.ps1` для **обоих** профилей: этапы 1–7 тесты.
3. Проверка версии при старте: расширение пишет в лог версию LibreOffice (`com.sun.star.configuration.theDefaultProvider`, путь `/org.openoffice.Setup/Product`, `ooSetupVersion`) и отключает недоступные возможности, не падая. Негативный тест: подставить в `config/versions.json` фиктивную «слишком старую» версию и убедиться, что расширение отвечает понятным сообщением в логе.

Gate: все тесты этапов 1–7 проходят на A и B из одного файла OXT. Результат — критерий 6.

## 12. Этап 9. Сравнение с wry (необязательный, только если этапы 1–8 завершены)

Выполнять **только** при наличии Rust (`rustup`, стабильная ветка) и доступе к crates.io. Иначе — `NOT VERIFIED`, этап пропустить без блокера.

Задачи: `webhost_wry.dll` (Rust `cdylib`, `wry` версии из `versions.json`, `build_as_child`), C-интерфейс с теми же 8 функциями `WebHost`, `catch_unwind` на каждой экспортируемой функции; запуск теста этапа 3 с wry-реализацией; записать: объём кода, размер DLL, время старта, работает ли смена родителя.
Gate: сводная таблица в `docs/WRY_COMPARISON.md`; решение не принимать, только факты.

## 13. Этап 10. Linux и macOS (подготовка для человека, критерии 1 и 8)

Здесь исполнитель **ничего не запускает**, а готовит материалы:
1. `src/webhost_linux.cxx` (CEF, окно с родительским XID) и `src/webhost_macos.mm` (WKWebView, родитель `NSView*`) — компилируемые заготовки с TODO там, где нужна проверка человеком; в CMake включаются условием `if(UNIX)`/`if(APPLE)`. Не заявлять, что они работают.
2. `docs/TEST_MATRIX_LINUX_MACOS.md`: по шагам, как собрать и запустить `tests/test_stage1..7.py` на Ubuntu LTS (gtk3 VCL-плагин и gtk4), Wayland-сессии с `GDK_BACKEND=x11`, macOS (x64 и arm64); таблица для заполнения: ОС, версия LibreOffice, VCL-плагин, критерий, результат.
3. Отдельные пункты Linux: конфликт GTK (запустить с `SAL_USE_VCLPLUGIN=gtk3` и `gtk4`, искать в логе `GLib-GObject-WARNING` и `Gtk-ERROR`), сессия под X11 и XWayland.

Gate: файлы существуют, CMake на Windows по-прежнему собирается (платформенные файлы не мешают).

## 14. Этап 11. Итоговый отчёт

1. Заполнить `docs/RESULTS.md`: таблица критериев 1–8 (формулировка из документа А+, статус, число или ссылка на лог, версия LibreOffice, дата), блок «Дополнительно измерено» (память на панель — `WorkingSet` процессов WebView2, холодный старт плагина — время от `created` до `navigated`, DPI — только если проверено).
2. Решение по таблице раздела 12 документа А+: указать **какая строка таблицы применима** и почему. Окончательное решение (ADR-001) оформляет человек.
3. `docs/ISSUES.md` дополнить открытыми вопросами.
4. Запустить `scripts/run_all.ps1` целиком, приложить итог.
5. Закоммитить (`stage 11: report`).

Критерий-статусы: `PASS` — измерено и в пределах порога; `FAIL` — измерено и нет; `NOT VERIFIED` — не проверялось на этой машине. Нельзя ставить `PASS` без измерения.

## 15. Блокеры (остановись, не импровизируй)

Создай `docs/BLOCKED.md`: номер, что не вышло, точная команда и вывод, что должен сделать человек.

| ID | Условие | Что нужно от человека |
|---|---|---|
| B1 | Нет прав для установки LibreOffice/Build Tools | Установить или выдать права |
| B2 | Прокси блокирует источник из раздела 2 | Открыть домен или положить файлы в `C:\Oracle\downloads` |
| B3 | Нет Visual Studio Build Tools | Выполнить `PLAN_BUILD_WINDOWS.md`, этап 2 |
| B4 | Нет интерактивного рабочего стола: `soffice` с боковой панелью не стартует | Запустить исполнителя в сеансе с рабочим столом |
| B5 | Одна и та же ошибка после 3 попыток и этап критичен (1, 2) | Разобрать `docs/ISSUES.md` |
| B6 | WebView2 Runtime отсутствует и не ставится | Установить Evergreen Runtime |

## 16. Типовые ошибки

| Симптом | Действие |
|---|---|
| `unresolved external symbol` для UNO-типов | Недостающую библиотеку взять из SDK `lib`; убедиться, что окружение UNO `msci` соответствует x64 (в примере посмотри `CPPU_ENV`) |
| Расширение ставится, панели нет | Проверить `Sidebar.xcu`: идентификатор колоды, `ContextList`, URL фабрики; открыть `unopkg list` для профиля |
| Панель есть, окно пустое | Проверить размеры панели и `setBounds`; HWND не нулевой; WebView2 создан после показа окна |
| `soffice` не завершается после теста | Принудительно `Stop-Process` по PID, запомненному при запуске; не использовать `taskkill /im soffice.exe` (заденет чужие процессы) |
| Тест зависает | Все ожидания с таймаутом; на таймауте — завершить `soffice` и пометить тест `FAIL` |
| Профиль повреждён | Удалить **только** `C:\Oracle\plugin-host\profile-A`/`-B` и поставить расширение заново |
| Паника/исключение JS роняет процесс | Обернуть границу JS↔C++ `try/catch`, проверить, что `JS_FreeValue` вызывается на всех ветках |

## 17. Что передать человеку по завершении

Короткое сообщение: статусы критериев 1–8, что проверено только на Windows, что NOT VERIFIED, открытые проблемы из `ISSUES.md`, пути к `RESULTS.md` и OXT. Не выдумывай результаты.
