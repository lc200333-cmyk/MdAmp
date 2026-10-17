# MdAmp: проверяемая граница полного повторения

Статус: **НЕ ЗАВЕРШЕНО**. Это проект критериев приёмки, а не отчёт
об их выполнении. Рабочая MdAmp 0.1.1 сохраняется; перечисление ниже не меняет
её возможности и не означает, что новые движки уже существуют.

Выбранный подход: самостоятельная реализация с новым исходным кодом. MdAmp
не загружает оригинальные DLL, APE, `gen_ff.dll` или исполняемый код Winamp.
Существующий `MdAmpStereo.dll` использует собственный MdAmp DSP ABI v1 и не
доказывает совместимость ни с одним старым плагином.

## 1. База, источники и конечная граница

Единственная текущая база сравнения — установленный **Winamp 5.8.0.3660**,
`winamp.exe` SHA-256
`bf895bdee6b4c6db180a9eee6b47e2cfbff40e8d1914d319d1efb67c95ce8f5b`.
Установщик 5.53 — отдельный исторический артефакт, не взаимозаменяемая база.

Исходные локальные отчёты пользователя:

- `rea-clone`: архитектура, команды, частично исследованные ABI и ограничения;
- `rea-clone-evidence/plugin-exports.json`: наблюдавшиеся экспортные имена;
- `rea-clone-modern-evidence/audio/INTERFACE-GUIDE.md`: аудио, AVS, MilkDrop;
- `rea-clone-modern-evidence/audio/NATIVE-AND-APE.md`: ограниченные callback-
  наблюдения и семь APE;
- `rea-clone-modern-evidence/library/README.md` и `NATIVE-APPENDIX.md`:
  библиотека, устройства, статические ресурсы и ограничения;
- `rea-clone-modern-evidence/skin/report.md` и JSON-каталоги: Modern/XML/MAKI;
- `rea-clone-modern-evidence/core/README.md` и `core/native-bridge.md`:
  ресурсы ядра, Freeform и границы исследования;
- `docs/plugin-coverage.md` и `docs/ui-parity.md`: состояние MdAmp до этой
  программы работ. Они остаются поимённым реестром текущих пробелов.

Имена в следующих таблицах — **ключи функционального соответствия**, не
обещание поставлять DLL с такими именами. Конечная граница модулей: 55 DLL
и 7 APE из исследованной установки, то есть все 62 строки текущего реестра.
Это не все версии Winamp и не все существующие в интернете плагины.

Наблюдения статического анализа не заменяют сценарии исполнения. Предлагаемые
ниже проверки — требования к новой реализации. Неустановленные точные значения,
динамические меню и переходы необходимо сначала подтвердить на базе; нельзя
выдавать предполагаемое поведение за наблюдавшееся.

## 2. Общее правило готовности каждой строки

Строка получает статус `PASS` только при одновременном выполнении всех условий:

1. Есть новый рабочий backend, а не только нарисованный диалог, список опций,
   сообщение «успешно» или перенаправление в чужое приложение.
2. Каждая видимая настройка имеет проверенный смысл, диапазон, значение по
   умолчанию, Apply/Cancel, момент применения и сохранение после перезапуска.
   Неизвестные параметры сначала отмечаются `UNKNOWN`, а не выдумываются.
3. Есть входной fixture, ожидаемый результат и автоматический тест или
   воспроизводимый ручной протокол. Тест отрицательного случая проверяет
   диагностическое сообщение и отсутствие повреждения данных.
4. Есть обход соответствующих окон/вкладок/меню мышью и клавиатурой, включая
   пустое состояние, рабочее состояние, ошибку и повторное открытие.
5. Для визуального соответствия сохранены парные исходные PNG и результат
   сравнения по правилам раздела 6. Успешная сборка/тест hit-map не является
   подтверждением изображения.
6. Сохраняются уже работающие playback, seek, pause, EQ, playlists, storage
   и собственный DSP. Debug и Release проходят регрессионные проверки.

Состояния: `NOT IMPLEMENTED`, `PARTIAL`, `UNKNOWN`, `BLOCKED FIXTURE`, `PASS`.
`SKIP`, `UNKNOWN` и `BLOCKED FIXTURE` не считаются `PASS` и не позволяют
объявить всю программу полностью повторённой.

## 3. Приёмка восьми семейств DLL и AVS APE

