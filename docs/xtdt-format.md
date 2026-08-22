# X-Tidal tide profile (`.xtdt`)

`.xtdt` is the X-Tidal profile of the authenticated XTD2 container.  The new
extension identifies a package intended for tide prediction; it does not fork
the authenticated container or its tile, compression and quantisation rules.
Existing `.xtd` files remain readable.

An `.xtdt` package declares `package_profile: "xtidal-tide-v1"` in authenticated
outer metadata.  It can contain these independent, tiled components:

| Capability | XTD2 component | Required |
|---|---|---|
| Water-level harmonic coefficients and vertical datum | `water_level_harmonics` | no |
| Tidal-current harmonic coefficients | `deterministic_tide` | no |
| Water-level uncertainty and support class | `water_level_quality` | no |
| Model-MSL to local Chart Datum transform | `vertical_datum_transform` | no |
| Tidal-current uncertainty | `climatological_uncertainty` | no |
| Expected non-tidal/seasonal current | `climatological_residual` | no |

At least one water-level or tidal-current component is required.  A height-only
v0.5 file is therefore valid, and a later combined height-and-stream file uses
the same profile.  Absence of a component means unavailable, never zero.

Water-level support classes are stable wire values: `0` unknown, `1` primary
background, `2` coastal observation-constrained, `3` river/estuary
observation-constrained, and `4` independent-model coverage fallback.  Class
4 was added for v0.6 so a client cannot mistake a hole filled by a secondary
model for ordinary primary-model support. Older readers reject that value
rather than misinterpreting it.

The optional vertical-datum component stores three quantised continuous
fields per wet grid point: the metres to add to model-MSL height, estimated
transform uncertainty in metres, and distance to the nearest datum station in
kilometres. It also stores a datum-realisation class, support class and
contributing-station count. Realisation values are `1` LAT, `2` MLLW, `3`
LLWLT, `4` NLLW, `5` MLWS, `6` MSL, `7` TLT/LLW and `8` another explicitly
authority-defined chart datum. Support values are `1` authority grid, `2`
station constrained, `3` estuary station constrained and `4` model derived.

The harmonic component remains model-MSL. A reader obtains height above local
Chart Datum only by adding an authenticated, unmasked transform value. Missing
or invalid transform data is `unknown`; it must never be treated as zero or
safe for chart-depth arithmetic. The selected national realization is
metadata about the local chart reference, not a claim that different national
datums are globally interchangeable.

Source lineage, licences, source filenames, authoring parameters and validation
evidence are forbidden from the operational container.  They belong in the
adjacent sidecar with the complete filename plus `.prv`, for example:

```
global-tides-v0.5.xtdt
global-tides-v0.5.xtdt.prv
```

Raw TPXO, FES, HAMTIDE and TICON files are offline authoring inputs only.  The
runtime package contains only derived, quantised operational coefficients and
quality values and must neither embed nor require those source datasets.
