# MdAmp: instructions

- Read README.md and docs/architecture.md before changing code.
- Keep this independent C++17 / Win32 project separate from other repositories.
- Preserve existing user changes; inspect git status and the complete scoped diff.
- Do not copy Winamp binaries, artwork, skin XML, MAKI, plugin code or decompilation.
  Rewrite functional requirements as observations and implement original code/assets.
- Distinguish implemented, planned and runtime-unverified behavior.
- Unsupported controls must remain disabled or explicitly report not implemented.
- Run `build.bat` and `build.bat -Configuration Debug` for changed native code.
  These run unit checks and an isolated hidden-window smoke test.
- Verify real playback and user-visible UI separately when those features exist.
- Do not add personal media, playlists, settings, local evidence bundles, credentials,
  machine paths or build outputs to Git. Use synthetic fixtures.
- Do not choose a distribution license, publish releases, configure secrets,
  change repository permissions or force-push without explicit owner authorization.
