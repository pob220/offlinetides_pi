# OfflineTides Alpha rebuild and publication plan

Date: 23 August 2026

## Scope of the first publication pass

The public product and OpenCPN package identity is `OfflineTides` /
`offlinetides_pi`. Its canonical public source repository is
`pob220/offlinetides_pi`; generated catalogue metadata, retained evidence and
the OpenCPN Alpha submission must all use this identity.

The initial Alpha publication used four native x86_64 Linux targets. The next
candidate expands this using genuine target executors and the same packaging
boundary:

| Target | Build image | Evidence class |
| --- | --- | --- |
| Debian 12 / Bookworm | `debian:bookworm` | build, tests, staged install, package |
| Debian 13 / Trixie | `debian:trixie` | build, tests, staged install, package |
| Ubuntu 22.04 / Jammy | `ubuntu:22.04` plus the OpenCPN PPA | build, tests, staged install, package |
| Ubuntu 24.04 / Noble | `ubuntu:24.04` | build, tests, staged install, package |
| Debian 12 ARM64 | native CircleCI ARM machine | build, tests, staged install, package |
| Flatpak 25.08 x86_64 | native x86_64 machine and Freedesktop SDK | extension build and package inspection |
| Flatpak 25.08 aarch64 | native ARM machine and Freedesktop SDK | extension build and package inspection |
| Windows x86 | Windows Server 2022 / Visual Studio 2022 | native OpenCPN ABI build, staged install and PE dependency inspection |
| macOS ARM64 | Apple Silicon / Xcode 16.4 | build, tests, staged install and Mach-O dependency inspection |

These are `build-and-package-only` results until the resulting archive is
installed into an isolated stock OpenCPN runtime. A successful compiler job is
not described as GUI or runtime qualification. The ARM, Flatpak, Windows and
macOS jobs are native rather than cross-builds, and their structured evidence
retains this distinction.

## Rebuild lessons adopted from xGRIB and xWeatherRouting

1. Configure test and distributable trees from clean directories. Stale CMake
   state and stale packages must not select or validate yesterday's artifact.
2. Resolve the archive and XML as one exact pair. Prefer a same-basename XML,
   while accepting Frontend2's different Flatpak XML name only when there is
   exactly one same-version candidate.
3. Put only CTest JUnit files in CircleCI's test-results path. Plugin metadata
   XML remains an artifact and must never be parsed as a JUnit report.
4. Build authoring tools in the test tree, but package from a second
   runtime-only tree. The archive must not contain `.xtdt`, raw source-model
   files, TPXO/TICON labels, NetCDF/HDF5 payloads, or a runtime link to the
   authoring libraries.
5. Retain logs, the exact archive/XML pair, checksums and a structured
   `result.json` for every target. The result classification says what was not
   run as well as what passed.
6. Ignore release tags in ordinary validation. Publication is a different
   parameter-gated workflow, rebuilds its artifacts, stops at a manual approval
   job and is restricted to the `alpha` branch.
7. Make the deployment context the only holder of the Cloudsmith token. Normal
   validation has no credential and no upload command.
8. Treat target metadata as part of the binary ABI contract. An archive is not
   portable merely because it was built on Linux; target, distribution,
   architecture, GTK/wxWidgets and OpenCPN API fields must match the candidate.
9. Preserve the user's live OpenCPN installation during automated rebuilds.
   Runtime smoke tests use isolated profiles and staged plugin directories.
10. Keep the operational `.xtdt` dataset outside the plugin archive. It is a
    separately authenticated, self-contained data product with a privately
    retained authoring record; raw source-model inputs are neither runtime
    dependencies nor redistributable plugin content.
11. Link JSONCPP, libsodium and zstd statically into release plugins. The
    archive gate inspects ELF, PE or Mach-O dependencies and rejects a package
    which assumes these libraries are installed on the user's host.

The first data product is
`offlinetides-global-data-1.0.0-alpha1.tar.gz`. It contains the authenticated
`offlinetides-global-v1.0.0-alpha1.xtdt`, a versioned JSON manifest, checksums
and field-use instructions. The authoring `.xtdt.prv` is verified before
packaging but retained privately and excluded from the release.
The release gate authenticates and exhaustively reads every component, reruns
the UK/international authoritative canaries against the renamed package,
verifies the private authoring-record/package hash binding and rejects any
missing raw-source boundary flag.

