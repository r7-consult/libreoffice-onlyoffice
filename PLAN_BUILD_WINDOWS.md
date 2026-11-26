# План: сборка бинарной базы LibreOffice на Windows (для автономного исполнителя)

Исполнитель: модель Haiku без присмотра. Читай план целиком, выполняй этапы строго по порядку.
Каждый этап: **Действия -> Проверка (gate) -> Если не прошло**. К следующему этапу переходи только после прохождения gate.

## 0. Правила (нарушать нельзя)

1. Не менять исходный код LibreOffice. Не коммитить, не делать `git add`, `git reset`, `git clean`, `git checkout`. Ветка `lo-plugins` остаётся как есть.
2. Не трогать `AGENTS.md`, папку `.claude/`, файлы `LIBREOFFICE_*.md`.
3. Не вводить пароли и не пытаться обходить UAC, прокси и права. Если нужное недоступно, это **блокер** (см. раздел 10): остановись и запиши отчёт.
4. Скачивать только с официальных источников из списка в разделе 2.
5. Все логи писать в `C:\Oracle\build-logs\` (создать). Исходники и сборка: `C:\Oracle\lo`.
6. Один шаг, одна команда. Не склеивай команды через `&&` в PowerShell 5.1 (не работает). Не используй `${...}` и скобки сразу после слова: хук окружения блокирует такие команды. Пиши простые команды, сложное клади в `.ps1`/`.sh` файлы в `C:\Oracle\build-scripts\` и запускай файл.
7. Долгие команды (больше 2 минут) запускай в фоне (`run_in_background`) и читай лог хвостом; не опрашивай чаще раза в 5 минут.
8. После каждого этапа дописывай строку в `C:\Oracle\build-logs\PROGRESS.md`: `этап N: OK|BLOCKED|FAILED, время, коротко`. При повторном запуске начни с чтения этого файла и продолжи с первого незавершённого этапа.

## 1. Цель и критерий готовности

Собрать LibreOffice из `C:\Oracle\lo` в рабочий каталог `C:\Oracle\lo\instdir` (без установщика).

Готово, когда выполнены все:
- `C:\Oracle\lo\instdir\program\soffice.exe` существует;
- `soffice.exe --headless --version` печатает версию;
- headless-конвертация тестового `.fods` в `.pdf` даёт непустой файл (этап 8);
- `PROGRESS.md` содержит OK по всем этапам.

## 2. Что нужно установить

| Компонент | Версия | Источник |
|---|---|---|
| Visual Studio 2022 Build Tools | 17.x | https://aka.ms/vs/17/release/vs_BuildTools.exe |
| Cygwin x86_64 | текущий | https://www.cygwin.com/setup-x86_64.exe |
| NASM | 2.16+ | https://www.nasm.us/pub/nasm/releasebuilds/ |
| JDK (необязательно) | 17+ | Eclipse Temurin, https://adoptium.net |

Нагрузки Visual Studio (ID для установщика):
`Microsoft.VisualStudio.Workload.VCTools`, `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`, `Microsoft.VisualStudio.Component.Windows11SDK.22621`, `Microsoft.VisualStudio.Component.VC.ATL`, `Microsoft.VisualStudio.Component.VC.ATLMFC`, `Microsoft.VisualStudio.Component.VC.CLI.Support`.

Пакеты Cygwin: `autoconf automake bison flex gperf make patch perl pkg-config unzip zip git libtool ccache rsync curl wget gawk grep sed diffutils findutils cpio ed binutils python3`.

JDK не нужен, если конфигурировать с `--without-java` (так и делаем для базы).

## 3. Этап 1. Предпроверка

Действия (каждая команда отдельно):
1. `Get-PSDrive C` — свободно не меньше 90 ГБ.
2. Проверить права администратора: `net session` может быть запрещён хуком, поэтому проверяй косвенно: запиши тестовый файл в `C:\Windows\Temp\x.txt` (если отказано, прав нет) и удали его.
3. Проверить доступ к источникам: `curl -sI -m 20 <URL>` для каждого URL из раздела 2 и для `https://dev-www.libreoffice.org/` , `https://github.com`.
4. Проверить версии: `git --version`, `python --version`.
5. Создать каталоги `C:\Oracle\build-logs`, `C:\Oracle\build-scripts`, `C:\Oracle\downloads`.
6. Включить длинные пути: ключ реестра `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem` значение `LongPathsEnabled`=1 (нужны права администратора; если нет — запиши предупреждение, не блокер).

