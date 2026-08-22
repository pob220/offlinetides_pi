# X-Tidal worldwide official-source canaries — 2026-08-21

## Scope

Ten station-only XTDs were authored from the bundled OpenCPN non-US harmonic
station data and executed through the same plugin prediction engine used by
the UI. They are isolated qualification packages, not a distributable global
field. No raw TPXO data is used, shipped or required.

Each comparison uses a minimal one-day factual sample from a national
hydrographic, meteorological or maritime authority. Source time zones were
converted to UTC and heights to local Chart Datum. In particular, Rotterdam
was converted from NAP to OLW using the published 0.71 m offset, and Hamburg
from PNP to SKN using BSH's 3.10 m offset. Full source publications are linked,
not copied into the repository.

## Baseline results

| Station | Region | Events | Mean time | Max time | Mean height | Mean range |
|---|---|---:|---:|---:|---:|---:|
| Auckland | South Pacific | 4 | 5.125 min | 8.050 min | 0.066 m | 0.033 m |
| Wellington | South Pacific | 3 | 17.550 min | 31.617 min | 0.019 m | 0.044 m |
| Halifax | Northwest Atlantic | 4 | 8.312 min | 14.633 min | 0.083 m | 0.050 m |
| Sydney | Tasman Sea | 4 | 2.325 min | 4.200 min | 0.092 m | 0.009 m |
| Darwin | Timor Sea | 4 | 20.621 min | 44.950 min | 0.816 m | 1.566 m |
| Tokyo | Northwest Pacific | 2 | 3.758 min | 4.467 min | 0.027 m | 0.054 m |
| Rotterdam | Rhine-Meuse estuary | 4 | 16.183 min | 19.533 min | 0.169 m | 0.032 m |
| Hamburg | Elbe estuary | 4 | 21.400 min | 44.600 min | 0.444 m | 0.122 m |
| Rio de Janeiro | Southwest Atlantic | 4 | 91.621 min | 115.750 min | 0.162 m | 0.031 m |
| Reykjavík | North Atlantic | 4 | 4.058 min | 7.750 min | 0.169 m | 0.036 m |

A provisional envelope of 20 minutes mean event-time error, 0.20 m mean event
height error and 0.20 m mean tidal-range error accepts seven of ten canaries.
It rejects Darwin (height/range), Hamburg (time/height) and Rio (time). These
are useful failures: the suite is not tuned to make the first run look good.
The envelope remains provisional until a second authoritative publisher is
available at each station; observed publisher disagreement will then become
the local uncertainty floor.

Sydney also exposed duplicate bundled station records. The older explicitly
identified Royal Australian Navy record agrees closely with the current BOM
day; the nominal newer Harmgen record is about two hours displaced. Both
results are retained in the qualification notes rather than silently selecting
by name. Rio exposed and now tests feet-to-metres conversion in the authoring
path.

## Authoritative sources

- Auckland and Wellington: Toitū Te Whenua LINZ 2026 CSV tables.
- Halifax: Canadian Hydrographic Service station 00490 annual predictions.
- Sydney and Darwin: Australian Bureau of Meteorology publications.
- Tokyo: Japan Meteorological Agency Tokyo/Harumi tide table.
- Rotterdam: Rijkswaterstaat 2026 astronomical tide table.
- Hamburg: Bundesamt für Seeschifffahrt und Hydrographie St. Pauli data.
- Rio de Janeiro: Brazilian Navy CHM 2026 Ilha Fiscal table.
- Reykjavík: Icelandic Coast Guard `Sjávarfallatöflur 2026`.

## Reproduction

```sh
python3 tests/run_global_canaries.py \
  --author build/xtidal_build_station_xtd \
  --validator build/xtidal_validate_height \
  --harmonics /usr/share/opencpn/tcdata/HARMONICS_NO_US \
  --catalogue tests/global_canaries.json \
  --reference-root tests/reference \
  --output-dir /tmp/xtidal-global-canaries
```