## Identity and compatibility

Changing `PACKAGE` from `xtidal` to `offlinetides` produces
`libofflinetides_pi.so` and `offlinetides_pi-*.tar.gz`. This lets an Alpha
package be tested without overwriting the installed X-Tidal binary. Public UI,
Plugin Manager and fallback forecast-source labels say OfflineTides. Internal
`xtidal` C++ namespaces and the versioned `xtidal-water-level-forecast` export
schema remain unchanged in 1.0.2 to avoid an unnecessary wire/data-format
break.

Settings are written under `/PlugIns/offlinetides_pi`. On first use, if that
group has no schema marker, OfflineTides reads the prior X-Tidal group as a
one-time compatibility source and then writes its own group. It never deletes
or rewrites the legacy group.

## Publication controls

The ordinary CircleCI workflow uses `run_workflow_deploy=false`. To prepare a
publication candidate, trigger the `alpha` branch with
`run_workflow_deploy=true`. The complete release matrix must pass before
`hold-for-alpha-approval` becomes available. Approval then permits only the
`deploy-alpha` job to use the restricted `offlinetides-deployment` context.

External prerequisites, deliberately not stored in source:

- the `pob220/offlinetides_pi` project followed by the CircleCI GitHub integration,
  with machine-executor builds enabled;
- an Open-Source GPL-3.0+ Cloudsmith raw repository named
  `pob220/offlinetides-alpha-oss`;
- an organization CircleCI context named `offlinetides-deployment`, restricted
  to this project and containing only `CLOUDSMITH_API_KEY`;
- explicit human approval after inspecting all retained target evidence.

The deployment script independently checks the `alpha` branch, the explicit
publication approval marker, the credential, metadata versions and exact
archive/XML pairs. Publication is not triggered by a normal push or tag.

## Alpha acceptance gate

Before approval:

- all five clean native Linux jobs pass the complete CTest suite;
- both Flatpak architectures, Windows x86 and macOS ARM64 pass their native
  build, packaging, metadata and dependency gates;
- each staged tree contains exactly one `libofflinetides_pi.so`;
- each archive has one unambiguous metadata partner and valid API 1.21/source
  identity;
- no archive includes an operational/raw dataset or links NetCDF/HDF5;
- checksums and structured evidence are retained;
- one package is installed and loaded in an isolated OpenCPN profile with an
  authenticated `.xtdt`, the forecast dialog opens, and unload/shutdown logs
  are clean;
- the live sailing OpenCPN profile and installed X-Tidal binary remain
  untouched.

Cloudsmith upload and OpenCPN catalogue submission are later, separately
authorized publication actions. This document makes the branch ready for that
gate; it does not claim that an upload has occurred.

## Pre-publication qualification snapshot

The exact CircleCI entry point was run locally in a clean Debian 12 container
on 22 August 2026. All public CTest suites passed, including the global canary
manifests, TICON import, chart-datum overrides and interchange fixtures.
Separate private qualification also used multiple local authoritative sources;
their prediction values are not part of the plugin or public repository.
The independently configured runtime-only tree then built, staged, packaged
and passed the archive and publication-contract gates.

The resulting Bookworm archive was 279,412 bytes and contained exactly one
runtime file, `libofflinetides_pi.so`. Its metadata identified OfflineTides
1.0.2.0, OpenCPN API 1.21, Debian 12 x86_64 and GTK3. A separate GCC 16
ASan/UBSan build also passed all six suites with fail-fast checks enabled;
LeakSanitizer was disabled because it cannot operate under this managed
environment's ptrace restrictions.

The definitive Alpha candidate is public commit
`2483ab116a983085e5dfcef47334ca1277f4e916`, qualified by CircleCI pipeline 22
on 23 August 2026. All nine native build/package jobs passed. Linux native
CTest suites passed; Windows and Flatpak remain build/package-qualified rather
than GUI-runtime-qualified pending Alpha field testing.
