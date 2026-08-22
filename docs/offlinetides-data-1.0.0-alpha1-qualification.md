# OfflineTides global data 1.0.0-alpha1 qualification

Date: 22 August 2026

## Exact release candidate

| Item | Bytes | SHA-256 |
| --- | ---: | --- |
| `offlinetides-global-v1.0.0-alpha1.xtdt` | 25,802,358 | `418f76d25c69122167876a56ea167b355134b97fbf711044f6002ea8ab17fd3f` |
| `offlinetides-global-data-1.0.0-alpha1.tar.gz` | 25,717,132 | `a19fa1745778a95b4740acf9052062b8f7d54333eb024f3bf2b27548cd9e50a2` |

The bundle is built reproducibly after the private authoring record is checked
against the operational package. Extraction followed by
`sha256sum -c SHA256SUMS` covers the operational file and manifest. The private
authoring record is deliberately excluded from the release.

## Authentication and distribution boundary

The release verifier authenticated XTD v2 package ID
`573ace60d283d4d6456d591fbda9294e`, checked the public dataset identity, and
read all 1,058 water-level harmonic, quality and vertical-datum tiles. The
package contains no tidal-current field. The private authoring record confirms
that no raw third-party source data are present and that no raw source model
is required at runtime.

The bundle contains only:

- the authenticated `.xtdt` operational package;
- a versioned JSON manifest and SHA-256 list; and
- field-use and installation instructions.

`.xtd` and `.xtdt` are independently designed, self-contained tidal-harmonic
formats. The released package contains only authenticated, quantised derived
coefficients and supporting quality/datum fields which the author is permitted
to distribute. It contains and requires no raw third-party source dataset.

## Official-source canaries

All 23 frozen authority-source canaries matched every published HW/LW event.
The complete candidate report was identical to the v0.9 operational package
report, confirming that the public identity change did not alter predictions.

| Set | Locations | Mean absolute time error | Mean absolute height error | Mean range error |
| --- | ---: | ---: | ---: | ---: |
| UK authoritative sources | 13 | 12.516 min | 0.139 m | 0.091 m |
| International authorities | 10 | 14.176 min | 0.103 m | 0.066 m |

Notable difficult cases remain visible rather than hidden: Avonmouth has a
31.8-minute mean event-time error, Portsmouth a 0.460 m mean height error,
Hamburg a 0.436 m mean height error, and Rio a 29.1-minute mean event-time
error. These stay within the Alpha's documented secondary-prediction boundary;
they are not station-specific runtime patches.

## Result

The data candidate passes the OfflineTides Alpha data gate. It remains an
astronomical prediction aid and does not include weather surge, river flow,
air-pressure effects or live sea-level anomaly.
