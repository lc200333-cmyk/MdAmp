# Проверка первой функциональной версии

17 октября 2026. Windows x64, MSVC 19.44.35228, CMake 3.31.6,
Windows SDK 10.0.26100.0. Исходный Winamp не изменялся.

## Автоматические проверки

`build.bat` и `build.bat -Configuration Debug`: exit 0, по **6/6 CTest**:

- `mdamp.unit`: формат времени и bounds layout;
- `mdamp.window_smoke`: настоящие HWND, восемь страниц Preferences, renderer
  всех тем/табов в memory DC, согласованность hit-regions;
- `mdamp.audio`: synthetic WAV / MP3 / FLAC, sample-level EQ, ошибки и Close;
- `mdamp.storage`: Unicode/BOM/EXTINF/relative paths, атомарная запись,
  повреждённые данные, finite/clamp, лимиты и отсутствие потери старого файла;
- `mdamp.plugin`: загрузка настоящей MdAmpStereo.dll, ABI и обработка PCM;
- `mdamp.version`: чтение/увеличение patch, отказ при неверной версии.

Отдельно выполнен `mdamp_audio_tests --audio-smoke` на синтетическом WAV через
реальный default WASAPI endpoint: PCM signal/cursor, pause, seek, resume, stop,
EOF/replay и повторное открытие с конкурентным snapshot/settings — exit 0.
Это не акустический loopback, MP3/FLAC проверены декодированием offline.

`MdAmp.exe --integration-test <synthetic.wav>`: exit 0. Проверена настоящая
связка controller/GUI/audio/DSP: игра duplicate-entry, корректный reorder,
pause→clear→pause без старого звука, сохранение старого списка при oversized
replace, удаление текущего трека с закрытием decoder, EQ presets.
Эти аппаратные тесты не включены в CI, где audio endpoint может отсутствовать.

Fixtures генерируются самим тестом в отдельной временной папке, не являются
личными медиа и не коммитятся. Пример локального запуска:

```powershell
.\build\Release\mdamp_audio_tests.exe --write-fixture "$env:TEMP\mdamp-test.wav"
.\build\Release\mdamp_audio_tests.exe --audio-smoke "$env:TEMP\mdamp-test.wav"
$probe = Start-Process .\build\Release\bin\MdAmp.exe -ArgumentList '--integration-test',"$env:TEMP\mdamp-test.wav" -WindowStyle Hidden -PassThru -Wait
$probe.ExitCode
```

## Runtime/UI

Computer Use подтвердил запущенное новое окно, Playlist Editor, открытие
Preferences через Ctrl+P и дерево восьми страниц. Захват screenshot снова
завершился `window capture timed out`; координатный click недоступен. Строгая
визуальная проверка и полное ручное прохождение всех сценариев НЕ выполнены.

## Непроверенное / неготовое

`git diff --check` собственного кода проходит. Полный diff-check сообщает
11 trailing-whitespace строк в неизменённом upstream `miniaudio.h`; они оставлены
как есть для сохранения SHA-256 оригинального header. Это известное замечание
поставляемой зависимости, а не проходящая проверка полного diff.

Нет акустической оценки качества, clean-machine/install тестов, всех форматов,
всех скинов и старых плагинов. Протоколы устройств/online/video/AVS отсутствуют.
Полный список — `ui-parity.md` и `plugin-coverage.md`. Результат hosted Actions
проверяется отдельно после push; локальный успех не означает hosted success.
