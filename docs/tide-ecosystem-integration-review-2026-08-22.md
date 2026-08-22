# X-Tidal accuracy and OpenCPN tide-ecosystem review

Date: 2026-08-22

## Bottom line

X-Tidal fills a different gap from OpenCPN's native tide station system and
the existing plugins: it gives an offline, globally continuous astronomical
water-level curve at an arbitrary wet point, with explicit vertical datum,
quality and provenance fields. It should complement rather than replace the
native station display.

The recommended near-term bridge is comparison, not conversion: on OpenCPN
API 1.21 and later, allow the user to compare the X-Tidal point forecast with
the nearest native tide station. Do not export the complete XTD grid as TCD.
A future OpenCPN provider API is the correct way to make arbitrary-point model
forecasts appear in the native tide UI without manufacturing thousands of
synthetic stations.

## Operational canary accuracy

All values are mean absolute errors per station for 2026-08-21. `Time` is the
HW/LW event-time error, `Height` is event height above the source's local Chart
Datum, and `Range` compares each successive HW-LW or LW-HW range. Every one of
the 83 published events matched a model event.

| Set | Station | Events | Time | Height | Range |
|---|---|---:|---:|---:|---:|
| UK authoritative sources | Avonmouth | 3/3 | 31.811 min | 0.179 m | 0.312 m |
| UK authoritative sources | Cromer | 3/3 | 5.617 min | 0.034 m | 0.066 m |
| UK authoritative sources | Dover | 3/3 | 4.478 min | 0.149 m | 0.026 m |
| UK authoritative sources | Fishguard | 4/4 | 6.192 min | 0.222 m | 0.072 m |
| UK authoritative sources | Harwich | 4/4 | 11.250 min | 0.066 m | 0.109 m |
| UK authoritative sources | Heysham | 3/3 | 10.561 min | 0.016 m | 0.029 m |
| UK authoritative sources | Holyhead | 4/4 | 9.721 min | 0.069 m | 0.035 m |
| UK authoritative sources | Immingham | 3/3 | 13.272 min | 0.071 m | 0.067 m |
| UK authoritative sources | Liverpool Gladstone | 4/4 | 14.846 min | 0.101 m | 0.028 m |
| UK authoritative sources | Newhaven | 4/4 | 10.950 min | 0.182 m | 0.199 m |
| UK authoritative sources | Newlyn | 4/4 | 9.571 min | 0.163 m | 0.060 m |
| UK authoritative sources | Portsmouth | 4/4 | 11.571 min | 0.460 m | 0.131 m |
| UK authoritative sources | Sheerness | 3/3 | 22.872 min | 0.089 m | 0.053 m |
| International | Auckland, New Zealand | 4/4 | 3.304 min | 0.104 m | 0.033 m |
| International | Wellington, New Zealand | 3/3 | 8.117 min | 0.423 m | 0.052 m |
| International | Halifax, Canada | 4/4 | 9.588 min | 0.017 m | 0.023 m |
| International | Sydney, Australia | 4/4 | 3.629 min | 0.121 m | 0.015 m |
| International | Darwin, Australia | 4/4 | 27.183 min | 0.837 m | 1.545 m |
| International | Tokyo, Japan | 2/2 | 12.658 min | 0.072 m | 0.006 m |
| International | Rotterdam, Netherlands | 4/4 | 25.458 min | 0.091 m | 0.022 m |
| International | Hamburg, Germany | 4/4 | 25.879 min | 0.436 m | 0.201 m |
| International | Rio de Janeiro, Brazil | 4/4 | 85.000 min | 0.343 m | 0.091 m |
| International | Reykjavik, Iceland | 4/4 | 9.338 min | 0.305 m | 0.034 m |

| Group | Station-mean time | Station-mean height | Station-mean range |
|---|---:|---:|---:|
| UK authoritative sources, 13 stations | 12.516 min | 0.139 m | 0.091 m |
| Ten international authority stations | 21.015 min | 0.275 m | 0.202 m |

The international sources are LINZ (Auckland and Wellington), CHS (Halifax),
Australian BoM/NT Government (Sydney and Darwin), JMA (Tokyo), Rijkswaterstaat
(Rotterdam), BSH (Hamburg), Brazilian CHM (Rio) and the Icelandic Coast Guard
(Reykjavik). UK predictions were checked against multiple local authoritative
sources retained outside the public repository.

Darwin and Rio remain deliberate stress canaries. Alternative published
products disagree materially with the frozen authority source in event
topology, time or range. The plugin is not tuned to hide that disagreement.
Portsmouth and the estuary stations with larger errors likewise remain visible
limits. This candidate is suitable as a secondary astronomical planning
source, not as the sole source for under-keel clearance.

The exact per-event source and predicted UTC times/heights are reproducible
with `tests/qualify_global_package.py`; the frozen one-day factual samples and
source URLs are under `tests/reference`.

## Package footprint

