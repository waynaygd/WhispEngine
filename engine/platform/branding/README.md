# Application icon — temporary placeholder

No existing WhispEngine logo was found in the repository. The neutral W is an
explicit **placeholder**, not a newly established logo. It contains only a
hand-defined W polygon and a dark rounded background; no fonts or external art.

`generate_icon.py` is the editable source. Running it with Python + Pillow
generates `WhispEngine.ico`, the 256px PNG preview and `GeneratedIcon.h` from the
same RGBA artwork at 16, 24, 32, 48, 64, 128 and 256 pixels. These outputs are
checked in, so ordinary builds need neither Python nor Pillow.

The Windows-only RC source embeds all seven ICO frames into WhispEngine.exe
for Explorer. GLFW receives the same seven PNG frames, decoded with the existing
stb_image dependency immediately after window creation. GLFW copies the RGBA
pixels; temporary decoded data is freed before Create returns. No runtime file
lookup, new dependency, extra native WM_SETICON override or per-frame work is
introduced. This shared GLFW path applies to DX12 and Vulkan. Existing titles
and project/diagnostic title updates are retained. No explicit AppUserModelID
is added: the normal executable grouping does not require one.

To replace the placeholder, update the generator artwork and regenerate all
outputs together. Existing pinned shortcuts and Explorer's icon cache may retain
an older executable icon until Windows refreshes them; no user cache is deleted.

## Validation (2026-10-21 / 2026-10-22)

- Release WhispEngine built successfully with DX12 and Vulkan enabled.
- All seven ICO entries decode as RGBA at the requested sizes; all seven exact
  compressed frame payloads were also found in the built Windows executable.
- The real Release application was visually inspected in Windows:
  the same W appears in the title bar, Windows taskbar, Alt+Tab switcher and
  Explorer's large-icon view of WhispEngine.exe. Existing FPS/backend/dt title
  information remains intact.
- ProjectSystemDx12Regression passed (15.77 s), including project startup/switch
  and an explicit zero DX12 ERROR/CORRUPTION validation count. No full engine
  suite was run. Log: `out/editor2d1-project-test.log`.
- The same Release executable started with Vulkan, loaded its shaders and exited
  normally after 30 frames. Vulkan's existing editor UI is unavailable; this
  change does not add Vulkan editor rendering. Config was restored after this
  isolated run. Log: `out/build/x64-release/editor2d-app/icon-vulkan.log`.
- Debug was attempted, but the build stopped on existing compiler error C1128 in
  LuaRuntime.cpp (requires /bigobj). This unrelated compilation setting was not
  changed; a successful Debug executable run is not claimed.
- Windows 11 was inspected directly; Windows 10 and non-Windows builds were not
  available for visual testing. RC compilation is guarded by WIN32; the resource
  has no configuration-specific branding, so Debug uses the same artwork.

Gameplay assets, renderer passes, window titles and unrelated user edits were
left unchanged. No branch switch or commit was made.