Gate: места достаточно; все URL отвечают не 407/403; права администратора есть.
Если не прошло: блокер B1 (нет прав), B2 (прокси), B3 (диск). Раздел 10.

## 4. Этап 2. Visual Studio Build Tools

1. Скачать `vs_BuildTools.exe` в `C:\Oracle\downloads`.
2. Запустить тихую установку одним файлом-скриптом `install_vs.ps1`:
   `vs_BuildTools.exe --quiet --wait --norestart --nocache --add <каждый ID через --add> --includeRecommended` (без `--includeOptional`).
3. Лог установщика: `%TEMP%\dd_*.log`; скопировать последние в `build-logs`.
4. Если код возврата 3010 — нужна перезагрузка: запиши в PROGRESS «нужна перезагрузка» и остановись (блокер B4).

Gate: файл `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\<версия>\bin\Hostx64\x64\cl.exe` существует; найден `ucrt` SDK в `C:\Program Files (x86)\Windows Kits\10\Include\`; `ATL` заголовок `atlbase.h` найден под `...\VC\Tools\MSVC\<версия>\atlmfc\include\`.

## 5. Этап 3. Cygwin

1. Скачать `setup-x86_64.exe`.
2. Тихая установка в `C:\cygwin64`:
   `setup-x86_64.exe -q -n -N -R C:\cygwin64 -l C:\cygwin-packages -s <зеркало> -P <пакеты через запятую>`.
   Зеркало: `https://mirrors.kernel.org/sourceware/cygwin/` (если недоступно — `https://cygwin.mirror.constant.com`).
3. Проверить: `C:\cygwin64\bin\bash.exe -lc "make --version"`.

Gate: `make`, `perl`, `gperf`, `bison`, `flex`, `autoconf`, `zip`, `unzip`, `patch`, `pkg-config` печатают версию в Cygwin bash. Делай проверку одним скриптом `check_cygwin.sh`, который выводит `OK name` или `MISSING name`.
Если не прошло: повторить установку только недостающих пакетов (`-P`), не более 2 попыток, иначе блокер B5.

## 6. Этап 4. NASM (и необязательный JDK)

1. Взять с `https://www.nasm.us/pub/nasm/releasebuilds/` последнюю папку, файл `win64/nasm-<ver>-win64.zip`.
2. Распаковать в `C:\nasm` (отдельный новый каталог).
3. Добавить `C:\nasm\nasm-<ver>` в пользовательский `PATH` не нужно: передадим путь через `configure` (этап 5) и окружение Cygwin.

Gate: `C:\nasm\nasm-<ver>\nasm.exe -v` печатает версию.

## 7. Этап 5. autogen и configure

Все команды выполняются в Cygwin bash через скрипты, запуск: `C:\cygwin64\bin\bash.exe -l C:/Oracle/build-scripts/<скрипт>.sh`.

Скрипт `autogen.sh` (положить в build-scripts):

    #!/bin/bash
    cd /cygdrive/c/Oracle/lo || exit 1
    export PATH=$PATH:/cygdrive/c/nasm/nasm-<ver>
    ./autogen.sh \
      --with-visual-studio=2022 \
      --without-java \
      --disable-ccache \
      --disable-online-update \
      --disable-odk \
      --without-junit \
      --without-doxygen \
      --without-help \
      --without-myspell-dicts \
      --disable-cve-tests \
      --enable-release-build=no \
      --with-parallelism=$(nproc) \
      2>&1 | tee /cygdrive/c/Oracle/build-logs/autogen.log

Примечание: `--enable-release-build=no` даёт быстрее сборку (без оптимизации полного релиза); флаг `--enable-debug` НЕ добавлять (раздувает диск).

Gate: в конце `autogen.log` есть строка `Now run 'make'`; файл `C:\Oracle\lo\config_host.mk` существует.
Если не прошло: найти в логе первую строку с `configure: error:`, найти в таблице раздела 11, применить исправление, перезапустить скрипт. Не более 3 попыток на одну и ту же ошибку.

