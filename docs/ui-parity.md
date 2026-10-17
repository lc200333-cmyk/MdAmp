# Modern-inspired player: implementation boundary

MdAmp's player is newly written C++/GDI, not a converted Winamp skin.
No Winamp artwork, fonts, XML, MAKI bytecode, executable code or logos are
distributed. This document describes the custom renderer only; playback and
other windows are documented separately.

## Evidence used

The supplied `rea-clone-modern-evidence/skin/report.md`, `controls.json` and
`colors.json` establish a 354-unit-wide main window, 354 x 25 windowshade,
354 x 280 expanded layout, a 116-unit drawer adjustment, transport commands,
seek/volume, three-state repeat, independent shuffle, and the Equalizer,
Options and Color Themes drawer tabs. The equalizer inventory establishes
preamp, ten vertical bands, balance, enable, auto and presets controls.
The report explicitly does not establish all dynamic menu behavior or MAKI
control flow. Those are not claimed reproduced here.

## Newly implemented geometry

The main coordinate system is 354 x 164 collapsed, 354 x 280 expanded, and
354 x 25 shaded. The application scales this logical space for DPI/user zoom.
All pointer hit rectangles come from `PlayerHits`; painting and hit-testing
share the same geometry. The titlebar is custom-drawn with minimize, shade
and close controls. A five-entry menu row occupies y=25..39, leaving the
application responsible for actual menu dispatch and keyboard navigation.

The song display has a large elapsed-time field, title, stream information,
transport status and a spectrum field supplied by the audio engine. It does
not manufacture animated bars when no spectrum data is supplied. Transport
includes previous, play, pause, stop, next and open; adjacent controls expose
volume, shuffle and repeat. Playlist and library toggles remain in the display.
The time display, typography, button outlines, track backgrounds, bevels and
icons are independently drawn with Windows GDI primitives/system fonts.

The EQ drawer exposes balance, preamp and frequencies 60, 170, 310, 600, 1k,
3k, 6k, 12k, 14k and 16k Hz, matching MdAmp's actual DSP filter centers.
The gain range is -12..+12 dB. Actual
processing is performed by MdAmp's audio engine rather than the renderer.
The renderer emits commands for enable, flat reset and presets. The internal
`EqAuto` action name is retained for the host interface, but its visible label
is FLAT and the host resets the bands; this is not Winamp's per-track automatic
preset behavior. A drawn control alone is not proof of audio integration.

Options contains real application command targets for Preferences, Playlist,
Shuffle and Repeat rather than pretending all legacy drawer settings work.
Themes contains previous, next and selection command targets and six original
palettes: Modern Silver, Midnight Blue, Phosphor, Amber, Violet and Sea Glass.
This is not the 44 legacy gamma presets and does not import their definitions.

Windowshade retains previous/play/pause/stop/next, current title, volume and
the three titlebar controls. Moving/resizing, menus, accelerators, persisted
state and separate native windows remain the host application's responsibility.

## Known non-parity

This renderer preserves the principal arrangement and compact workflow, not
pixel identity. It does not implement translucent irregular skin regions,
sprite-based hover/pressed artwork, MAKI animations, notifier windows,
legacy color gamma processing, EQ crossfade controls, or the complete seven
legacy options subpages. It is not a Winamp XML/WSZ/WAL skin interpreter.
Third-party skins and visualization plug-ins require separate compatibility
work and cannot be inferred from similarity of the main window.

No live pixel-comparison claim is made: the original investigation's desktop
capture timed out. A build test and logical hit-map checks do not replace
visual inspection, keyboard/accessibility testing or audio integration tests.

## Major-screen parity matrix

This is a scope ledger, not a claim that the reference application's 357
dialog templates have been implemented. "Partial" means a working MdAmp
alternative exists but the reference surface, options or semantics differ.
"Absent" means there is no corresponding implementation, not a hidden or
placeholder implementation. Screen names are taken from the supplied REA
reports; behaviors below are checked against MdAmp's current source.