Все строки ниже пока **не приняты**. Частичные альтернативы MdAmp перечислены
в `plugin-coverage.md`; они не закрывают требования этой таблицы целиком.
Для каждой строки также обязательны общие проверки раздела 2.

### 3.1. Входные модули — 16

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `in_avi.dll` | AVI demux, аудио/видеодекодирование, синхронизация, seek, File Properties | Фиксированный AVI с таймкодом: изображение/звук, длительность и seek соответствуют fixture; повреждённый файл даёт ошибку. |
| `in_cdda.dll` | Чтение audio-CD, дорожки/CD Text, чтение PCM/извлечение, окно диска и ошибки привода | Тестовый физический/контролируемый образ диска: номера/длины дорожек, PCM-хеш и отмена извлечения; отсутствие привода проверяется отдельно. |
| `in_dshow.dll` | DirectShow-конвейер, выбор типов, перечень фильтров, открытие реальных property pages | Фиксированный фильтр/файл даёт ожидаемую цепочку; двукратное нажатие открывает настройки действующего фильтра; отмена не меняет состояние. |
| `in_flac.dll` | FLAC, параметры расширений/вывода информации, чтение/редактирование тегов | Mono/stereo и разные bit-depth/rate; seek; добавление/удаление произвольного тега с проверкой после повторного открытия без повреждения аудио. |
| `in_flv.dll` | FLV demux, поддерживаемые аудио/видеопотоки, buffering/seek, информация | Контрольные FLV с заявленными кодеками; синхронизация и seek; неподдерживаемый кодек явно отклонён. |
| `in_linein.dll` | Захват входа, разбор `linein://`, параметры устройства/формата, monitoring | Контролируемый loopback/тестовый источник: частота/каналы и PCM; смена устройства и его исчезновение; тест не подменяет вход синтетической анимацией. |
| `in_midi.dll` | MIDI/RMI, синтез/устройства, DLS, SysEx, loop/tempo/channels, lyrics и редакторы | Конечный набор MIDI/RMI с tempo/loop/lyrics/SysEx; слышимые и проверяемые изменения mute/program/tempo; round-trip безопасного SysEx-presets fixture. |
| `in_mkv.dll` | Matroska demux, выбранные audio/video streams, seek и свойства | Файл с несколькими потоками и таймкодами: выбор/seek/синхронизация и корректная информация; неизвестный кодек не выдаётся за работающий. |
| `in_mod.dll` | Трекерные форматы, metadata/seek и информационное окно | Заявленный конечный набор MOD/XM/IT/S3M fixtures проходит воспроизведение/длины/seek; Config в исследованной базе информационный, не выдуманная страница параметров. |
| `in_mp3.dll` | Заявленные MPEG audio, decoder policies, buffering/HTTP/SHOUTcast, ID3/APEv2/Lyrics3 UI | MPEG fixtures разных слоёв/битрейтов; контролируемый сервер с auth/underrun/title/artwork; tag round-trip для каждого заявленного формата и режима кодировки. |
| `in_mp4.dll` | MP4/M4A demux и заявленные AAC/audio codecs, extensions/config | AAC/MP4 fixtures, gap/seek/duration по контракту; настройка расширений применяется после указанного в UI момента. |
| `in_nsv.dll` | NSV demux, audio/video sync, seek/buffering, metadata editor | NSV fixtures со всеми заявленными потоками; запись метаданных с повторным чтением; обрезанный файл/underrun без зависания. |
| `in_swf.dll` | Самостоятельный SWF-runtime для объявленного поднабора с навигацией/звуком и UI | Версионированный SWF corpus; рабочие интерактивные действия и звук; неподдерживаемые конструкции явно перечислены, не называются полной поддержкой. |
| `in_vorbis.dll` | Ogg Vorbis, chained streams, HTTP buffering, теги | Цепочка с изменением rate/channels, seek и границы потоков; tag round-trip; контролируемый сетевой underrun. |
| `in_wave.dll` | Заявленные waveform-контейнеры/кодеки, extensions/config/info | PCM/float и каждый дополнительно заявленный codec fixture; длина/seek/sample values; ошибка неподдерживаемого формата. |
| `in_wm.dll` | Заявленные WMA/WMV/Windows Media и потоковые протоколы, конфигурация | Отдельные audio/video/stream fixtures, синхронизация и seek; DRM/недоступный codec не обходится и не маркируется успешным. |