| Item | Bytes | Approximate binary size |
|---|---:|---:|
| v0.8.1 plugin tarball | 239,052 | 233.4 KiB |
| Stripped plugin binary in tarball | 570,096 | 556.7 KiB |
| Global authenticated `.xtdt` | 25,802,104 | 24.61 MiB |
| `.xtdt.prv` sidecar | 3,371 | 3.3 KiB |
| Complete installed X-Tidal runtime | 26,375,571 | 25.15 MiB |
| OpenCPN source-tree bundled TCD/IDX data | 11,733,579 | 11.19 MiB |

The plugin code is small; almost all of the footprint is the global 0.125
degree, 40-constituent data product plus datum/quality fields. The XTD file is
larger than OpenCPN's bundled discrete station databases because it supports
arbitrary wet points rather than only station locations.

## Functional comparison

| System | Main strength | Data/coverage | Important boundary or lesson |
|---|---|---|---|
| OpenCPN built-in | Integrated chart icons and tide/current station graph | Multiple TCD and legacy HARMONIC/IDX sources | Excellent native UX, but discrete stations and inconsistent source/datum metadata |
| TideFinder | Four nearest native tide stations within 100 NM; calendar for long past/future selection | Whatever native harmonics OpenCPN has loaded | Add a concise nearest-reference-station workflow to X-Tidal |
| UK online tide-table plugins | Published HW/LW for many UK locations | UK only; typically Internet-backed | Keep them as independent authority checks; do not pretend the model supersedes them |
| oTCurrent | Date/time stepping and clearer current arrows/rate/direction | TCD or HARMONIC/IDX current stations | Currents belong in xGRIB/oTCurrent, not back in X-Tidal |
| FR Currents | SHOM area streams tied to reference-port HW | French regional data and separate Merak56 harmonics | Explicit reference-port and HW offset semantics are useful for regional stream products |
| oTidalPlan | EP, course-to-steer and ETA planning using current harmonics | Native/selected current harmonics | Route-current planning is a separate consumer of current data, not a height UI |
| Admiralty Tide Tables | Manual standard/secondary port corrections using ATT/Reeds inputs | User-entered published standard and secondary ports | Secondary-port correction provenance and explicit user inputs are valuable; the plugin is not an automatic official-format importer |
| X-Tidal | Offline global curve at arbitrary point, explicit CD/MSL, uncertainty and source sidecar | Authenticated XTD grid; no raw source-model runtime dependency | Complements station products; astronomical only and must preserve fail-closed datum/coverage semantics |

## What OpenCPN exposes today

OpenCPN's tide manager loads any number of `.tcd` or `.IDX` data sources from
the Options panel. The core station model feeds chart overlays, tide/current
graphs, waypoint tide-station association and route printouts.

Plugin API 1.21 adds read-only `GetNearestTideStation` and `GetTideHeight`
operations. That is enough for X-Tidal to compare with one native reference
station, but not to enumerate the four nearest stations and not to register an
arbitrary-point water-level provider. X-Tidal deliberately remains API 1.18
today for the current compatibility target.

There is no stable plugin API for adding an XTD provider to `TCMgr`. Reaching
into `TCMgr`, its global object or the configured source list would couple the
plugin to GUI internals and should not be done.

## Recommended integration sequence

1. Keep `.xtdt` as X-Tidal's operational format and keep provenance only in
   the adjacent `.xtdt.prv` sidecar. Do not convert or export the global grid
   wholesale to TCD.
2. Add an optional API-1.21 build/capability which shows the nearest native
   station, distance and a side-by-side native/X-Tidal curve or event
   comparison. Label both datum/source identities; disable numerical height
   comparison when datum equivalence is unknown.
3. Add a compact nearest-reference list, ideally four stations within a
   configurable radius, after asking OpenCPN for a plural station-query API.
   This learns from TideFinder without duplicating all native station icons.
4. Export forecast results as a small, explicit interchange document (CSV and
   versioned JSON with UTC, display zone, coordinate, datum, uncertainty and
   package ID). This is more useful and less misleading than TCD export.
5. If users genuinely need a TCD bridge, export only explicitly selected
   virtual stations, name them as model-derived X-Tidal points, include only
   compatible harmonic constituents, and keep source/datum limitations in an
   adjacent provenance file. Treat this as optional interoperability, not the
   primary distribution format.
6. Propose an upstream typed `WaterLevelPredictionProvider` interface. It
   should query a curve at coordinate/time/reference and return datum,
   uncertainty, freshness, provenance and unknown status. OpenCPN's existing
   TCD engine and X-Tidal could then be separate providers behind one native
   UI.
7. Track IHO S-104 as the eventual standard gridded/time-varying interchange
   path. Do not force `.xtdt` into the discrete-station TCD abstraction merely
   to obtain chart icons.

## Data-quality lessons to retain

- Filter lake, historical/behind-barrier and duplicate stations rather than
  assuming every harmonic record represents present navigable water.
- Preserve source-specific vertical datum families and time bases.
- Compare multiple publishers when available and record disagreement instead
  of selecting whichever source best fits the model.
- Use official station products as independent acceptance canaries, not as a
  hidden correction table fitted to the acceptance dates.
- Keep continuous point estimates and discrete authority stations visibly
  distinct in the UI.
