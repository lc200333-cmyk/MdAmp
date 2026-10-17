# MdAmp

Самостоятельный рабочий аудиоплеер для Windows x64 на C++17 / Win32.
Первая функциональная версия по наблюдениям Modern. Это **не полный клон
Winamp**: точные границы описаны в [матрице интерфейса](docs/ui-parity.md) и
[списке всех 62 исследованных модулей](docs/plugin-coverage.md).

## Работает

- Локальные WAV, MP3 и FLAC; настоящий Windows WASAPI-вывод.
- Play/Pause/Stop, previous/next, seek, volume, balance, shuffle и три режима repeat.
- Основной компактный плеер, drawer Equalizer / Options / Themes, shade-режим,
  шесть собственных палитр и масштаб 100% / 200%.
- Отдельные Playlist Editor и Media Library; меню File/Play/Options/View/Help,
  File/Playlist/Sort/Help в плейлисте, контекстные меню и foreground-hotkeys.
- Поиск, выбор, удаление из списка (не с диска), сортировка, reverse/randomize,
  добавление файлов/папок, drag-and-drop и UTF-8 M3U/M3U8 import/export.
- Настоящий 10-полосный DSP-эквалайзер, preamp, пять собственных пресетов.
- Реальная DLL `plugins/MdAmpStereo.dll`: наш stereo-enhancer и MdAmp DSP ABI v1.
- Спектр из фактического PCM, восемь страниц настроек, сохранение настроек и
  сеанса в `%LOCALAPPDATA%\MdAmp`. При ошибке загрузки старый сеанс сохраняется.

Старые DLL Winamp, MAKI/WSZ/WAL-скины, AVS/MilkDrop, видео, остальные форматы,
кодировщики, CD, синхронизация устройств и онлайн-сервисы **не реализованы**.
Совпадение всех окон/настроек и пикселей не заявляется. Программа не использует
установленный Winamp и не требует его наличия.

## Локальная сборка без GitHub

Нужны Visual Studio Community либо Build Tools с «Разработка классических
приложений на C++», MSVC x64/x86, Windows SDK и C++ CMake/Ninja.

```powershell
.\build.bat
.\build.bat -Configuration Debug
.\build\Release\bin\MdAmp.exe
```

Обе команды сборки запускают шесть тестов. Сборка не скачивает пакеты.
Не переносите один `.exe` без соседней папки `plugins`, если нужен DSP-модуль.
MSVC Runtime линкуется статически; проверка на чистой машине ещё необходима.
Можно открыть `CMakeLists.txt` в Visual Studio.

## Горячие клавиши

| Клавиша | Действие |
| --- | --- |
| Z / X / C / V / B | Previous / Play / Pause / Stop / Next |
| Space, Left/Right, Up/Down | Pause, seek ±5 с, volume |
| L / Ctrl+O; Ctrl+L | Открыть; добавить файлы |
| J; Enter; Delete; Ctrl+A | Поиск; играть; удалить из плейлиста; выделить |
| S / R | Shuffle / цикл repeat off → playlist → track |
| Ctrl+P; Ctrl+T; Ctrl+W | Настройки; поверх окон; shade |
| Alt+E / Alt+G / Alt+L | Плейлист / EQ drawer / медиатека |
| Ctrl+S / Ctrl+Shift+O | Сохранить / загрузить плейлист |
| Ctrl+Shift+K / Alt+3 | Визуализация / информация о файле |
| Alt+F/P/O/I/H | Меню основного плеера |

Это выбранный рабочий набор, не вся исходная accelerator table; например
Ctrl+L здесь добавляет локальные файлы, а не открывает сетевой URL.

## GitHub Actions и версия

Push в `main` или `master` запускает `Windows build and version`: отдельный job
увеличивает patch в `VERSION`, коммитит только этот файл без force push, затем
build-job собирает **именно полученный коммит**, проверяет Release/Debug и
сохраняет ZIP в Actions Artifacts. GitHub Releases и теги не создаются.
Автокоммит не запускает бесконечный цикл. PR собирается без изменения версии.

[Репозиторий](https://github.com/lc200333-cmyk/MdAmp) ·
[Actions](https://github.com/lc200333-cmyk/MdAmp/actions)

## Документация

- [Архитектура](docs/architecture.md), [аудиодвижок](docs/audio.md)
- [Проверки](docs/verification.md), [оставшиеся этапы](docs/roadmap.md)
- [Соответствие UI](docs/ui-parity.md), [все исследованные плагины](docs/plugin-coverage.md)
- [Происхождение](docs/provenance.md), [сторонние компоненты](THIRD_PARTY_NOTICES.md)

Исходники, графика, скины и бинарники Winamp не распространяются. miniaudio
0.11.23 включён с upstream notices; лицензия собственного кода MdAmp пока
не назначена владельцем. Личные медиаданные и REA bundles не публикуются.
