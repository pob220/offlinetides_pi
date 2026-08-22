# X-Tidal topology-aware UK qualification — 2026-08-21

## Candidate and method

The v0.4 developer candidate replaces radial correction patches with
topology-bounded anisotropic support. Each of its 19 anchors can define a
channel axis, separate along/across reach for major and shallow-water
constituents, a smaller independent Chart Datum support, a quality weight and a
hard polygon. Missing constituent evidence no longer dilutes anchors which do
provide that constituent.

Five station transforms survived a 14-day training, seven-day acceptance and
untouched seven-day final gate: Avonmouth, Cromer, Immingham, Liverpool and
Weymouth. The final seven days were never used to fit parameters. Other anchors
still provide untransformed local harmonic evidence.

Candidate properties:

- authenticated, height-only XTD v2;
- 19 correction anchors and 779 supported 0.025-degree cells;
- 0.5 mm coefficient quantisation;
- local Chart Datum output with unsupported points masked;
- XTD SHA-256 `4107e27e91d1e56df074f577a8e2c46e4f59b5127da293714449f540f9222527`;
- full authoring lineage and calibration policy in the adjacent `.xtd.prv`,
  never in the `.xtd`.

## Full 28-day NTSLF result

Errors are mean absolute values. `unknown` is a deliberate fail-closed result,
not zero error.

| Station | Time base | Time v1 | Time v2 | Range base | Range v1 | Range v2 | Height v1 | Height v2 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Avonmouth | 21.770 min | 11.476 min | 9.744 min | 1.316 m | 0.455 m | 0.312 m | 0.259 m | 0.215 m |
| Cromer | 7.862 min | unknown | 5.483 min | 0.130 m | unknown | 0.063 m | unknown | 0.042 m |
| Dover | 13.475 min | 10.123 min | 10.135 min | 0.336 m | 0.174 m | 0.174 m | 0.104 m | 0.104 m |
| Fishguard | 6.561 min | unknown | 4.794 min | 0.053 m | unknown | 0.039 m | unknown | 0.027 m |
| Harwich | 16.593 min | unknown | unknown | 0.351 m | unknown | unknown | unknown | unknown |
| Heysham | 5.779 min | unknown | 5.860 min | 0.284 m | unknown | 0.110 m | unknown | 0.078 m |
| Holyhead | 7.219 min | 5.691 min | 5.693 min | 0.113 m | 0.052 m | 0.052 m | 0.031 m | 0.031 m |
| Immingham | 48.961 min | unknown | 6.299 min | 1.355 m | unknown | 0.093 m | unknown | 0.052 m |
| Liverpool | 9.677 min | 8.780 min | 5.615 min | 0.503 m | 0.098 m | 0.089 m | 0.135 m | 0.069 m |
| Newhaven | 5.586 min | unknown | 4.947 min | 0.216 m | unknown | 0.115 m | unknown | 0.060 m |
| Newlyn | 5.264 min | 3.665 min | 3.664 min | 0.116 m | 0.046 m | 0.046 m | 0.035 m | 0.035 m |
| Portsmouth | 26.207 min | 23.375 min | 23.375 min | 0.121 m | 0.123 m | 0.123 m | 0.102 m | 0.102 m |
| Sheerness | 18.567 min | unknown | 12.887 min | 0.300 m | unknown | 0.114 m | unknown | 0.068 m |
| Weymouth | 54.297 min | 40.469 min | 36.623 min | 0.111 m | 0.110 m | 0.111 m | 0.079 m | 0.069 m |

The clearest gains are at the difficult estuarine stations. Immingham falls
from 49.0 to 6.3 minutes and from 1.36 m to 0.09 m mean range error. Liverpool
falls from 9.7 to 5.6 minutes, 0.50 to 0.09 m range error, and reaches 0.07 m
mean absolute height error. Dover remains stable at about 10 minutes and 0.10 m
height error rather than being sacrificed to improve estuaries.

## Untouched final seven days

| Station | Time base | Time v1 | Time v2 | Range base | Range v1 | Range v2 | Height v1 | Height v2 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Avonmouth | 15.783 | 7.658 | 6.347 | 1.689 | 0.318 | 0.111 | 0.172 | 0.160 |
| Cromer | 8.058 | unknown | 5.915 | 0.132 | unknown | 0.046 | unknown | 0.031 |
| Dover | 15.806 | 12.671 | 12.691 | 0.439 | 0.238 | 0.238 | 0.124 | 0.124 |
| Fishguard | 5.026 | unknown | 2.873 | 0.058 | unknown | 0.029 | unknown | 0.031 |
| Heysham | 6.570 | unknown | 6.219 | 0.390 | unknown | 0.126 | unknown | 0.090 |
| Holyhead | 5.822 | 3.293 | 3.291 | 0.151 | 0.041 | 0.041 | 0.022 | 0.022 |
| Immingham | 47.667 | unknown | 7.409 | 1.535 | unknown | 0.054 | unknown | 0.033 |
| Liverpool | 12.496 | 8.084 | 4.593 | 0.304 | 0.117 | 0.092 | 0.138 | 0.052 |
| Newhaven | 4.031 | unknown | 3.900 | 0.208 | unknown | 0.091 | unknown | 0.051 |
| Newlyn | 4.293 | 2.674 | 2.673 | 0.078 | 0.042 | 0.042 | 0.038 | 0.038 |
| Portsmouth | 32.458 | 23.755 | 23.755 | 0.187 | 0.085 | 0.085 | 0.127 | 0.127 |
| Sheerness | 21.086 | unknown | 10.746 | 0.264 | unknown | 0.048 | unknown | 0.043 |
| Weymouth | 37.839 | 36.569 | 33.744 | 0.076 | 0.072 | 0.072 | 0.068 | 0.045 |

Harwich remains unknown because the local chart-datum evidence does not cover
it. This is scientifically preferable to claiming the nearby Felixstowe datum
applies across the estuary.

## Independent-publisher uncertainty

During development, predictions were verified against multiple local
authoritative sources. The second-publisher comparison inputs described here
were qualification inputs only: they are not used by the plugin, embedded in
`.xtdt`, or distributed in the public repository. The retained private
evidence showed genuine publisher spread as well as location-specific model
weaknesses; no source was selected merely because it produced the most
favourable comparison.

Liverpool checks outside the calibration month remain mixed: the May samples
improve substantially, while the January timing check regresses. The candidate
is therefore a developer-quality regional result, not yet a year-round
navigation certificate.
