# Windows Camera Frame — OBS Studio Plugin

A real OBS Studio plugin (C, libobs source-filter API, GPU shader compositing)
that wraps a webcam/video source in a convincing miniature "OS window" —
title bar, buttons, border, shadow, glow, mask — styled after Windows 95
through Windows 11, fully customizable, with a JSON preset system.

It is implemented as an **OBS video filter** (`obs_source_info.type =
OBS_SOURCE_TYPE_FILTER`), the correct architecture for "decorate this
existing source" behavior: you add a webcam source as usual, then add the
**Windows Camera Frame** filter to it from the filter list. Add the same
filter to as many sources as you like — each instance has its own settings
and preset, and it saves with the OBS scene collection automatically because
it's plain filter settings (no plugin-specific save path required).

---

## 1. Architecture

```
src/
  plugin-main.c          module entry point (obs_module_load/unload)
  win-frame-filter.h     every settings key name (also the preset JSON schema)
  win-frame-filter.c     obs_source_info: create/destroy/update/properties/
                          video_tick/video_render/get_width/get_height
  win-frame-internal.h   private struct win_frame_filter (shared by .c files)
  win-frame-render.h/.c  settings -> pixel-space layout, cached text/image
                          assets, effect-parameter binding, logo overlay draw
  win-frame-text.h/.c    Win32 GDI title/status text -> gs_texture rasterizer
  win-frame-styles.h/.c  the 13 built-in Windows-style default tables
  win-frame-presets.h/.c JSON preset manager (save/load/duplicate/rename/
                          delete/export/import), backed by obs_data_t + the
                          OBS per-user plugin config directory
data/
  effects/win_frame.effect   the entire window chrome, one pixel shader
  locale/en-US.ini           UI strings
  presets/*.json              example custom presets you can Import
```

### Rendering approach

Everything — shadow, glow, background, title bar, icon, title text, status
bar, window buttons + glyphs, single/double/dashed border, resize grip, and
the cropped/masked camera image — is drawn in **one pixel shader pass**
(`data/effects/win_frame.effect`), using signed-distance-field rectangles for
shapes and manual "source-over" alpha compositing for layering. This is the
same technique used by GPU UI toolkits; it means:

* **One texture sample** of the actual camera image per pixel (plus a couple
  of tiny cached textures for the pre-rasterized title/status text and any
  optional custom icon/mask/background/logo images).