Параметры устройств, внешние codec property pages и сетевые значения нельзя
восстановить одним копированием подписей из PE-ресурсов. Кодеки/зависимости
каждого теста фиксируются в протоколе; наличие на машине разработчика не
считается доказательством наличия на чистой целевой Windows.

### 3.2. Выходные модули — 4

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `out_disk.dll` | Disk writer, format/codec chooser, naming, batch, RIFF/RAW согласно выбранному режиму | Контрольный PCM записан в выбранный формат; sample count/header/hash проверены; cancel/disk-full/существующий файл обработаны без ложного успеха. |
| `out_ds.dll` | DirectSound, device/buffer/fade/status страницы | Звук на выбранном устройстве; измеренный pause/seek/flush, buffer/latency; fades проверены PCM/loopback, не только положением slider. |
| `out_wasapi.dll` | WASAPI, управление воспроизведением и реальное устройство | Проверка потока, sample rate, pause/seek/flush и смены устройства; Config исследованной DLL — пустой `RET`, поэтому отдельное старое окно настроек не создаётся. |
| `out_wave.dll` | WinMM waveOut, device/buffering/volume policy и status | Реальный waveOut path, сохранение/восстановление volume согласно настройке, buffer tests, device disappearance и повторный старт. |

### 3.3. Кодировщики — 5

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `enc_flac.dll` | FLAC encoding/compression settings | Разные compression levels; decoded output идентичен исходному PCM; размер/время не симулируются. |
| `enc_lame.dll` | MP3 CBR/VBR/ABR, bitrate/quality/VBR limits, dependency error UI | Каждый mode и край диапазона закодирован; headers/bitrate/duration и decode проверены; ошибка encoder имеет честную причину. |
| `enc_vorbis.dll` | Vorbis quality/bitrate settings | Контрольные quality values формируют валидный Ogg Vorbis; повторное чтение и длительность; корректная отмена. |
| `enc_wav.dll` | WAV/RAW, codec/format chooser | Форматы/channels/rate/bit-depth соответствуют выбору; RIFF header присутствует только где требуется; RAW проверяется по внешнему контракту PCM. |
| `enc_wma.dll` | WMA format/sample-format/bitrate enumeration и encoding | Каждый заявленный режим кодируется/декодируется; перечень отражает действующие backend capabilities; недоступный режим не сохраняется как работающий. |

Единая очередь Format Converter должна действительно читать, преобразовывать,
писать и завершать/отменять задания; её требования включены в `ml_transcode`.
Наличие декодера не закрывает ни одну строку кодировщика.

### 3.4. DSP, визуализация, NSV-декодеры — 5

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `dsp_sps.dll` | SPS expression language/runtime, init/slider/per-sample scopes, четыре sliders/triggers, editor/help/presets | Вычисления и состояние по golden fixtures, save/load round-trip, audible/PCM-изменение; syntax/runtime error не ломает аудиопоток; единый EQ вместо этого не принимается. |
| `vis_avs.dll` | AVS effect tree, renderer, expressions, buffers/blending, beat detection, presets/undo/redo, display/fullscreen/transitions/debug | Конечный corpus эффектов/пресетов, expression tests, frame comparisons при фиксированном PCM/seed/time; реальные Add/Delete/Reorder/Undo/Save/Load; все зарегистрированные эффекты сопоставлены с их страницами. |
| `vis_milk2.dll` | MilkDrop-compatible preset/equation/shader renderer, editor/browser/rating/mash-up, desktop/fullscreen/VJ/dualhead и configuration | Контрольные `.milk`-fixtures и shader levels; fixed PCM/time/seed/GPU-profile; frames, переходы, edit/save/reload, lock/rating/hotkeys и режимы проверены; spectrum не является заменой. |
| `nsvdec_vp3.dll` | VP3 decoder внутри NSV-пути | Decode известных кадров в ожидаемый pixel format/hash или согласованную codec tolerance, timestamps/seek и повреждённый frame; выдуманного отдельного config UI нет. |
| `nsvdec_vp5.dll` | VP5 decoder внутри NSV-пути | Отдельные VP5 fixtures и проверки frames/timestamps/errors; наличие VP3 не считается VP5-поддержкой. |

Для AVS/MilkDrop до объявления полного покрытия составляется конечный реестр
всех динамически зарегистрированных эффектов, экранов редактора и команд в
базе. Каталог ресурсов не установил их полный runtime-порядок. Зависимые от
GPU результаты требуют закреплённого профиля либо отдельного честно указанного
допуска; произвольное расхождение нельзя называть pixel identity.

