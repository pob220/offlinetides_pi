# S-104 mapping decision — X-Tidal v0.9

Status: accepted for the v0.9 developer candidate. This is a mapping seam,
not an S-104 encoder.

## Decision

X-Tidal keeps `.xtdt` as its only operational package. Forecast JSON and CSV
use the versioned `xtidal-water-level-forecast` schema. The schema preserves
the information needed by a future, separately qualified mapper to IHO S-104
Edition 2.0.0, but v0.9 adds no HDF5 dependency, S-104 file writer, importer,
or visible S-104 control.

S-104 2.0.0 is the first operational edition and is based on S-100 5.2.0. It
models water-level observations, mathematical predictions and forecasts over
point or gridded coverages. An S-104 product is an authority-produced exchange
product with carrier metadata and HDF5 encoding; renaming X-Tidal JSON or
writing an approximate HDF5 hierarchy would not create a conformant product.

Normative starting points for later work:

- <https://registry.iho.int/productspec/view.do?category=product_ID&domainS=ALL&idx=209&product_ID=S-104&searchValue=&statusS=5>
- <https://iho.int/standards-and-specifications>

## Frozen semantic mapping

| X-Tidal forecast field | Intended S-104 semantic | Rule |
|---|---|---|
| `geometry` WGS84 point | feature-instance point geometry | Preserve longitude/latitude order and WGS84; a future grid exporter must describe its grid explicitly. |
| `samples[].valid_time_utc` | time record / time-point validity | UTC only; display-zone labels never enter the exchange model. |
| `samples[].water_level_m` | water-level height | Metres; never substitute zero for unknown or masked data. |
| `vertical_datum.identifier`, `name`, `epoch` | vertical-datum metadata | Map only through an authority-controlled identifier table. A free-text name is insufficient proof of equivalence. |
| `prediction_method` | data dynamicity and method/provenance metadata | `harmonic-astronomical` is a mathematical prediction, not an observation or surge-inclusive forecast. |
| `uncertainty_m` | quality / uncertainty metadata | Preserve metres and nullability; the X-Tidal combined estimate must not be claimed as an authority S-104 uncertainty class without qualification. |
| `package`, `source` | dataset and producer lineage | Preserve both runtime package identity and authoring-source identity. They are not an IHO producer code. |
| `samples[].state` | missing/invalid-data representation | `unknown` and `masked` map to the edition-defined missing-value mechanism, never to a numeric water level. |

The frozen fixture is
`tests/fixtures/s104-mapping-water-level-v1.json`. Tests protect the X-Tidal
side of this seam from accidental field loss.

## Deferred qualification work

Before an S-104 export can be called conformant, select a producer identity;
implement the exact Edition 2.0.0 carrier metadata and HDF5 layout; establish a
controlled vertical-datum code mapping; add spatial/temporal extent and issue
metadata; implement update/security rules where applicable; and run the
published S-158:104 validation checks plus independent reader interoperability
tests. That work should be an optional adapter, not part of the prediction
engine or `.xtdt` runtime.
