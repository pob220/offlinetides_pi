# Proposed OpenCPN tide and water-level provider APIs

Target: a future OpenCPN API after 1.21. This document is deliberately
separate from X-Tidal's implementation so current plugin compatibility is not
changed.

## V1.0 developer-candidate seam

The v1.0 development core implements the station half of this proposal as a
runtime-resolved C compatibility bridge and an API 1.22 prototype. X-Tidal is
still compiled against API 1.21, so the same plugin binary runs on an
unmodified host and fails closed when the bridge is absent. The seam retains
dataset name/version, record provenance, stable identity, datum status and
equivalence, metric `Z0`, reference/subordinate status and metre
height output. This validates the value model without making the experimental
surface a permanent public ABI.

The generic `WaterLevelProvider` registration below remains a proposal. It is
not implemented or exposed in v1.0.

## Problem in API 1.21

`GetNearestTideStation()` returns one tide station with only index, name and
position. `GetTideHeight()` returns an untyped float. A plugin cannot discover
the plural nearby choices, source dataset, station kind, vertical datum,
datum epoch, validity, prediction method or uncertainty. Consequently it is
unsafe to calculate a height difference between a plugin forecast and the
native result.

## Backwards-compatible plural station query

Keep the API 1.21 calls and add value-returning types along these lines:

```cpp
struct TideStationQuery {
  Wgs84Position position;
  double maximum_distance_nm{100.0};
  std::size_t maximum_results{4};
  TideStationKind kind{TideStationKind::kWaterLevel};
};

struct TideStationInfo {
  std::string stable_id;
  std::string display_name;
  Wgs84Position position;
  double distance_nm;
  TideStationKind kind;
  std::string source_dataset_id;
  std::string source_dataset_name;
  VerticalDatumInfo vertical_datum;
};

virtual Result<std::vector<TideStationInfo>> GetNearestTideStations(
    const TideStationQuery& query) const;
```

The result must be ordered by distance, use stable IDs rather than exposing
internal IDX indices as identity, and return fewer than the requested count
without treating that as an error.

## Typed water-level provider

```cpp
enum class WaterLevelState { kKnown, kUnknown, kMasked };
enum class WaterLevelMethod {
  kObservation, kHarmonicPrediction, kHydrodynamicForecast, kOther
};

struct WaterLevelRequest {
  Wgs84Geometry geometry;
  UtcTime start;
  UtcTime end;
  std::chrono::seconds interval;
  std::optional<std::string> provider_id;
};

struct WaterLevelSample {
  UtcTime valid_time;
  WaterLevelState state;
  std::optional<double> water_level_m;
  std::optional<double> uncertainty_m;
};

struct WaterLevelForecast {
  Wgs84Geometry geometry;
  VerticalDatumInfo vertical_datum;
  WaterLevelMethod method;
  ProviderProvenance provenance;
  std::vector<WaterLevelSample> samples;
};

class WaterLevelProvider {
 public:
  virtual ~WaterLevelProvider() = default;
  virtual ProviderCapabilities Capabilities() const = 0;
  virtual Result<WaterLevelForecast> Forecast(
      const WaterLevelRequest&, Deadline, CancellationToken) = 0;
};
```

Provider registration needs explicit ownership and deregistration semantics;
no callbacks after deregistration or shutdown; bounded requests; documented
thread affinity; immutable values; and no GUI, JSON, `IDX_entry*` or plugin
private structures in the contract. `unknown` and `masked` must never be
converted to a valid zero height.

## Datum rule

`VerticalDatumInfo` needs at least identifier, authority, name, epoch and an
explicit equivalence key. Two results are height-comparable only when OpenCPN
can establish equivalence from those typed fields. Matching free-text names is
not sufficient.

The existing API 1.21 methods can be implemented as compatibility adapters
over the first plural result, preserving existing plugins while allowing new
consumers to be correct.