| Reference surface | MdAmp v1 status | Implemented behavior / explicit gap |
| --- | --- | --- |
| Modern main player | Partial | Custom silver/blue compact player, seek, volume, transport, title/time/status, live spectrum; not sprite-identical. |
| Main File/Play/Options/View/Help | Partial | Real native popup commands, themes and checked states; original dynamic plug-in menu registry absent. |
| Windowshade | Partial | 354 x 25 transport/title/volume/chrome; no expandable shade groups, separate shade seek, library/playlist buttons or theme slots. |
| Equalizer drawer | Partial | Audible ten-band EQ, preamp, balance, enable, five newly defined presets; FLAT resets rather than automatic per-track presets. |
| Crossfade drawer controls | Absent | No track crossfade, crossfade time slider or increment/decrement buttons. |
| Options drawer | Partial | Preferences, playlist, shuffle and repeat shortcuts; not the seven original drawer subpages. |
| Options Drawers/Menus/Windowshade/Vis Button | Absent | No MAKI drawer animation preferences, menu visibility buckets, shade linkage policy or context/config selection setting. |
| Options Notifications/Songticker/Misc | Absent | No notification timings, marquee modes, detach-video settings or beat overlay toggles. |
| Color Themes drawer | Partial | Six original palettes with previous/next/select and menu selection; not 44 gamma presets. |
| Playlist Editor | Partial | Separate window, query, numbered titles, selection, play, append/remove/clear, ordering, UTF-8 M3U import/export; native system controls rather than original skin widgets. |
| Playlist windowshade | Absent | Only main player supports shade. |
| Playlist search and Jump to File | Partial | J focuses live title/path filtering; no separate search-results popup, history, 500-result policy or advanced move-selection actions. |
| Playlist footer and context menus | Partial | File/directory append, select, sort, remove, load/save and context ordering; no original full File/Playlist/Sort/Help menu set. |
| Media Library host | Partial | Separate folder scan and title/path list, play by double-click and append all; no original left navigation/filter host. |
| Local Media/smart views | Partial | WAV/MP3/FLAC folder scan only; no persistent indexed database, tag-column filters or smart-query editor. |
| Saved playlists / import-export manager | Partial | Session playlist plus user-chosen M3U files; no saved-playlist library, PLS, or named-playlist management screen. |
| Bookmarks / History | Absent | No bookmark editor, playback history or resume history UI. |
| Album Art / Now Playing artwork | Absent | No independent album-art container, embedded artwork view or art search. |
| Video / fullscreen | Absent | No video decoding, video canvas, subtitle controls, video sizing or fullscreen playback. |
| Visualization | Partial | Independent live 32-band spectrum; no AVS effect editor/presets or MilkDrop renderer/configuration. |
| Notifier | Absent | No desktop notifications, artwork bubbles, fade timing or nonactivating overlay. |
| Preferences host | Partial | Native tree with eight MdAmp pages and apply/close, not the original composed Winamp/plugin preference tree. |
| General Preferences | Partial | Always-on-top and drawer visibility plus save behavior; no proxy, startup, multiple-instance, tray policy, priority or recycle settings. |
| File Types / OS associations | Absent | Files can be opened/dropped; no extension ownership, icon registration or OS-association editor. |
| Titles / metadata formatting | Absent | Filename-derived titles/basic decoder information; no editable advanced title formatting or metadata read policies. |
| Playback / ReplayGain / EQ engine setup | Partial | Volume, balance, shuffle, three-state repeat, EQ and preamp; no ReplayGain scanning/playback policy, limiter-type or EQ-engine selector. |
| Skins / Modern / Classic preferences | Partial | Six palettes and player scale 100/200%; no Classic mode, XML/MAKI loader, skin installer or external theme package support. |
| Modern alpha/window/font settings | Absent | No desktop alpha, opacity/fade policies, FreeType, font mapper or linked-window scaling policy. |
| Localization | Absent | No language-pack loader or localization preference page. |
| Keyboard / Global Hotkeys | Partial | Documented foreground shortcuts; no system-wide hotkey registration, editor or original complete accelerator set. |
| File Info / metadata / tags / artwork editor | Partial | Basic active-file path, format and duration information; no tag writing, artwork editing, batch metadata or Auto-Tagger. |
| Open File / Folder / Load-Save playlist | Partial | Standard Windows dialogs; no original URL, time-jump or playlist-entry editing dialogs. |
| Input plug-in configuration | Partial | WAV/MP3/FLAC decoding via bundled miniaudio; no individual legacy input DLL preference pages. |
| MIDI / tracker / Ogg / WMA / NSV / CD input | Absent | No matching decoder-specific features, hardware/SysEx controls, chained-stream navigation or legacy format configuration. |
| Output plug-ins | Partial | WASAPI stereo output; no DirectSound/WaveOut/DiskWriter devices, buffer/fade policies or output-module selector. |
| DSP plug-ins | Partial | Own MdAmpStereo.dll with width slider, separate MdAmp DSP ABI v1; no Winamp DSP loader or SPS expression editor. |
| Encoders / Format Converter | Absent | No LAME/FLAC/Vorbis/WAV/WMA encoding settings, transcode jobs or progress screens. |
| AVS and seven APE effect modules | Absent | No effect tree, expressions, image/text effects, color maps, convolution or APE binary compatibility. |
| MilkDrop | Absent | No preset browser, shader/equation editor, fullscreen/desktop mode or dual-head configuration. |
| Devices / Portables | Absent | No Android, USB, iPod, ActiveSync, Wi-Fi discovery/pairing, sync or device-transfer manager. |
| CD ripping / disc metadata | Absent | No optical drive controls, CD Text/lookup or rip progress. |
| Online Services / Podcasts / Downloads | Absent | No network service browser/editor, subscriptions, permission prompts or downloads queue. |
| Tray / error reporting | Absent | No tray plug-in configuration, crash sender or SMTP configuration. |
| About / shortcuts | Partial | MdAmp version, independent implementation notice and shortcut page; no imported credits, branding, Winamp history or original About animation. |
| Setup / installation / service progress | Absent | No reimplementation of original setup wizard, updater, uninstaller or legacy helper progress dialogs. |

The supplied analysis itself leaves original dynamic UI paths and MAKI branch
conditions unverified. Completing this matrix would require new feature work
and runtime acceptance tests; static resource inventory is not a substitute
for the input codecs, device protocols, renderers or network services behind
those screens.