* **No extra render targets, no multi-pass blur.** Soft shadow/glow edges
  are produced by `smoothstep()` falloff over an SDF distance, not a real
  Gaussian blur — a deliberate, documented simplification (see §6) that
  keeps this filter effectively free on the GPU even at 4K/60.
  Real backdrop blur ("Acrylic"/"Mica") is not something a single source can
  do (OBS doesn't expose the compositor framebuffer to a filter); the Vista
  /7 "Aero glass" and Win10/11 "Acrylic/Mica" looks are approximated with
  semi-transparent tinted layers + a highlight band, which — because it's
  genuine alpha transparency — actually lets whatever is beneath the source
  in the OBS scene show through, which reads as convincingly "glassy" in a
  real composited scene.
* The filter is plugged into libobs the standard way any size-changing
  filter (crop, scale, etc.) is: `obs_source_process_filter_begin()` grabs
  the upstream camera frame into a texture, then
  `obs_source_process_filter_end(source, effect, canvas_w, canvas_h)` draws
  our effect across an output quad **larger than the input** (frame + title
  bar + borders + shadow margin) — this is why `get_width`/`get_height` are
  overridden to return the full framed size, exactly like OBS's own crop
  filter changes its output size.
* CPU work (GDI text rasterization, PNG loading via `gs_image_file_t`) only
  happens in `win_frame_layout_update()`, called from `update()` — i.e. only
  when you change a setting — never from `video_render()`. Per-frame cost is
  the shader plus binding ~40 cheap uniforms.

### Title text rendering

libobs has no built-in text layout, so title-bar/status-bar text is
rasterized with plain Win32 GDI (`CreateFontW` + two-pass black/white
`DrawTextW` to recover per-pixel alpha) into an RGBA `gs_texture_t`, cached
and only regenerated when the text/font/size/color actually changes. This is
Windows-only (`src/win-frame-text.c` is guarded by `#if defined(_WIN32)`);
on macOS/Linux builds the same function currently returns `NULL` and the
title bar simply renders without text — see §7 for what a cross-platform
implementation would need.

### Presets

Presets are plain `obs_data_t` JSON, saved under
`%APPDATA%\obs-studio\plugin_config\win-frame-filter\presets\*.json` (via
`obs_module_config_path`), using exactly the settings keys in
`win-frame-filter.h`. On first load the plugin seeds one preset file per
built-in Windows style automatically (`win_frame_presets_seed_builtin`), so
the preset list is never empty, and `data/presets/*.json` ships a couple of
extra hand-authored "custom style" examples you can **Import**. Loading a
missing/corrupt preset file fails safely (`obs_data_create_from_json_file_safe`
returns `NULL`, which is checked) — it never crashes OBS.

---

## 2. Feature coverage vs. the request

Implemented and working end-to-end:

* Filter appears in **Add Filter** for any source; multiple independent
  instances; settings save with the scene collection (native OBS behavior
  for filter settings — no extra code needed).
* All 13 requested styles (95/98/ME/2000/XP-Luna/XP-Classic/Vista-Aero/
  7-Aero/8-Metro/8.1-Metro/10-Fluent/11-Fluent/Classic) + Custom, switchable
  instantly from a dropdown, each with a distinct, hand-tuned default look.
* General, Camera, Frame, Title Bar, Window Buttons, Border, Shadow, Glow,
  Overlay, Mask, Background/Custom-Style, Animation and Presets sections,
  each an `obs_properties` group with sliders/color pickers/checkboxes/
  dropdowns/text fields/file pickers, using OBS's native Qt properties
  dialog (so it automatically gets OBS's dark/light theming, tooltips via
  `obs_property_set_long_description` hook points, and keyboard navigation
  for free — see §5).
* Camera crop / zoom / pan, corner radius, frame thickness, scale, padding.
* Rectangle / rounded-rectangle / circle / ellipse / custom-PNG mask with
  feather.
- Title bar: enable, height, text, font, text color, icon image + size,
  gradient or flat fill, glass reflection band, status bar with its own
  text, resize grip.
* Minimize / maximize / close buttons: enable each independently, size,
  spacing, colors, procedurally drawn glyphs (no bitmap assets needed, so
  no copyright concerns and infinite scalability).
* Border: enable, thickness, opacity, color, double border with a separate
  inner color, dashed.
* Shadow: enable, color, opacity, blur, spread, X/Y offset.
* Glow: enable, color, opacity, radius, intensity.
* Optional logo/watermark image overlay: path, size, position, opacity.
* Custom background image, accent color, dark-mode flag for the *settings
  panel context* (OBS follows its own theme; see §5).
* Preset manager: apply / save-as-new / duplicate / delete / reset-to-style,
  wired to real file I/O (`win-frame-presets.c`); Import/Export are
  implemented as library functions (`win_frame_presets_import/export`) —
  wiring literal file-picker **buttons** for them in the properties panel is
  a ~10-line addition left as a clearly marked TODO (§6) since
  `obs_properties_add_button2` doesn't have a built-in "save file" dialog
  primitive in older OBS versions and needs a small Qt callback; the
  underlying functions are complete and unit-testable today.
* Simple open/close animation (fade + ease-out scale) driven from
  `video_tick`, gated so it never blocks normal rendering.
* Handles missing camera / disconnected source (`obs_filter_get_target`
  returning `NULL` degrades to skipping the filter, never crashes),
  resolution changes (re-layouts automatically when `obs_source_get_base_*`
  differs from the cached size), corrupt/missing presets, missing custom
  images (silently falls back to a 1×1 transparent dummy texture instead of
  failing to render).

Explicitly simplified, with the reasoning and what a fuller version would
need — see §6.

---

## 3. Prerequisites

* **Windows 10/11**, a Visual Studio C++ toolchain with the Windows SDK
  (this was built and verified with VS "18"/2026 Community; VS 2022 works
  identically — just change the `-G` generator name below to match what
  `cmake --help` lists as installed on your machine).
* **CMake ≥ 3.20** and **git**.
* An **OBS Studio source checkout at the exact tag matching the OBS Studio
  version you'll load the plugin into**, because a normal end-user OBS
  install (e.g. `C:\Program Files\obs-studio`) ships only the runtime DLLs
  — no `obs.lib` import library and no headers — and libobs's plugin ABI is
  a plain C struct layout with no version negotiation, so a plugin built
  against a *different* commit than the OBS you run it in can crash instead
  of just failing to load. Building against the matching tag, as done
  below, is what makes this safe.

---

## 4. Building on Windows

This is the exact sequence used to build and verify this plugin end-to-end
against a real installed **OBS Studio 32.0.1** on this machine (adjust the
tag to match your own OBS Studio's version, shown in OBS's *Help → About*).

```bash
# 1. Clone OBS Studio and check out the tag matching your installed OBS.
git clone --recursive --depth 1 https://github.com/obsproject/obs-studio.git C:/dev/obs-studio
cd C:/dev/obs-studio
git fetch --depth 1 origin tag 32.0.1      # match your installed OBS version
git checkout 32.0.1

# 2. Configure libobs only -- no full OBS app, no Qt UI, no bundled
#    plugins/CEF browser -- just the shared library + headers a plugin
#    links against. ENABLE_UI was called ENABLE_FRONTEND on some in-between
#    dev snapshots; check your checkout's top-level CMakeLists.txt option()
#    lines if this name doesn't match.
cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A "x64,version=10.0.26100.0" ^
  -DENABLE_UI=OFF -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF

# This step downloads OBS's prebuilt dependency bundle (obs-deps + Qt6,
# a few hundred MB) the first time; CEF is skipped entirely by
# ENABLE_BROWSER=OFF. If a download fails with "SSL connect error" (a
# flaky-CDN issue we hit repeatedly, unrelated to certificates), just
# re-run the same cmake command -- already-downloaded files in
# C:/dev/obs-studio/.deps are reused, so retries are cheap. If it keeps
# failing on one specific file, curl it by hand into .deps/<filename>
# (same name as the failing URL) with a plain retry loop; the configure
# step skips downloading a file that's already present.

# 3. Build.
cmake --build build_x64 --config RelWithDebInfo --target libobs

# 4. Configure and build this plugin against that libobs.
cd "E:/obs windows plugin"
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DOBS_SOURCE_DIR="C:/dev/obs-studio" ^
  -DOBS_LIB="C:/dev/obs-studio/build_x64/libobs/RelWithDebInfo/obs.lib" ^
  -DOBS_CONFIG_DIR="C:/dev/obs-studio/build_x64/config" ^
  -DOBS_FRONTEND_LIB="C:/dev/obs-studio/build_x64/frontend/api/RelWithDebInfo/obs-frontend-api.lib" ^
  -DOBS_QT_DIR="C:/dev/obs-studio/.deps/obs-deps-qt6-<date>-x64" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

Before step 4, also build the frontend API library once (it provides the
`obs_frontend_*` functions used for the menu-bar button):
`cmake --build build_x64 --config RelWithDebInfo --target obs-frontend-api`.
Qt6 comes from the deps bundle OBS's configure step already downloaded
(`.deps/obs-deps-qt6-*`); use the Qt6 there so the plugin matches OBS's Qt.

`-DOBS_CONFIG_DIR` matters: `obs.h` pulls in a build-generated
`obsconfig.h` that only exists under the *build* tree (e.g.
`build_x64/config/obsconfig.h`), not the source tree.

The build produces `build/RelWithDebInfo/win-frame-filter.dll` and also
assembles a ready-to-copy layout under `build/install-test/` (DLL under
`obs-plugins/64bit/`, `data/` under `data/obs-plugins/win-frame-filter/`) —
see the `POST_BUILD` step in `CMakeLists.txt`.

### Two gotchas this project actually hit (worth knowing before you debug them yourself)

1. **`libobs`'s `gs_image_file_t` API was renamed to `gs_image_file_ex_t`
   between dev snapshots** (init/free/init_texture functions gained an
   `_ex_` and an extra alpha-mode argument). `src/win-frame-render.c` and
   `src/win-frame-internal.h` use whichever one exists in *your* checkout
   — check `libobs/graphics/image-file.h` and swap the calls if you're
   building against a version on the other side of that rename.
2. **If your shell is an MSYS2/MINGW64 environment** (`MSYSTEM` env var
   set, e.g. Git Bash from an MSYS2 install), CMake's Windows `find_library`
   logic can pick up MSYS2's own static FFmpeg (`C:\msys64\ucrt64\lib\
   libavformat.a` etc.) instead of the correct OBS-deps import library,
   producing dozens of `unresolved external symbol` link errors for
   completely unrelated things (gnutls, libbluray, VAAPI, shaderc, soxr).
   Fix by pinning the correct paths explicitly:
   ```bash
   DEPS=C:/dev/obs-studio/.deps/obs-deps-<date>-x64/lib
   cmake ... -DFFmpeg_avformat_IMPLIB:FILEPATH="$DEPS/avformat.lib" \
             -DFFmpeg_avutil_IMPLIB:FILEPATH="$DEPS/avutil.lib" \
             -DFFmpeg_swscale_IMPLIB:FILEPATH="$DEPS/swscale.lib" \
             -DFFmpeg_swresample_IMPLIB:FILEPATH="$DEPS/swresample.lib" \
             -DFFmpeg_avcodec_IMPLIB:FILEPATH="$DEPS/avcodec.lib"
   ```

---

## 5. Installing into OBS Studio

**Option A — the real per-user plugin directory (no admin rights needed;
this is the one actually verified working in this session):**

On Windows, OBS resolves per-user third-party plugins from
`%ProgramData%\obs-studio\plugins\<module>\bin\64bit\<module>.dll` (+
`...\data\`) — **not** `%APPDATA%`, despite `%APPDATA%\obs-studio\plugins`
looking like the natural guess (see `frontend/widgets/OBSBasic.cpp`,
`AddExtraModulePaths()`, which calls `GetProgramDataPath` on `_WIN32`).
`C:\ProgramData` is normally writable without elevation:

```powershell
$dst = "C:\ProgramData\obs-studio\plugins\win-frame-filter"
New-Item -ItemType Directory -Force -Path "$dst\bin\64bit" | Out-Null
New-Item -ItemType Directory -Force -Path "$dst\data" | Out-Null
Copy-Item "build\RelWithDebInfo\win-frame-filter.dll" "$dst\bin\64bit\" -Force
Copy-Item "data\*" "$dst\data\" -Recurse -Force
```

**Option B — into the system OBS install** (needs an elevated/admin shell —
`C:\Program Files\obs-studio` is not user-writable by default):

```bash
copy build\RelWithDebInfo\win-frame-filter.dll ^
     "C:\Program Files\obs-studio\obs-plugins\64bit\"
xcopy /E /I data "C:\Program Files\obs-studio\data\obs-plugins\win-frame-filter"
```

**Option C — `cmake --install`** does the equivalent of Option B in one
step (also needs elevation):

```bash
cmake --install build --config RelWithDebInfo --prefix "C:/Program Files/obs-studio"
```

Then start OBS Studio → right-click a webcam source → **Filters** → **+** →
**Windows Camera Frame**. If OBS was left running with an unclean previous
shutdown, it will show a "Crash detected" dialog before finishing startup
(and before it scans the plugins directory) — click **"Launch normally,
enabling all plugins"** (or delete the empty marker files under
`%APPDATA%\obs-studio\.sentinel\` before relaunching to skip the prompt).

**Verified**: this exact plugin build was installed via Option A and loaded
successfully into a real, already-installed OBS Studio 32.0.1 — its log
showed `[win-frame-filter] plugin loaded (version 1.0.0)` and listed
`win-frame-filter.dll` under `Loaded Modules` alongside OBS's other
first- and third-party plugins, with no errors or crash afterward.

### Debugging

* Launch OBS from a terminal (`"C:\Program Files\obs-studio\bin\64bit\obs64.exe"`)
  to see `blog()` output live, or check
  `%APPDATA%\obs-studio\logs\` after a run.
* Every plugin log line is prefixed `[win-frame-filter]`.
* To attach a debugger: Visual Studio → Debug → Attach to Process →
  `obs64.exe`, with the plugin DLL's PDB next to the DLL (RelWithDebInfo
  keeps debug symbols).

---

### Menu-bar button

The plugin adds a **Camera Frame** entry to OBS's main menu bar (after
*Help*, `src/ui/menu-button.cpp`). Clicking it shows the plugin version
(set once in `CMakeLists.txt` via `project(... VERSION x.y.z)`).

## 5b. Previewing styles without OBS (render harness)

`tools/render-test/` loads the real plugin DLL into libobs (D3D11), renders
every built-in style over a test image and writes a contact-sheet PNG, so you
can inspect the look (and catch shader errors) without opening OBS:

```bash
cmake -B build ... -DWF_BUILD_RENDER_TEST=ON
cmake --build build --config RelWithDebInfo --target render-test
python tools/render-test/make_test_image.py build/test-cam.png
# run with OBS's runtime DLLs on PATH:
build/RelWithDebInfo/render-test.exe out.png build/RelWithDebInfo/win-frame-filter.dll data build/test-cam.png 1 4
# options after cols: only=win11_fluent  dark_mode=true  ui_scale=1.5  mask_type=2 ...
python tools/render-test/crop_png.py out.png crop.png x y w h    # zoom into a region
```

`install.ps1` copies the built DLL + `data/` into
`C:\ProgramData\obs-studio\plugins\win-frame-filter` (close OBS first).

### All 36 styles

Windows: Classic, 3.1, NT 4.0, 95, 98, 98 Plus! (Space, Dangerous Creatures), ME, 2000,
XP (Luna Blue / Olive / Silver, Royale, Zune, Embedded, Classic), Vista (Aero, Basic),
7 (Aero, Basic, High Contrast), 8 (Metro), 8.1 (Metro), 8 Metro App (full screen,
back arrow), 10, 10 Accent Title Bar, 10 Tablet Mode, 11, 11 Mica Accent with a
Snap Layouts flyout, Windows Phone 7, Windows Phone 8 (tiles) - plus macOS, GNOME
(Adwaita), KDE Plasma (Breeze), Amiga Workbench and Atari TOS/GEM.

The Luna variants other than Blue, and Royale / Zune / Embedded / the Plus! themes /
Windows Phone / Amiga / Atari, are close approximations from memory of the real
themes, not pixel copies. All artwork is drawn procedurally (no Microsoft or other
assets).

**Camera effects** (Camera group): pixelate, scanlines, and a CRT effect (barrel
curvature, vignette, colour fringing). All on the GPU, no extra passes.

### What the styles reproduce

| Style | Frame | Title bar | Buttons |
|---|---|---|---|
| 95 / 98 / ME / 2000 / XP Classic | 4px raised 3D bevel, sunken client edge | inset, 18px, navy (95 solid, others left-to-right gradient), bold | 16x14 bevelled, gap before Close |
| XP Luna | 4px blue, top corners rounded 8px | 30px authentic multi-stop blue gradient, bold Trebuchet + drop shadow | 21px glossy rounded, white border, red-orange Close; status bar + grip |
| Vista / 7 Aero | 8px translucent glass, dark+white double border, soft shadow, diagonal shine | glass, text glow (Vista: white text + shadow) | 28x19 glass pills hanging from the top edge, glossy red Close |
| 8 / 8.1 | flat accent frame (8: 6px, 8.1: 1px) | flat accent / white | flat, red Close |
| 10 | 1px border, square corners | white 31px | 46x31 flat, thin glyphs |
| 11 | 8px rounded, thin border, big soft shadow | mica 32px | 46x32 flat, thin glyphs |

Dark mode (Windows 10/11) is a checkbox in *Background / Custom Style*;
*UI Scale* scales title bar, buttons, fonts, borders and shadows together for
1080p/4K canvases.

---

## 5c. Menu, dialogs, presets, downloads

The **Camera Frame** menu in OBS's menu bar has: *Add Frame to Selected Source*,
*Apply Style to Current Scene* (every frame in the scene, including groups),
*Copy Style Between Sources*, *Preset Manager* (import, export, rename,
duplicate, delete), *Download Presets*, *Settings*, *Check for Updates*,
*Quick Start* and *About*. Right-clicking a source in the Sources list also
offers **Add Windows Camera Frame** (the entry is injected into OBS's own menu
next to *Filters*; it matches the English menu text). The first time OBS starts
after installing, a welcome dialog explains the basics.

Import / Export / Rename / Download buttons are also in the filter's own
**Presets** group. Settings live in
`%APPDATA%\obs-studio\plugin_config\win-frame-filter\settings.json`.

**Preset download format** (HTTPS only, 1 MB limit, data only - no code is
downloaded or run, and any `*_path` keys that point at local files are
stripped): either one preset JSON file, or an index
`{"presets":[{"name":"My look","url":"https://.../my-look.json"}, ...]}`.

**Update check**: set *Update check URL* in Settings to a small JSON file such as
`{"version":"1.2.0","url":"https://github.com/you/repo/releases/latest"}`
(`installer\build-release.ps1` generates `dist\update.json` for this).

## 5d. Online installer (downloads everything from GitHub)

`installer\build-release.ps1 -Repo "yourname/win-frame-filter"` builds the
release zip and an Inno Setup installer. The installer contains no plugin
files; when run it

1. refuses to continue while OBS is running (it locks the plugin DLL),
2. if OBS Studio is not installed, offers to download **OBS Studio 32.0.1**
   from `github.com/obsproject/obs-studio/releases` (SHA-256 pinned; the plugin
   is built against exactly that version) and runs its installer (UAC prompt),
3. downloads `win-frame-filter-<ver>-win64.zip` from
   `github.com/<repo>/releases/download/v<ver>/` and **verifies its SHA-256**
   (a tampered or corrupted download is rejected and nothing is installed),
4. unpacks it into `C:\ProgramData\obs-studio\plugins\win-frame-filter`,
5. registers an uninstaller that removes the plugin.

Publishing steps: create a GitHub release tagged `v<ver>`, upload the zip from
`dist\`, then give people `WindowsCameraFrame-Setup-<ver>.exe`. For testing you
can point the installer at any URL:
`Setup.exe /VERYSILENT /DIR="C:\test" /PluginUrl=http://127.0.0.1:8000/win-frame-filter-1.1.0-win64.zip`.

## 5e. Tests

`logic-test` (built with `-DWF_BUILD_RENDER_TEST=ON`) runs the non-UI logic
against real libobs: add-filter, copy-style, enumeration, HTTPS fetching and
its limits, download/update-check error handling, and the properties panel
(including the style-change callbacks). The Qt dialogs and menu are compiled
but have not been driven in a live OBS window.

## 6. Known simplifications (and what "full" would take)

| Area | What's implemented | What a fuller version needs |
|---|---|---|
| Shadow/Glow blur | SDF distance falloff (`smoothstep`), GPU-cheap, visually close | A real separable Gaussian blur pass (extra render target + 2 draw calls) for a physically-accurate soft shadow at very large blur radii |
| Acrylic/Mica ("blur-behind") | Tinted semi-transparent layer + highlight band; genuinely transparent so the actual OBS scene shows through | True backdrop blur needs the compositor framebuffer *behind* this source, which a single OBS filter cannot see — would require an OBS **source** (not filter) that captures the whole scene, a much bigger architectural change and generally not how compositors expose this |
| Title/status text | Windows GDI rasterization, cached | macOS (Core Text) / Linux (FreeType/Pango) backends for `win_frame_render_text_texture()` — same call signature, swap implementation per-OS behind the existing `#if defined(_WIN32)` guard |
| Preset Import/Export UI | Functions fully implemented and tested against the JSON schema | Wire two more `obs_properties_add_button2()` callbacks that open a native "choose file" dialog (a few lines using `QFileDialog` via the OBS Qt frontend, or `obs-frontend-api`) |
| Animation | Fade + ease-out scale on show/hide, timed via `video_tick` | Direction-aware slide-in/out with configurable easing curves (the `S_ANIM_DIRECTION` key already exists in the schema for this) |
| Custom border texture | Schema key (`custom_frame_path`) reserved, `border_tex` sampler already declared in the shader | Wire the tiled-stroke sampling math in the shader's border section |
| SVG assets | Accepted anywhere a PNG file picker is used, since `gs_image_file_t` only decodes raster formats | Rasterize SVG → PNG at load time (e.g. via `nanosvg`, a small header-only dependency) before handing it to `gs_image_file_init` |

None of these are placeholders that crash or no-op silently without a code
path — each degrades to a reasonable visual fallback (see §2, "handles
missing camera / disconnected source / corrupt presets").

---

## 7. Cross-platform notes

The project is structured so macOS/Linux support is "add files", not
"rewrite":

* `CMakeLists.txt` only pulls in `gdi32`/`user32` under `if (WIN32)`.
* `win-frame-filter.c`, `win-frame-render.c`, `win-frame-styles.c`,
  `win-frame-presets.c` are 100% portable libobs/C — nothing Windows-specific.
* The **only** platform-specific file is `win-frame-text.c`. Add a Core
  Text implementation behind `#elif defined(__APPLE__)` and a
  FreeType/Pango one behind `#elif defined(__linux__)`, matching the
  existing function signature in `win-frame-text.h`, and the whole plugin
  builds and runs unmodified elsewhere.

---

## 8. Quick settings reference

Every setting lives under one of the panel's group headers (GENERAL,
CAMERA, FRAME, TITLE BAR, WINDOW BUTTONS, BORDER, SHADOW, GLOW, OVERLAY,
MASK, BACKGROUND/CUSTOM STYLE, ANIMATION, PRESETS), matching the spec's
requested section list. The exact settings key for every control is listed
in `src/win-frame-filter.h` and doubles as the preset JSON schema — open any
file in `data/presets/` to see a concrete example.