### 3.5. Общие модули и Modern — 5

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `gen_crasher.dll` | Перехват контролируемой ошибки, локальный отчёт, настройки и явное согласие на передачу | Отдельный synthetic crash fixture создаёт читаемый отчёт; cancel не отправляет; передача только на контролируемый endpoint и без личных данных. |
| `gen_ff.dll` | Независимые skin/package/XML/XUI/layout/script runtimes и оконная интеграция | Утверждённый skin corpus проходит раздел 4; новый исходный код, без загрузки оригинального `gen_ff.dll`. |
| `gen_hotkeys.dll` | Global registration, binding editor, enable/conflict/persistence, media keys | Сочетания работают при фокусе другого приложения; конфликт явно виден; disable/unregister/restart корректны; foreground accelerators проверены отдельно. |
| `gen_ml.dll` | Media Library host, navigation/views, context menu/command routing, shared settings | Все 13 функциональных библиотечных разделов ниже доступны через рабочее дерево; переключение/закрытие не теряет состояние и не подменяется статическими заголовками. |
| `gen_tray.dll` | Tray controller/icons/buttons и настройки | Tray/menu/transport при скрытом главном окне, restore/minimize policies, изменение состояния playback, выход с удалением icon. |

### 3.6. Модули медиатеки — 13

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `ml_bookmarks.dll` | Persistent bookmarks, add/edit/remove/play/enqueue/menu | CRUD/restart/export fixture без потери URL/path/title; cancel удаления не меняет хранилище. |
| `ml_devices.dll` | Discovery/device cards, state/actions, shared views | Fixture-провайдер плюс подтверждённое реальное устройство; connect/disconnect/busy/error отражены, недоступные действия отключены по действительной причине. |
| `ml_disc.dll` | Disc browsing/ripping/metadata и только подтверждённые функции записи | Контролируемый disc corpus, track PCM и rip/cancel; нельзя активировать остаточные CD Burning ресурсы как доказательство работающей функции базы. |
| `ml_downloads.dll` | Queue, progress/cancel/retry/history и actual transfers | Контролируемый HTTP сервер: known payload hash, errors/redirect/interrupt, перезапуск; UI показывает реальные байты/скорость, не таймер. |
| `ml_history.dll` | Persistent playback history/count/resume/search/clear policies | Детерминированная последовательность playback/seek/end/restart даёт ожидаемые записи/счётчики; clear с подтверждением; playlist session не заменяет history. |
| `ml_impex.dll` | Library import/export, supported formats/metadata mapping | Собственная synthetic library round-trip всех заявленных полей, Unicode/duplicates/ratings/counts; unsupported legacy schema явно отклонена. |
| `ml_local.dll` | Indexed database, watched folders, smart views, artist/album filters, batch metadata | Add/change/remove fixture files отражены в индексе; запросы и sort дают known rows; restart/crash-safe persistence; tag writes проверены повторным чтением. |
| `ml_online.dll` | Service catalogue/editor/browser/accounts и разрешения | Контролируемые service pages/API: create/edit/delete/navigation/auth/error; неработающий исторический сервер помечен отдельно, а не заменён фальшивым успехом. |
| `ml_playlists.dll` | Multiple named saved playlists, CRUD/import/export/send-to/view menus | Несколько списков с Unicode/duplicates: reorder/rename/delete/restart, M3U/M3U8/PLS по заявленному контракту; selection/current-playback invariants. |
| `ml_pmp.dll` | Device host, transfer/sync/transcode queue, compatibility/capacity policies | Synthetic device fixture и реальное тестовое устройство; transfer hashes, playlists/metadata, cancel/disconnect/full-device/retry; personal devices не используются как расходный материал. |
| `ml_rg.dll` | ReplayGain analysis/tag writing and playback policies | Known PCM corpus: independently checked track/album gain/peak и tag round-trip; playback gain/clipping policies измерены; volume slider не заменяет scanner. |
| `ml_transcode.dll` | Real Format Converter, encoder selection/settings, queue/progress/naming | Каждая из пяти encoder-строк проходит через очередь; expected file/metadata, cancel/overwrite/error recovery, non-destructive source handling. |
| `ml_wire.dll` | RSS subscriptions, polling/downloads/episode state/storage | Контролируемые RSS fixtures, update/duplicate/malformed-feed/redirect, episode download hash; restart/unsubscribe/delete policies. |

