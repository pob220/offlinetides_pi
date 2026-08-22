# X-Tidal lightweight correction qualification — 2026-08-21

## Candidate

The developer candidate is a Chart Datum, height-only XTD containing seven
bounded local correction patches: Avonmouth, Dover, Holyhead, Liverpool,
Newlyn, Portsmouth and Weymouth. The background is the existing derived
UK/Ireland XTD. Station innovations are applied to complex harmonic
coefficients; amplitude and phase are never interpolated independently.

The Chart Datum reference is represented by the constant `z0` coefficient.
Unsupported points are masked. The XTD contains only quantised operational
coefficients and required structural metadata. Authoring sources, station
selection, method and hashes are in the adjacent `.xtd.prv` only. Neither the
author nor runtime requires raw TPXO.

Candidate properties:

- 7 anchors;
- 685 supported 0.05-degree grid points;
- 0.5 mm coefficient quantisation;
- authenticated XTD v2;
- Chart Datum height output;
- SHA-256 `73ad93f960d23829654feacf7b98bc5c648372d0340e8b95ae89c0b13bc143f0`.

## Frozen NTSLF comparison

The same NTSLF 2026-08-21 factual samples were evaluated against the existing
regional model-MSL XTD and the corrected Chart Datum XTD. Time and tidal range
are comparable before and after. Absolute height is only comparable after the
Chart Datum correction.

| Station | Mean time before | Mean time after | Mean range before | Mean range after | Mean height after |
|---|---:|---:|---:|---:|---:|
| Dover | 13.472 min | 6.783 min | 0.260 m | 0.115 m | 0.045 m |
| Liverpool | 11.958 min | 13.279 min | 0.441 m | 0.076 m | 0.169 m |
| Avonmouth | 41.261 min | 22.306 min | 0.091 m | 0.495 m | 0.236 m |
| Holyhead | 3.975 min | 9.208 min | 0.156 m | 0.036 m | 0.035 m |
| Newlyn | 9.662 min | 6.312 min | 0.109 m | 0.119 m | 0.061 m |

Dover is a strong result for the first lightweight iteration: timing error is
roughly halved, range error is more than halved and mean absolute HW/LW height
error is 4.5 cm above Chart Datum.

The broader result is intentionally not described as a universal improvement.
Liverpool timing on this date and Holyhead timing became worse despite much
better range and now-comparable absolute height. Avonmouth timing improved but
range became worse. This demonstrates why future automatic catalogue growth
must use held-out dates and reject an anchor or constituent correction which
does not improve the agreed multi-date score.

## UI clarity

For a Chart Datum package, the cursor panel now states `Selected time: ... m
above Chart Datum`. Each HW/LW is listed on its own line with UTC time and an
explicit `m above Chart Datum` value. The curve itself marks each extremum and
labels it `HW/LW ... m CD`; its title and vertical-axis units also identify
Chart Datum. Non-Chart-Datum packages continue to identify their actual model
datum and are not presented as Chart Datum.

## Reproduction

```sh
xtidal_build_corrected_xtd BASE.xtd HARMONICS_NO_US \
  tests/corrections/uk-chart-datum-v1.json CORRECTED.xtd

python3 tests/compare_correction_results.py \
  --validator xtidal_validate_height \
  --before BASE.xtd --after CORRECTED.xtd \
  tests/reference/*-2026-08-21-ntslf.json
```

This remains a developer evaluation product, not a navigation-suitability
certificate.
