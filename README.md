# OfflineTides — Tidal Height Forecast — Alpha candidate 1.0.2

OfflineTides is an offline OpenCPN tidal-height forecasting plugin. It reads
authenticated `.xtdt` tide profiles, predicts
water level at the chart cursor, an OpenCPN waypoint or mark, or manually
entered WGS84 coordinates, and displays a rolling or selected-date curve with
interpolated HW/LW events. It is the publication identity of the experimental
plugin previously developed as X-Tidal; the on-disk plugin package is now
`offlinetides_pi`, while `.xtdt` remains the stable operational data format.

Version 1.0.2 establishes the OfflineTides product and package identity,
migrates user settings without modifying the legacy X-Tidal group, and adds a
deterministic CircleCI Alpha pipeline with clean Linux builds, exact
archive/metadata pairing, runtime-only dependency checks, retained evidence,
and a separate approval-gated publication workflow.

The Alpha plugin binary and global operational data are published separately.
After installing the plugin, download
`offlinetides-global-data-1.0.0-alpha1.tar.gz` from the
[OfflineTides Alpha package repository](https://cloudsmith.io/~pob220/repos/offlinetides-alpha/packages/),
verify its adjacent SHA-256 file, extract it, then use
**Open .xtdt package...** in the forecast window. The public archive contains
the self-contained `.xtdt`, manifest, checksums and field-use instructions;
the detailed authoring record is retained privately and is neither distributed
nor required by OfflineTides.

Version 1.0.1 gives the graph a persistent header legend identifying both
the blue OfflineTides forecast and the dashed orange native station curve by name
and distance. Long station names are ellipsized to remain visible in
constrained dialog layouts; the complete station name remains available in
the native-comparison details below the graph. The legend sits outside the
plot and the protected graph height is increased so it cannot obscure useful
curve data.

Version 0.8.1 adds direct curve inspection: moving the pointer along the graph
shows the interpolated height and exact display-zone time at that point. A
click pins the readout for comparison; clicking again releases it. The readout
uses the currently selected Chart Datum or model-MSL reference explicitly.

Version 1.0 adds an optional native OpenCPN comparison curve without weakening
the plugin's API 1.21 compatibility. When run by an enhanced OpenCPN core it
uses typed station metadata, vertical datum, source provenance, metric heights
and stable station identity. It then chooses one of three explicit modes:

- absolute comparison only for a declared, equivalent vertical datum;
- a documented TCD `Z0` transform when that transform is applicable; or
- vertical mean alignment for timing, range and shape only, with absolute
  height differences suppressed.

The comparison defaults on when eligible, remains user-selectable, and is
drawn as a dashed orange curve. Pointer
inspection shows both curves at the same instant. A native station must be
within 100 NM of the OfflineTides forecast point; otherwise no native station or
curve is displayed. This avoids presenting a remote station as meaningful
local evidence and follows the established TideFinder search radius.

OpenCPN cores without the enhanced metadata bridge continue to work through
Host API 1.21 for station and HW/LW timing comparison. The plotted curve is
disabled there because API 1.21 exposes neither a dependable length unit nor
enough datum information to put its values safely on the OfflineTides metre axis.
`XTIDAL_NATIVE_DATUM_AUDIT=<file.json>` writes a complete live catalogue audit
for the supplied validation tool.

Version 0.9 made `.xtdt` the only operational package and added:

- nearest native OpenCPN tide-station comparison through Host API 1.21,
  including station name, distance, source availability, native and plugin
  datum labels, and HW/LW timing differences;
- fail-closed height comparison: API 1.21 does not expose the native datum, so
  a native/plugin height delta is suppressed instead of assuming equivalence;
- versioned JSON and CSV forecast export with UTC validity, WGS84 position,
  metre units, datum identifier/name/epoch, method, uncertainty, package and
  source identity, and explicit known/unknown/masked states;
- a frozen semantic seam and decision record for a possible future S-104 2.0
  adapter, without HDF5 or an S-104 user control; and
- an audited Chart Datum authoring overlay using permitted authority facts,
  with reproducible spatial replacement recorded in the private authoring
  record.

Tidal-stream/current display belongs in xGRIB, where it can be interpreted
alongside GRIB weather and used by weather routing. OfflineTides deliberately
exposes only water-level forecasts even though the shared XTD reader retains
backward-compatible support for older combined packages.

Version 0.8 adds:

- persistent forecast-position selection between the chart cursor, any
  waypoint or mark in the current OpenCPN profile, and manual WGS84
  coordinates;
- signed decimal coordinate entry with optional N/S/E/W notation and strict
  latitude/longitude bounds;
- a choice between the rolling 48-hour next-tides view and a complete selected
  calendar date in the chosen display time zone; and
- daylight-saving-correct 23-, 24- and 25-hour local-day windows while all
  package prediction times remain UTC.

Version 0.7 added:

- an authenticated, independently masked model-MSL-to-local-Chart-Datum
  transform in the `.xtdt` package;
- topology-bounded authoring from 3,878 permitted reference stations covering
  117 countries;
- explicit LAT, MLLW, LLWLT, NLLW, MLWS, MSL, TLT/LLW and other-authority
  realization classes instead of assuming one global definition of Chart
  Datum;
- a UI selector between `Height above Chart Datum` and the unchanged native
  `Level relative to model mean sea level` prediction;
- datum offset, uncertainty, nearest-station distance, realization and support
  information at the chart cursor;
- fail-closed Chart Datum display outside the transform's supported water
  graph;
- one operational-package qualification runner covering 13 UK and ten
  international authoritative-source canaries; and
- a reproducible libtcd authoring exporter. The source TCD and libtcd are not
  runtime or distribution dependencies.

Version 0.6 added:

- a backward-compatible `.xtdt` tide profile whose height, stream and quality
  components are independently optional;
- the exact adjacent `<name>.xtdt.prv` convention for private authoring
  records;
- a global 0.125-degree TPXO-derived model-MSL background with no raw-model
  runtime dependency;
- offline TICON-3 normalization for 3,325 coastal/river gauge records and all
  40 published constituents;
- complex harmonic residual assimilation on a connected water graph;
- station-supported cells for observed estuaries omitted by the ocean mask;
- per-cell harmonic uncertainty, datum uncertainty, observation distance,
  support class and observation count;
- deterministic per-cell gauge selection and a supported constituent union;
- an exact-grid-node coastal interpolation fix preventing mask erosion during
  XTD-to-XTD authoring.
- all 40 TICON-3 constituents in the prediction engine, including the IHO
  lambda-2 frequency/nodal correction;
- a 3,325-station global 40-constituent operational package with independent
  background-model fallbacks used only for coverage gaps;
- runtime sampling and cursor display of harmonic uncertainty, station
  distance, support class and observation count;
- explicit `MSL` graph/event labels and a visible block on using model-MSL
  heights for chart-depth or under-keel-clearance arithmetic.

Version 0.6.1 keeps prediction instants in UTC but adds an editable IANA
display-time-zone selector with daylight-saving-aware labels. It also gives the
water-level graph a protected minimum height, bounds the HW/LW list in a
scrollable panel and hides unused current controls for height-only packages.

The retained v0.4 capabilities include:

- authenticated XTD v2 water-level harmonic components, including height-only
  packages;
- explicit vertical-datum identifiers and reference levels;
- 48-hour UTC calculations with selectable local-time display and sub-sample
  HW/LW interpolation;
- fail-closed masked or outside-coverage points;
- backward-compatible parsing of XTD v1/v2 current components in the shared
  reader, without exposing current prediction in the OfflineTides plugin;
- headless event-time, height and tidal-range comparison tools.
- bounded, complex-valued local harmonic corrections authored from an
  existing XTD and permitted station constants;
- topology-bounded anisotropic station support, separate major/shallow-water
  constituent reach, hard support polygons and a separately bounded datum
  field;
- train/accept/final-test station calibration without fitting the final seven
  days;
- local Chart Datum products which mask unsupported points;
- explicit HW/LW graph annotations in metres above Chart Datum;
- a ten-location worldwide canary suite against national published sources.

## Data, provenance and IP boundary

The OfflineTides plugin accepts `.xtdt` packages only. The shared lower-level test
reader retains legacy `.xtd` compatibility for xGRIB and authoring tools, but
that is not an operational OfflineTides input. The plugin does not contain, discover, download
or read TPXO or another raw source model, and it does not compile the NetCDF
source loader into the plugin.

Raw TPXO may be used only by the separate offline authoring tool to derive a
self-contained coefficient grid. The distributable `.xtdt` contains operational
interpretation fields and quantised coefficients, not raw TPXO data. Source
lineage and authoring details are deliberately excluded from the `.xtdt`; the
detailed authoring record is retained privately.

`.xtd` and `.xtdt` are independently designed, self-contained tidal-harmonic
formats. Released packages contain only authenticated, quantised derived
coefficients and supporting quality/datum fields which the author is permitted
to distribute. They contain and require no raw third-party source dataset, and
OfflineTides requires no access to such datasets at runtime.

Harmonic coefficients remain referenced to model mean sea level. Version 0.7
adds a distinct authenticated transform to local Chart Datum. Unsupported
datum areas remain unknown rather than silently inheriting model MSL. The
private authoring record identifies the authoring release and hashes; it is
never embedded in or required by the operational container.

## Developer tools

- `environmental_grib_tpxo_height_author` creates a height-only `.xtd` plus
  `.xtd.prv` from a private local TPXO authoring directory.
- `xtidal_validate_height` runs the plugin prediction engine headlessly against
  a frozen JSON HW/LW reference.
- `xtidal_build_station_xtd` creates a small Chart Datum validation package
  from OpenCPN station harmonics; it is an engine-equivalence fixture, not an
  independent accuracy source.
- `tests/fetch_ntslf_reference.py` imports a requested interval from NTSLF's
  public table for a manual published-reference benchmark.
- `xtidal_build_corrected_xtd` creates a multi-station, locally bounded Chart
  Datum XTD from an existing derived XTD, a harmonic station catalogue and a
  correction catalogue. It does not read raw TPXO.
- `tests/compare_correction_results.py` prints a reproducible Markdown
  before/after table over frozen references.
- `tests/compare_reference_publishers.py` measures the empirical time/height
  disagreement floor between two same-datum published sources.
- `tests/run_global_canaries.py` authors isolated station XTDs and runs the ten
  official-source worldwide comparisons without installing anything.
- `tools/export_tcd_chart_datums.c` exports permitted reference-station datum
  offsets for offline authoring while preserving station restrictions and
  source provenance.
- `tests/qualify_global_package.py` runs one operational `.xtdt` against
  locally supplied authoritative-source canaries and fails if Chart Datum is
  unavailable or any published event is unmatched. The private 13-station
  comparison catalogue used for the final UK qualification is not distributed
  with the public source.
- `tests/apply_chart_datum_overrides.py` creates the reproducible v0.9 datum
  authoring catalogue from the base catalogue and documented authority facts.
- `tests/audit_vertical_datums.py` flags near-constant absolute height biases
  with small event/range spread for human datum-source review; it never tunes
  a station automatically.
- `tests/audit_native_tide_catalogue.py` validates the enhanced core's live
  native-station metadata audit, including stable identities, source/version,
  datum status, coordinates and `Z0` values.
- `XTIDAL_UI_SMOKE_TEST=1` opens the forecast dialog after plugin
  initialization in an isolated OpenCPN profile, allowing deterministic GUI
  construction and accessibility checks without adding a public runtime API.
  `XTIDAL_UI_SMOKE_LATITUDE` and `XTIDAL_UI_SMOKE_LONGITUDE` optionally select
  a deterministic manual forecast point without desktop-input automation.

Initial and correction-field results are recorded in
`docs/validation-results-2026-08-20.md` and
`docs/correction-results-2026-08-21.md`. The topology-aware v0.4 and worldwide
qualification are in `docs/correction-results-v2-2026-08-21.md` and
`docs/global-canaries-2026-08-21.md`. The final v0.6 package, 23-station
qualification, source-spread analysis and field-use boundary are recorded in
`docs/v0.6-qualification-2026-08-21.md`. Version 0.7 datum design, absolute
height results and release evidence are in
`docs/v0.7-chart-datum-qualification-2026-08-21.md`.
The v0.9 mapping and future API decisions are in
`docs/s104-mapping-decision-v09.md` and
`docs/upstream-opencpn-water-level-api-proposal.md`.
The native comparison and catalogue evidence for v1.0 is in
`docs/v1.0-native-comparison-qualification-2026-08-22.md`.
The OfflineTides identity, reusable CircleCI lessons, first Linux matrix and
approval-gated Alpha procedure are in
`docs/alpha-rebuild-and-publication.md`.
The exact separately distributed Alpha data hashes, authentication evidence
and 23-station canary result are in
`docs/offlinetides-data-1.0.0-alpha1-qualification.md`.

## Current limitations

The offline place selector uses OpenCPN waypoints and marks rather than a
network geocoder. Chart Datum support is global in source coverage but remains
deliberately bounded to supported coastal/estuary water cells; unsupported
locations fail closed and can still be inspected explicitly in model-MSL mode.
The transform is astronomical and does not include weather surge, river flow,
air-pressure effects or live sea-level anomaly. Published products can
disagree about event topology at mixed and microtidal ports. Avonmouth,
Portsmouth, Hamburg and some estuaries remain caution cases. V1.0
is a field-testable secondary astronomical prediction source, not a
replacement for an authoritative local tide table, notices, or a live
water-level observation. Sounding/depth-shading and under-keel-clearance
integration remain disabled.
