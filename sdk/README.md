# Plugin SDK headers (vendored from the host)

These headers are the **ABI contract** between the host application
(HMV Chartplotter) and this plugin. They are copied verbatim from the host
repository's `src/` directory:

| header | provides |
|---|---|
| `plugin_factory.hpp` | `IPluginFactory`, `kPluginAbiVersion`, the plugin IID |
| `plugin_api.hpp` | `IPlugin`, `ICoreApi` (and friends) |
| `chart_source.hpp` | `IChartSource`, `ChartSourceCell` — what this plugin implements |
| `raster_chart_source.hpp` | `IRasterChartSource` (the raster twin; unused here) |
| `chart_loader.hpp` | `Feature`, `Pt`, `BBox`, `FeatureKind` |
| `projection.hpp` | `proj::lonToX` / `latToY` (Mercator) |

They contain only pure-virtual interfaces, POD structs, and inline functions, so
the plugin compiles against them and links **no** host code.

## Licence

These files are part of the host application and are **LGPL-2.1**. They are
vendored here unchanged; their own licence/notices apply to them. Vendoring LGPL
headers into this GPL-2.0 plugin is fine — LGPL-2.1 is GPL-compatible.

## Keeping them in sync (important)

The host's loader rejects a plugin whose ABI doesn't match, so a stale copy fails
*loudly at load time* rather than misbehaving — but you still must re-sync when
the host's plugin API changes:

- Keep `plugin_factory.hpp` (the `kPluginAbiVersion` and IID) **byte-identical**
  to the host you are targeting.
- When you update these headers, note the host commit they came from and bump
  this plugin's version accordingly.

Source of truth (host repo, at the time of vendoring):
`<host-repo>/src/{plugin_factory,plugin_api,raster_chart_source,chart_source,chart_loader,projection}.hpp`
— plugin ABI **v5**, vendored from host commit `8c46b2d` ("add raster charts to
plugin API").

> **v4 → v5 note.** v5 added `registerRasterChartSource`/`unregisterRasterChartSource`
> to `ICoreApi` *above* `dialogParent()`, which moves `dialogParent()`'s vtable
> slot. A v4 build of this plugin calling `dialogParent()` would land on the wrong
> function, so re-vendoring these headers **and** bumping `apiVersion` in
> `src/cm93_plugin.json` is mandatory, not cosmetic. The loader's version check is
> what turns that into a clean "plugin rejected" instead of a crash.