`ml_disc` исследованной базы содержит сообщение о недоступных DriveInfo/CD
Burning функциях. Полная реконструкция не означает, что остаточный диалог
нужно превращать в вымышленную работающую функцию Winamp 5.8. Граница реального
поведения подтверждается runtime и фиксируется отдельно от новой возможности.

### 3.7. Транспорт устройств — 7

| Ключ | Рабочий backend и UI | Минимальный проверяемый результат |
| --- | --- | --- |
| `pmp_activesync.dll` | ActiveSync discovery/transfer/sync/settings | Реальное совместимое тестовое устройство/контролируемый протокол: enumerate, hash-verified transfer, cancel/disconnect и restart. |
| `pmp_android.dll` | Подтверждённый Android transport/discovery/sync и настройки | Тестовый Android endpoint/device: connect, metadata/playlists, transfer hashes, low-space/disconnect; выбор обычной папки не считается этим транспортом. |
| `pmp_ipod.dll` | iPod database/media/playlists/capacity protocol | Synthetic database round-trip и disposable test device: читаемые им tracks/playlists после sync; failure rollback без порчи базы. |
| `pmp_njb.dll` | NJB transport/discovery and shared device actions | Совместимое тестовое устройство либо protocol fixture с явной отметкой границы; реальные enumerate/transfer/delete/status checks. |
| `pmp_p4s.dll` | PlaysForSure transport, capabilities and permitted content operations | Совместимое устройство, enumeration/transfer/capacity/error; защищённый контент не выдаётся за проверенный и не обходится. |
| `pmp_usb.dll` | USB portable host, media mapping/queues/sync policies | Disposable USB filesystem fixture: transfer hashes, paths/playlists, capacity/disconnect/restart; обычное playback с USB не закрывает sync. |
| `pmp_wifi.dll` | Network discovery/pairing, async attach, transfer and device states | Контролируемый pair endpoint и реальный test peer: approve/reject/timeout/retry/unpair, transfer hashes; наличие строки URL `/pair` не восстанавливает протокол целиком. |

Локальное исследование трёх PMP callbacks установило только короткие числовые
dispatchers, не весь device contract. Device-present views, виртуальные методы,
pair responses и сторонние COM UI остаются отдельными исследуемыми условиями.
Без нужного устройства статус соответствующего end-to-end теста —
`BLOCKED FIXTURE`, а не `PASS` после одного mock-теста.

### 3.8. Внешние AVS APE — 7

Это AVS effect modules, не формат Monkey's Audio. Реализуются новые внутренние
эффекты; оригинальные APE не загружаются. Требования включают совместимое
сохранение/чтение параметров и участие в цепочке AVS, а не семь пустых диалогов.

| Ключ | Рабочая реализация и приёмка |
| --- | --- |
| `AddBorder.ape` | Подтверждённая геометрия/цвет/режим рамки; editor controls изменяют frames; golden input/frame и preset round-trip. |
| `colormap.ape` | Подтверждённый алгоритм отображения цветов, editor и preset transitions; контрольные RGB patterns и границы параметров. |
| `convolution.ape` | Подтверждённые kernel/edges/rounding/channel rules; impulse/checkerboard fixtures, editor/save/load и сравнение frames. |
| `fyrewurx.ape` | Восстановленная, не угаданная по имени семантика; deterministic seed/time/PCM fixtures и все установленные параметры UI. |
| `multifilter.ape` | Конечный реестр реально подтверждённых filter modes; каждый mode/параметр проверен image fixtures и preset round-trip. |
| `texer.ape` | Подтверждённые placement/blend/image-loading rules; безопасный image fixture, alpha/edge cases, preset и frames. |
| `texer2.ape` | Отдельно подтверждённый Texer2 contract, expressions/parameters; fixtures, сохранение и frames; не alias к Texer без доказательства. |

Восемь известных диалогов APE и 134 controls — статическая инвентаризация,
не описание всех алгоритмов/регистрации. Для каждой строки ещё нужны
подтверждённое registration name, параметрический contract и эталон исполнения.

## 4. Скины, Modern и MAKI

### 4.1. Невыполненная граница «все старые скины»

**Полного corpus всех исторических скинов нет.** Ни каталог Modern, ни
установщик 5.53 не содержат все когда-либо созданные WSZ/WAL/MAKI packages.
Невозможно подтвердить универсальное «все» проверкой нескольких образцов.