## 8. Этап 6. Сборка

Скрипт `build.sh`:

    #!/bin/bash
    cd /cygdrive/c/Oracle/lo || exit 1
    make 2>&1 | tee -a /cygdrive/c/Oracle/build-logs/make.log

- Запуск в фоне. Время: 3-8 часов. Читать хвост лога: `Get-Content C:\Oracle\build-logs\make.log -Tail 20`.
- Загрузка внешних архивов идёт автоматически (с `dev-www.libreoffice.org`); при сбое сети просто перезапусти `make` (он продолжает с места остановки).
- Если процесс завершился с ошибкой: найти первую строку `Error` / `error:` / `*** [` в логе, применить раздел 11, перезапустить `make`.
- Не запускай `make clean` и `make distclean` ни при каких условиях.
- Контроль диска раз в час: если свободно меньше 15 ГБ — остановить и блокер B3.

Gate: лог кончается строкой `Build finished` или аналогичной без `Error`; файл `instdir\program\soffice.exe` существует.

## 9. Этап 7. Проверка результата

1. `C:\Oracle\lo\instdir\program\soffice.exe --headless --version` -> версия.
2. Создать `C:\Oracle\build-tests\t.fods` с минимальным документом (абзац текста «test»), затем:
   `soffice.exe --headless --convert-to pdf --outdir C:\Oracle\build-tests C:\Oracle\build-tests\t.fods`
3. Файл `t.pdf` больше 1 КБ.

Gate: оба условия выполнены. Затем записать в `PROGRESS.md` итог и создать `C:\Oracle\build-logs\REPORT.md`: версии компонентов, команда configure, длительность, размер `instdir`.

## 10. Блокеры (останавливайся, не импровизируй)

Создай `C:\Oracle\build-logs\BLOCKED.md`: номер, что именно не получилось, точная команда и вывод ошибки, что должен сделать человек. После этого завершай работу.

| ID | Условие | Что нужно от человека |
|---|---|---|
| B1 | Нет прав администратора для установки VS | Запустить скрипт установки от администратора |
| B2 | Прокси отвечает 407/403 для источника из раздела 2 | Открыть доступ к домену или положить установщик в `C:\Oracle\downloads` |
| B3 | Свободно меньше 15 ГБ или меньше 90 ГБ на старте | Освободить диск |
| B4 | Требуется перезагрузка | Перезагрузить и перезапустить исполнителя |
| B5 | Пакеты Cygwin не ставятся после 2 попыток | Проверить зеркала Cygwin |
| B6 | Одна и та же ошибка сборки после 3 попыток | Передать лог разработчику |

## 11. Типовые ошибки и действия

| Симптом | Действие |
|---|---|
| `configure: error: cannot find Visual Studio` | Проверить этап 2, добавить `--with-visual-studio=2022`, убедиться, что `vswhere.exe` существует |
| `configure: error: ... nasm` | Проверить `PATH` в скрипте, версия NASM не ниже 2.16 |
| `configure: error: ... gperf/bison/flex` | Доустановить пакет Cygwin (этап 3) |
| `configure: error: Windows SDK` | Доустановить компонент `Windows11SDK.22621` |
| `Cannot find atlbase.h` | Доустановить `VC.ATL` и `VC.ATLMFC` |
| Ошибки `curl`/`wget` при скачивании внешних архивов | Перезапустить `make`; при повторе проверить доступ к `dev-www.libreoffice.org` (блокер B2) |
| `fatal error LNK1104` / файл занят | Закрыть процессы `soffice`, антивирус исключить `C:\Oracle\lo` (если нельзя — просто повторить) |
| `cl: command line error` на длинных путях | Убедиться, что репозиторий в `C:\Oracle\lo`, а не в GUID-каталоге |
| Нехватка памяти (`C1060`, `out of heap`) | Перезапустить с `make -j4` вместо `-j` по числу ядер |

## 12. Что передать человеку по завершении

Короткое сообщение: статус, путь к `instdir`, версия, время сборки, найденные проблемы и что осталось не проверенным. Не выдумывай результаты: если этап не прошёл, так и пиши.
