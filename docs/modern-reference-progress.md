# Modern reference iteration — incomplete

2026-10-17. This is an independently drawn implementation, not original skin
assets. Full Winamp, skin, plugin or pixel parity is **not achieved**.

## Implemented in this iteration

- Horizontal main-window resizing, 17-pixel titlebar and 16-pixel menu row,
  silver/navy display and circular transport, centered fixed-width EQ panel,
  bottom drawer tabs. Paint and pointer maps share logical coordinates.
- Playlist docked below the player, default 487 x 976 logical pixels, own chrome
  and real Edit/ListView controls. Known EXTINF/playback duration is shown at
  the right. Unknown duration is not invented; M3U roundtrips retain it as -1.
- Functional Add/Rem/Sel/Misc/Manage popup actions, selection inversion/clear,
  playlist shade, persisted size/docking flag and scale-aware control fonts.
  Existing explicitly saved scale is retained; new profiles default to 100%.
  Detached window position and playlist shade are not restored across restarts;
  dimensions and docking choice are persisted, not a complete layout snapshot.
- Correct transformed GDI clipping at translated origins and 100/200% zoom.
- Regression coverage for native layout/search/durations, long-list scrollbars,
  main/playlist shade, detached shade scaling and library docking below playlist.

No original DLL, MAKI/XML, sprites, fonts or logo asset was imported. Six palettes
remain our own, not the full skin/theme collection. AUTO remains disabled.

## Strict supplied-frame check: FAILED

Owner supplied a PNG, 510 x 1276, including desktop margins around the windows.
Encoded SHA-256:
`856b1ebb07e6463beabd1bc10815e60b49eb0dd466f658e161695946c9f0e4f4`.

`MdAmpRenderPreview NEW-output.png` produces an explicitly synthetic frame of
the production GDI painters: same 510 x 1276 canvas, player at (8,5), 487 x 280,
playlist at (8,286), 487 x 976. It draws a synthetic two-second bimbim entry.
Search/list text is painted as fixture text, **not captured native child HWNDs**.
Unrendered desktop margins remain black. No original reference pixels are copied.
The original PNG is not modified, cropped, rescaled or distributed.

Strict `MdAmpPixelDiff` result for this fixture (not a live application capture):

| Field | Result |
| --- | --- |
| Candidate encoded SHA-256 | `c8d0759befe17a9dacc76332008126c2bf4e2cf7064d5846f924ebb98f1dc2ac` |
| Dimensions equal | true |
| Changed pixels, all BGRA channels | 238007 of 650760 |
| Maximum channel delta | 255 |
| Mean absolute channel delta | 13.294474537463888 |
| Comparator exit | 1 (different) |
| Pair pixel equal | false |
| Screenshot provenance verified | false |
| Full application equivalence verified | false |

No masks, exclusions, tolerance or automatic alignment are used. In particular,
MdAmp/WINAMP title text, changing data and desktop margins count as differences.
The owner explicitly rejected exceptions. Equal background pixels do not imply
implemented screens, equivalent behavior or any percentage of application parity.
Raw reference/preview/heatmap remain local and are not repository assets.

## Still required

Live candidate capture and input/accessibility checks remain unverified: two
fresh selected-window captures failed with a timeout, after which retries were
stopped under the Computer Use recovery procedure. The supplied reference PNG
does not solve live MdAmp capture. Original hover/pressed artwork, exact fonts,
menu/dialog content and rendering, all old skins and independent AVS/MilkDrop
and plugin implementations remain open in `ui-parity.md` and
`plugin-coverage.md`. This iteration must not be labelled a complete clone.