Для конечной приёмки создаётся manifest конкретных доступных packages с
SHA-256, форматом и ожидаемыми сценариями. Пока такой manifest не согласован,
полное покрытие исторических скинов имеет статус `UNKNOWN` и не объявляется
готовым. Форматы и VM могут тестироваться отдельно, но это не заменяет corpus.

### 4.2. Приёмка независимых skin/runtime компонентов

- Classic: архив/каталог, необходимые bitmap atlases, области/маски, font/text,
  palette, player/EQ/playlist/shade, размер/масштаб, hit testing, hover/pressed/
  disabled states, drag/docking/resize. Для каждого package — открытие, команды,
  восстановление состояния и screenshot comparison.
- Modern XML/XUI: include/resource resolution, groups/definitions/inheritance,
  layouts/containers, relative geometry, bitmap/font/color/theme definitions,
  controls/events, config bindings, window holders and native menu delegates.
  Неразрешённый include/unknown element — диагностируемая ошибка, не молчаливое
  игнорирование, замаскированное под успешную загрузку.
- MAKI: подтверждённый bytecode parser, imports/types/variables/strings/event
  bindings, opcode/stack/call semantics, object lifetime, event ordering,
  timers and configuration APIs. Нужны отдельные VM unit fixtures для каждого
  реализованного opcode/API, limits для ошибочного bytecode и end-to-end tests.
  Структурная расшифровка 39 скриптов не доказывает семантику opcode/ветвлений.
- Modern baseline: main normal/shade; playlist normal/shade/search popup;
  media library; video; AVS holder; album art; notifier и About group. Начальные
  и минимальные размеры сверяются с каталогом, не подменяются только общим
  прямоугольником 354 × 280.
- Drawer states: EQ, Options, Color Themes; семь Options sections, notification
  timing, songticker, shade linkage, vis-button actions, gamma/theme changes,
  animation/opacity и нужные transitions. Все параметры связаны с рабочим
  backend, сохраняются и отменяются по установленному contract.
- EQ: bands/preamp/balance/enable/presets и настоящее автоматическое применение
  per-track presets; нынешний `FLAT` не считается Auto. Crossfade проверяется
  PCM-переходом двух fixtures, включая seek/stop/end/next.
- Menus/accelerators: native delegate registrations, checked/enabled states,
  keyboard navigation, foreground/global distinction и одинаковый результат
  menu/button/shortcut для одной команды. Неизвестные delegate IDs сначала
  устанавливаются по поведению базы.
- Playlist advanced actions: search/history/result-limit policies, selection
  move/remove/sort/context menus, keyboard focus, all shade interactions и
  сохранение позиции/текущего элемента при изменениях.

Известные расхождения literal/XML config names и неиспользуемые include-файлы
сначала разрешаются подтверждением reachable runtime path. Нельзя считать
всякий файл на диске активным элементом Modern.

## 5. Нативные настройки, команды и интеграция

Реестр приёмки составляется из 357 известных dialog templates, 3 800 controls,
20 menu tables и 314 accelerator records, но это **не 357 обещанных рабочих
окон**. Нужна классификация каждого: reachable screen / child template /
дубликат / остаточный ресурс / informational surface / external component.
Удаление из проверки допускается только с сохранённым доказательством причины.

Для каждой reachable страницы фиксируются entry route, parent, tabs/order,
смысл каждого control, defaults/ranges, зависимые enabled/visible states,
Apply/Cancel/restart semantics, error validation, persistence и тесты.
Динамически построенные страницы дополняются независимо от resource count.

Отдельные обязательные группы:

- General: startup/instance/tray/priority/recycle policies, proxy/network,
  title formatting, UI/window/font/alpha/language settings;
- Playback: repeat как три состояния, shuffle, EQ/ReplayGain policies, seek,
  gap/crossfade и metadata/title updates;
- File types/association dialogs: пользовательский выбор и ограничения Windows,
  без самовольного захвата ассоциаций; проверка на disposable OS profile;
- Input/Output/DSP/Visualization/encoder pages: все 30 audio DLL keys,
  их зависимые pages и external property pages;
- Library/Devices: все 25 general/library/PMP keys, view registration,
  progress/error states и их реальные backend operations;
- Open/Save/URL/Time Jump/Jump to File/File Info: validation, Unicode/long paths,
  selection/focus, metadata/artwork read/write и безопасные error paths;
