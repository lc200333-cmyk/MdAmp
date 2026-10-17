# Strict pixel comparison

`MdAmpPixelDiff` compares two supplied PNG frames. It does not capture windows,
identify which program produced a picture, or establish application equivalence.
No Winamp reference screenshots are supplied or validated by its self-test.
An owner-supplied reference was subsequently compared with an explicitly
synthetic renderer fixture: **different**, not a verified live pair. See
`modern-reference-progress.md`. The owner requires no pixel exclusions.

```text
MdAmpPixelDiff.exe reference.png candidate.png NEW-output-directory
MdAmpPixelDiff.exe --self-test
```

The output directory must not exist; its parent must already exist. Inputs are
opened read-only. Both inputs are completely decoded and hashed before the tool
creates the output directory. Existing outputs are never overwritten. If writing
fails, an incomplete new output directory may remain; retry with another new path.

Only PNG files of at most 128 MiB and at most 16,777,216 pixels are accepted.
Zero dimensions and decoded size/stride overflows are rejected before allocation.
Comparison uses WIC-decoded, straight (not premultiplied) 32-bit BGRA pixels,
including alpha. There is no resizing, alignment, cropping, masking, tolerance,
color-profile transformation, or exclusion of dynamic/title/branding pixels.
This is a decoded-frame comparison, not a comparison of PNG compression or
metadata; the report separately retains SHA-256 of each complete encoded file.
Animated behavior is not tested.

Exit statuses:

- `0`: dimensions and every decoded BGRA channel are identical.
- `1`: at least one channel differs, or dimensions differ.
- `2`: invalid arguments, rejected input/output path, decode/hash/write failure.

`report.json` records dimensions, encoded-file SHA-256, changed pixel count,
maximum channel delta, and mean absolute delta over **all four channels** of all
pixels. Dimension mismatches produce null difference metrics, because the tool
does not resize or invent an alignment. For equal dimensions, `heatmap.png` is
opaque red for changed pixels and opaque black for unchanged pixels. It is not
an overlay. No heatmap is produced for a dimension mismatch.

`pair_pixel_equal` reports only the pair's exact decoded pixel result. Both
`screenshot_provenance_verified` and `full_application_equivalence_verified` are
**always false**, even for an identical pair. Identical input filenames/images
are not evidence that two applications rendered equivalent windows.

## Obtaining meaningful reference pairs

Use lossless live captures from the independently identified reference Winamp and
MdAmp executables. Retain executable/skin hashes, capture method, OS/build,
Windows theme, fonts, DPI, desktop scale, application scale, exact client bounds,
skin/theme, media metadata and fixed playback state. Match focus, mouse position,
hover/pressed/disabled state, selection, scroll position and animation time.

Record each pair's screen and action sequence. Compare separate cases for normal
and shade windows, drawers, menus, preferences, playlists, plugin dialogs,
resize/DPI choices and relevant states. A result covers exactly its captured
frame pair, not other screens, skins, functionality or state transitions. A capture
timeout means pixel confirmation is still blocked; source counts, geometry tests
or a stock preview thumbnail do not substitute for reference captures.

## Synthetic self-test

The self-test generates its own small PNG fixtures in a new uniquely named
temporary directory. It checks identical pixels, a one-channel RGB difference,
an alpha-only difference, differing dimensions, missing/invalid input, oversized
PNG headers, existing-output rejection, and heatmap colors. Artifacts are retained
at the printed temporary path for inspection. They are not Winamp goldens and
must never be labelled Winamp screenshot proof.

Standalone MSVC build (Developer Command Prompt):

```text
cl /nologo /EHsc /W4 /WX /std:c++17 /utf-8 tools\pixel_diff.cpp /Fe:MdAmpPixelDiff.exe /link windowscodecs.lib ole32.lib bcrypt.lib
MdAmpPixelDiff.exe --self-test
```
