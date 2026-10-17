# Архитектура первой рабочей версии

## Границы

C++17 / Win32 / GDI, Windows x64, MSVC, CMake/Ninja. Нативный GUI не использует
Winamp engine, DLL, XML, MAKI или оригинальные bitmap. Qt/Flutter не нужны.
Решение заменяет стартовый каркас функциональным ядром, не весь старый плеер.

| Часть | Ответственность |
| --- | --- |
| `main.cpp` | Окна, меню/команды, контроллер плейлиста, события, настройки и DSP-host |
| `modern_ui.*` | Собственная векторная отрисовка и общая карта hit-regions |
| `playlist_ui.*` | Рамка/геометрия/карта footer-команд плейлиста; живые native Edit/ListView создаёт controller |
| `audio_engine.*` | Декодирование через miniaudio, WASAPI, transport, спектр |
| `equalizer.h` | Собственные stereo biquad filters и preamp |
| `storage.*` | UTF-8 M3U/settings, clamping, bounded scan, атомарная замена файлов |
| `plugin_api.h`, `plugins/stereo.cpp` | Собственный MdAmp DSP ABI v1 и загружаемая DLL |
| `VERSION`, `version.h.in`, `MdAmp.rc.in` | Единая версия для GUI и Windows properties |

Аудиопуть: decoder → 10-band EQ → MdAmp DSP callback → balance → volume →
finite/clamped samples → WASAPI. Спектр вычисляется из PCM. Подробности потоков,
буферизации, форматов и ограничений — в `audio.md`.

Данные живут только в собственном LocalAppData/MdAmp. Исходный Winamp и его
базы/плейлисты/настройки автоматически не читаются. Remove не удаляет медиафайлы.
Лимит сеанса — 10,000 записей, чтения плейлиста — 16 MiB. При повреждённом
сеансе оригинал не перезаписывается, новый сеанс получает другое имя.

DLL ищется по полному пути в подпапке `plugins` рядом с exe с ограниченными
LoadLibrary search flags. Это доверенный поставляемый модуль, не sandbox и не
загрузчик произвольных Winamp DLL. Перед выгрузкой модуля закрывается engine.

## Решения и риски

- miniaudio 0.11.23 выбран вместо копирования input/output DLL. Библиотека
  зафиксирована в Git, SHA и лицензия сохранены, сетевой FetchContent не нужен.
- GDI позволяет собственный компактный UI; замена на Direct2D возможна позже.
- Простая сериализация mutex подходит первой версии, но не hard-real-time
  worker/ring-buffer архитектура. Сглаживания EQ и crossfade пока нет.
- GUI/controller ещё в одном файле: дальнейшее развитие должно выделять модели
  постепенно, с regression tests, а не проводить ненужный массовый рефакторинг.
- Функциональный аналог не означает совместимость с Wasabi/Winamp ABI или
  точное воспроизведение всех 357 resource dialogs.

## CI

Push-only version-job получает contents:write; PR/build-job имеет contents:read.
Версия меняется только обычным commit/push; при расхождении истории job падает,
а не переписывает remote. Сборка идёт на SHA version-job и пакует приложение,
DSP DLL и notices. Actions зафиксированы по upstream SHA. Release-публикации нет.