- About/help/credits: собственное MdAmp branding; функциональные ссылки и
  shortcuts, без ложного указания исходного приложения или версии;
- Installer/updater/uninstaller/helper UI: отдельный новый implementation
  contract; residue templates не запускаются как доказательство функции.

Команда считается реализованной только после проверки изменения состояния и
результата, а не после её появления в меню. Winamp `WM_COMMAND`/IPC names могут
служить ключами сопоставления, но полный IPC contract в отчёте не восстановлен.
Самостоятельная реализация не обязана выдавать несуществующую DLL/IPC
совместимость. Если публичная IPC-совместимость заявляется позднее, для каждого
сообщения требуются payload/result/lifetime tests и версия контракта.

## 6. Подтверждение пиксельного соответствия

**Сейчас не подтверждено.** Предыдущие попытки захвата оригинального окна
завершались timeout. Атлас wireframes и stock preview PNG не являются живыми
эталонными скриншотами. Нельзя заменять новый захват изображением из отчёта.

Для каждой принятой screen/state пары сохраняются:

1. Идентичные Windows build/theme, DPI/scale, language/locale, fonts, display
   geometry, GPU/render profile; версии приложений и fixture manifest.
2. Конкретные window/client sizes, позиция, focus/hover/pressed/enabled states,
   открытое меню/вкладка, deterministic playback/clock/seed и входные данные.
3. Два исходных lossless PNG без resize/retouch и отдельный diff PNG; SHA-256,
   command/scenario ID, timestamp и фактические capture settings.
4. Машинный результат: сравниваемый rectangle, число изменённых пикселей,
   maximum channel error и явно перечисленные masks/exclusions. Для буквального
   pixel match нужен zero diff в сравниваемой области; tolerance не называется
   нулевым расхождением.
5. Человеческая проверка clipping, font alignment, hover/disabled art и всех
   интерактивных путей. Один PNG главного окна не закрывает остальные состояния.

### Невыполненное противоречие whole-frame zero diff

**Название и branding MdAmp намеренно отличаются от Winamp.** Поэтому буквальное
нулевое расхождение всего кадра, содержащего эти надписи/логотипы/версии, нельзя
объявить при сохранении нового branding. Допустимый конечный критерий должен
честно различать:

- полное whole-frame zero diff — не выполнено при различающемся branding;
- layout/state/pixel match вне заранее перечисленных brand/version regions —
  отдельная ограниченная метрика с опубликованной маской, не «полностью один
  в один»;
- геометрическое/функциональное соответствие с иной графикой — ещё одна
  метрика, не pixel identity.

Нельзя автоматически скрывать все отличающиеся области или выбирать маску
после сравнения ради прохождения теста. Динамические clocks/визуализации
фиксируются входом/временем; недетерминированность явно блокирует zero-diff
приёмку соответствующего состояния.

## 7. Требуемые fixtures и итоговый gate

Нужны synthetic/licensed test media для каждого заявленного формата, tags/art,
HTTP/RSS/auth/pair test endpoints, disposable filesystem/database/device states,
совместимые optical/portable/MIDI/audio devices, graphics/display configurations,
skin/preset corpus и подтверждённые reference screenshots.

Mocks проверяют изолированные алгоритмы, но не считаются наблюдением реального
устройства, доступности исторического сервиса или эквивалентности DirectShow/
COM/GPU поведения. Нет endpoint/device/reference capture — соответствующая
проверка остаётся открытой. Не допускаются декоративные progress bars,
синтетические «подключённые устройства» или успешно завершённые сетевые
операции без фактического результата.

Итоговая версия принимается только когда:

- все 62 ключа имеют backend/UI/positive/negative/persistence test evidence;
- достигнуто покрытие согласованного skin/MAKI corpus и reachable native UI;
- опубликованы парные screenshot artifacts и точная, непротиворечивая pixel
  метрика для каждого заявленного состояния;
- Debug/Release tests и предыдущие audio/storage/DSP regressions проходят;
- обновлены обе текущие матрицы с реальными результатами, без превращения
  `UNKNOWN`, `PARTIAL` или пропущенного hardware test в выполненную функцию;
- scope exclusions/branding differences/недоступные fixtures перечислены
  открыто; при их наличии абсолютное «всё повторено» не заявляется.

Текущий итог: **требования зафиксированы; полная реализация, corpus и пиксельная
приёмка ещё не выполнены**.
