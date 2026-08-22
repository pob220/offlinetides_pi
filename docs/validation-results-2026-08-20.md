# X-Tidal height validation — 2026-08-20

## Scope

This is an exploratory developer benchmark, not a navigation-suitability
certificate. All predictions were made by `xtidal_validate_height`, which uses
the same `PredictionService`, XTD reader and harmonic engine as the plugin.
Event extrema are located from a five-minute curve using parabolic
sub-sample interpolation.

Two deliberately different datasets were tested:

1. A regional 0.05° UK/Ireland height-only XTD derived offline from the locally
   permitted TPXO10 source. It has 321 × 281 points and 15 constituents. Its
   datum is **model mean sea level**, so comparisons with published Chart
   Datum tables use event times and datum-invariant tidal ranges only.
2. Tiny Chart Datum station packages derived from OpenCPN's bundled station
   harmonics. These check the authoring conversion and plugin engine at exact
   stations. They are not evidence that the regional field predicts Chart
   Datum at arbitrary points.

The regional file is 1,199,144 bytes. Full XTD verification authenticated all
30 water-level tiles. Its SHA-256 is
`7d8cdafcbad002a418a124bfc7185e81958d75be629edefe007f91dc6bfbaccf`.
Source provenance is absent from the `.xtd` and recorded only in its adjacent
`.xtd.prv` sidecar.

## Regional dataset versus NTSLF

The 2026-08-20 samples were fetched from NTSLF's live public high/low-water
tables. Each row contains four successive extrema and three tidal ranges.

| Station | Events found | Mean absolute time error | Maximum time error | Mean absolute range error | Maximum range error |
|---|---:|---:|---:|---:|---:|
| Holyhead | 4/4 | 4.23 min | 10.03 min | 0.158 m | 0.215 m |
| Newlyn | 4/4 | 10.48 min | 20.23 min | 0.107 m | 0.119 m |
| Dover | 4/4 | 12.66 min | 23.57 min | 0.277 m | 0.374 m |
| Liverpool | 4/4 | 13.95 min | 21.92 min | 0.461 m | 0.523 m |
| Avonmouth | 4/4 | 32.32 min | 41.93 min | 0.318 m | 0.468 m |

A second Liverpool date, 2026-01-01, matched 4/4 NTSLF events with 6.58
minutes mean and 12.08 minutes maximum timing error. Mean absolute tidal-range
error was 0.363 m.

The coastal authoring fallback uses the nearest wet TPXO model node within
0.25° when bilinear interpolation is masked. This is necessary for narrow
channels absent from the model mask, but the Avonmouth and Liverpool results
show that nearest-coastal values are not a finished estuary model.

## Liverpool Chart Datum station benchmark

Against the official NTSLF 2026-01-01 table:

| Event | NTSLF UTC / m CD | Plugin UTC / m CD | Time error | Height error |
|---|---|---|---:|---:|
| LW | 03:07 / 2.29 | 03:08:33 / 2.258 | +1.55 min | -0.032 m |
| HW | 08:51 / 8.55 | 08:48:09 / 8.474 | -2.85 min | -0.076 m |
| LW | 15:42 / 2.24 | 15:43:23 / 2.141 | +1.38 min | -0.099 m |
| HW | 21:17 / 8.84 | 21:15:16 / 8.772 | -1.73 min | -0.068 m |

Aggregate errors were 1.88 minutes mean / 2.85 minutes maximum for time,
0.069 m mean / 0.099 m maximum for height, and 0.033 m mean / 0.043 m maximum
for tidal range.

For 2026-05-01, the same engine was compared with two published sources:

| Published source | First HW UTC / m CD | Second HW UTC / m CD |
|---|---|---|
| NTSLF | 10:49 / 8.99 | 23:06 / 9.02 |
| Aquavista Liverpool table | 10:49 / 9.00 | 23:07 / 9.00 |
| Plugin engine | 10:42:27 / 8.940 | 23:00:51 / 9.031 |

The two publications agree within one minute and 0.02 m. Against NTSLF's four
May extrema the plugin's mean errors were 6.93 minutes and 0.146 m. Against the
two Aquavista high waters they were 6.35 minutes and 0.045 m.

## Interpretation

- The XTD height component, quantisation, authentication, curve generation and
  HW/LW extraction are functioning end to end.
- A station-constant package can reproduce difficult Liverpool predictions
  closely, including the nonlinear curve contribution from shallow-water
  constituents.
- The first regional field is already useful for exploratory height curves and
  event timing, but it is not yet a Chart Datum product and its estuary errors
  are too large for adjusted soundings.
- The next accuracy iteration should encode bounded local harmonic correction
  fields for Liverpool/Lower Mersey and Avonmouth/Severn, preserve additional
  shallow-water constituents, and validate on dates held out from correction
  fitting. All authoring lineage belongs in `.xtd.prv`, not `.xtd`.

## Reproduction

Build the generator authoring target and plugin test targets, author the
regional package from the private model directory, then run
`xtidal_validate_height PACKAGE.xtd REFERENCE.json`. The live NTSLF one-day
manifest can be regenerated with `tests/fetch_ntslf_reference.py`. Frozen
Liverpool NTSLF and Aquavista samples are in `tests/reference/`.
