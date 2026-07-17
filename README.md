# CM93 plugin for HMV Chartplotter (GPL-2.0-or-later)

Reads **C-Map CM93 version 2** vector charts and feeds them into
[HMV Chartplotter](../) through the host's `IChartSource` plugin interface, so
CM93 cells flow through the same catalog → quilt → cache → S-52 symbology → paint
pipeline as ENC (S-57) charts. Built as a standalone, dynamically-loaded module
(`QPluginLoader`) — the host links none of this code.

## Licensing

This plugin is **GPL-2.0-or-later**, because its CM93 binary decoding is derived
from [OpenCPN](https://github.com/OpenCPN/OpenCPN) (`gui/src/cm93.cpp`,
© 2010 David S. Register). The host application is **LGPL-2.1**; keeping the CM93
reader in this separate GPL module is what preserves the host's licence. Full GPL
text is in [COPYING](COPYING); every source file carries an SPDX tag. The
vendored host headers under `sdk/` remain LGPL-2.1 (see [sdk/README.md](sdk/README.md)).

## Dependencies

- **Qt 6** (Core, Gui, Widgets, Concurrent). At runtime the plugin imports only
  `Qt6Core.dll` + `Qt6Concurrent.dll`.
- The host **plugin-SDK headers**, vendored in `sdk/` (no host library needed).
- **No GDAL** and no host build are required to build this plugin.

## Build

```sh
cmake --preset vs2022                 # adjust CMAKE_PREFIX_PATH for your Qt
cmake --build --preset vs2022-release
```

Override Qt location if needed: `-DCMAKE_PREFIX_PATH=<your Qt>/msvc2022_64`.
The result is `build/vs2022/plugins/cm93_plugin/Release/chartplotter_cm93_plugin.dll`
(name/layout varies by generator).

### Install into the host

Either pass the host directory at configure time so it deploys automatically:

```sh
cmake --preset vs2022 -DCHARTPLOTTER_APP_DIR="C:/Program Files/HMV Chartplotter"
cmake --build --preset vs2022-release
```

…or copy two files by hand:

1. `chartplotter_cm93_plugin.dll` → `<host>/plugins/`
2. `Qt6Concurrent.dll` → `<host>/` (next to `hmvchartplotter.exe`). The host does
   not ship this DLL itself (its own `QtConcurrent` use is header-only, so
   `windeployqt` never copies it); the plugin imports it, so it must be present.

Then point the app at a CM93 dataset root (the folder containing `CM93OBJ.DIC`).

### Windows installer (NSIS)

Like the host application, a multi-config MSVC Release build automatically
produces an NSIS installer (`build/vs2022/installer/HMV Chartplotter CM93
Plugin-<version>-win64.exe`) — no separate command needed. It installs the two
runtime files into an existing host install under `$PROGRAMFILES64\HMV
Chartplotter`:

- `plugins\chartplotter_cm93_plugin.dll`
- `Qt6Concurrent.dll` (next to `hmvchartplotter.exe`)

so the layout matches what the host's own installer produces. Install the host
first, then run this installer to add CM93 support. It requires
[NSIS](https://nsis.sourceforge.io/Download) (`makensis`, found via PATH or the
registry) at build time; disable the auto-step with `-DCM93_BUILD_INSTALLER=OFF`,
or build it by hand:

```sh
cpack --config build/vs2022/CPackConfig.cmake -C Release -G NSIS
```

The step is gated to multi-config (Visual Studio) generators, so the single-config
Ninja/MinGW CI build never invokes it.

## Smoke test

A GUI-free decoder check (built by default, `-DBUILD_CM93_TESTS=OFF` to skip):

```sh
# needs Qt6Core.dll on PATH
build/vs2022/plugins/cm93_plugin/Release/test_cm93.exe  "C:/path/to/CM93"
```

It loads the dictionary and decodes a sample of cells across all scales, printing
each cell's footprint, coverage-ring count, and object counts.

## ABI / staying in sync with the host

The five headers in `sdk/` are the ABI contract. The host's loader **rejects** a
plugin whose `kPluginAbiVersion` / IID doesn't match, so a stale plugin fails
loudly at load time. When the host's plugin API changes, re-vendor `sdk/` and
keep `plugin_factory.hpp` identical. Current target: plugin ABI **v4**.

## How CM93 is decoded (overview)

Ported from OpenCPN into `src/cm93_decoder.cpp` / `cm93_dictionary.cpp`:

1. **Dictionary** — `CM93OBJ.DIC` maps object-class codes to S-57 acronyms;
   `ATTRLUT.DIC` / `CM93ATTR.DIC` maps attributes + value types. `COLMAR` colour
   enums become S-52 `COLOUR` values; a few class substitutions (e.g. `ITDARE` →
   `DEPARE`) are applied.
2. **Deobfuscation** — cell files are byte-scrambled through a fixed 256-byte
   table; every field is decoded on read.
3. **Layout** — 10-byte prolog + 128-byte header (with a length checksum), then
   vector-edge / 3-D-point / 2-D-point / feature-record tables.
4. **Geometry** — cell-relative `uint16` points are de-quantised with the
   header's transform coefficients onto the International 1924 ellipsoid, minus
   the per-`M_COVR` WGS84 datum offset of the region containing the feature, then
   converted to lon/lat (kept continuous across the antimeridian) and projected
   to Mercator by the host. Areas assemble multi-ring polygons; lines concatenate
   edges; soundings carry depth.
5. **Catalog** — each cell's footprint (header bbox + `M_COVR` coverage outline)
   is read for quilting and cached to disk
   (`AppData/.../cm93_catalog_cache/`), so only the first scan decodes every cell
   (in parallel, with progress); later scans are fast.

CM93's 8 scales (Z, A–G) each map to their own host band so overlapping
global-coverage scales never share a band (which would double-draw / flicker).

## Limitations

- `.xz`-compressed cells are skipped (no LZMA decompression yet).
- The scale→band thresholds reuse the ENC breakpoints (serviceable, not tuned).
- `M_COVR` holes are treated as covered (over-covering is the safe quilt direction).
