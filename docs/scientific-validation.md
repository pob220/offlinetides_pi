# Scientific validation plan

X-Tidal must describe what it predicts and how well it predicts it. A visually
plausible arrow field or curve is not sufficient evidence.

## Two related, distinct products

1. **Tidal-stream prediction** is a vector field: east/north velocity, speed
   and direction at a position and UTC time. This is what the current XTD v1/v2
   runtime and first plugin version implement.
2. **Tidal-height prediction** is a scalar water-level curve relative to a
   stated datum. XTD v2 now has an authenticated water-level harmonic component,
   and the plugin can load a height-only package without manufacturing a current
   capability.

Both can use harmonic constituents, but they require different observations,
units, datums, masks and acceptance measures. They must never be silently
substituted for one another.

## Data separation

- A permitted source model may be used in an offline authoring environment to
  derive an XTD package.
- The end-user plugin reads XTD only and never accesses the source model.
- Calibration observations and validation observations are recorded separately.
- XTD provenance is never embedded in the `.xtd`. It is recorded in the
  same-name `.xtd.prv` sidecar agreed for this format.
- Validation periods and locations are held out from fitting where possible.
- Reference data is redistributed only when its licence permits it. Otherwise
  tests store fetch instructions, checksums and derived error summaries—not the
  restricted observations.

This makes accuracy claims reproducible without turning the source model into
a runtime or distribution dependency.

## Height acceptance measures

For each gauge and evaluation window, record:

- root-mean-square and 95th-percentile height error in metres;
- high- and low-water timing error in minutes;
- high- and low-water level error in metres;
- tidal-range error;
- phase and amplitude error for declared constituents;
- curve-shape error, including rise/fall asymmetry and secondary extrema;
- datum, sampling interval, observation quality flags and clock basis.

Separate astronomical prediction error from meteorological residual. Tests
should report both an unfiltered all-weather score and a fair-weather or
de-surgered astronomical score. The UI must state the datum and must not imply
that an astronomical prediction includes surge, river discharge or waves.

## Stream acceptance measures

For fixed instruments or transects, record:

- east/north component RMSE and bias in m/s;
- speed error in knots and circular direction error in degrees;
- slack-water timing error;
- peak flood/ebb speed and timing error;
- flood/ebb direction and reversal behavior;
- coverage, masked/unknown rate and depth represented by the observation.

Nearshore comparisons must match instrument depth or explicitly label the XTD
field as depth-mean/surface/other. A value in a channel must not be validated
against an instrument outside that channel simply because the coordinates are
close.

## Liverpool and other nonlinear sites

Liverpool/Lower Mersey is a required stress case, not a single pass/fail point.
The benchmark should include the port tide gauge for height and one or more
properly licensed channel-current observations for streams. It should examine
springs, neaps, equinoctial periods, high/low river flow when available, and
meteorologically quiet versus surge-affected windows.

If a regional harmonic field misses local curve shape, the response is not a
hidden correction in the UI. Candidate model improvements, in increasing
complexity, are:

1. preserve more shallow-water/overtide constituents in the XTD source;
2. use a higher-resolution estuary/channel field and wet/dry mask;
3. add a versioned local harmonic correction, with its source and calibration
   lineage recorded in the `.xtd.prv` sidecar;
4. use a calibrated hydrodynamic or response model when harmonic correction is
   inadequate.

Each correction has a spatial validity area, datum, calibration window,
independent validation window and uncertainty. Outside that validity area the
answer remains the regional prediction or unknown; it is never extrapolated
silently.

## Reference-data candidates

The initial height work should use authoritative gauge observations such as the
UK National Tide Gauge Network/NTSLF and BODC archive, subject to their current
terms. NOAA CO-OPS is a useful independent international comparison. Stream
validation will need licensed ADCP/current-meter observations or published
benchmark datasets with adequate metadata; tide-table arrows are not a
substitute for observations.

- UK NTSLF tidal predictions: <https://ntslf.org/tides/uk-network/predictions-uk-ireland>
- BODC sea-level data systems: <https://www.bodc.ac.uk/data/hosted_data_systems/sea_level/>
- NOAA CO-OPS data API: <https://api.tidesandcurrents.noaa.gov/api/prod/>

Exact station identifiers, coordinates, datums, access terms and redistribution
rights must be verified and frozen in the test manifest before downloading or
publishing reference records.

## Proposed gates

The exploratory gate reports errors without declaring navigation suitability.
Numerical acceptance thresholds are set only after the first independent
benchmark run shows the distribution of errors and the intended use is agreed.
Later gates should include:

- deterministic harmonic canaries at known inputs;
- multi-year hindcast at ordinary open-coast gauges;
- difficult-estuary cases including Liverpool, Avonmouth and Southampton;
- fast-stream cases including the Menai Strait, Portland/Pentland and Dover;
- explicit outside-coverage and missing-data cases;
- source-version comparison, so a newly derived XTD cannot regress silently.

All developer fixtures are labelled **not for navigation**. Only independently
validated datasets may carry an accuracy statement.
