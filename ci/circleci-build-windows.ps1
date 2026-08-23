$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-NativeLogged(
    [scriptblock] $Command,
    [string] $LogPath,
    [string] $Operation
) {
    $savedPreference = $ErrorActionPreference
    $nativeExit = 1
    try {
        $ErrorActionPreference = "Continue"
        & $Command 2>&1 | Tee-Object -FilePath $LogPath
        $nativeExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($nativeExit -ne 0) {
        throw "$Operation failed with exit code $nativeExit"
    }
}

function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    $installation = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1)
    $root = Join-Path $installation "VC\Tools\MSVC"
    foreach ($toolset in @(Get-ChildItem $root -Directory | Sort-Object Name -Descending)) {
        $candidate = Join-Path $toolset.FullName "bin\Hostx64\x86\dumpbin.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    throw "Visual Studio dumpbin.exe was not found"
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo
$build = Join-Path $repo "build-windows"
$stage = Join-Path $repo "stage-windows"
$artifact = Join-Path $repo "artifacts\windows-x86"
$logDir = Join-Path $artifact "logs"
$testDir = Join-Path $artifact "tests"
$packageDir = Join-Path $artifact "package"
Remove-Item $build,$stage,$artifact -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $build,$stage,$logDir,$testDir,$packageDir | Out-Null

git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }

Invoke-NativeLogged `
    { choco install cmake nsis -y --no-progress } `
    (Join-Path $logDir "chocolatey-build-tools.log") `
    "Chocolatey build-tool installation"
Invoke-NativeLogged `
    { choco install gettext --version 1.0.0.20260310 -y --no-progress } `
    (Join-Path $logDir "chocolatey-gettext.log") `
    "Chocolatey gettext installation"
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:PATH = "$machinePath;$userPath;C:\Program Files\CMake\bin;$env:PATH"

$vcpkgVersion = "2026.06.24"
$vcpkg = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else {
    Join-Path $repo "cache\vcpkg-$vcpkgVersion"
}
if (-not (Test-Path (Join-Path $vcpkg ".git"))) {
    git clone --branch $vcpkgVersion --depth 1 `
        https://github.com/microsoft/vcpkg.git $vcpkg
    if ($LASTEXITCODE -ne 0) { throw "Pinned vcpkg checkout failed" }
}
if (-not (Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    & (Join-Path $vcpkg "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed" }
}

$triplet = "x86-windows-release-static"
$overlayTriplets = Join-Path $repo "ci\vcpkg-triplets"
$packages = Get-Content (Join-Path $repo "ci\windows-vcpkg-deps.txt") |
    Where-Object { $_.Trim() -and -not $_.Trim().StartsWith("#") } |
    ForEach-Object { $_.Trim() }
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $repo "cache\vcpkg-binary"
New-Item -ItemType Directory -Force $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
Invoke-NativeLogged {
    & (Join-Path $vcpkg "vcpkg.exe") install --triplet $triplet `
        "--overlay-triplets=$overlayTriplets" @packages
} (Join-Path $logDir "vcpkg.log") "vcpkg dependency installation"
& (Join-Path $vcpkg "vcpkg.exe") list | Set-Content -Encoding utf8 `
    (Join-Path $logDir "dependencies.log")

$wxVersion = "3.2.8"
$wxRoot = Join-Path $repo "cache\wxWidgets-$wxVersion"
New-Item -ItemType Directory -Force $wxRoot | Out-Null
$wxBase = "https://github.com/wxWidgets/wxWidgets/releases/download/v$wxVersion"
$downloads = [ordered]@{
    "wxWidgets-$wxVersion-headers.7z" = "86a2c99b4e9608b7cfc0b59e0f5a6d200a9d2541"
    "wxMSW-$($wxVersion)_vc14x_Dev.7z" = "65112a99d3e253796081d1ec80df294290403398"
    "wxMSW-$($wxVersion)_vc14x_ReleaseDLL.7z" = "44ceee6ddcbb6aa60de6b6fc26c57491c189477f"
}
foreach ($entry in $downloads.GetEnumerator()) {
    $path = Join-Path $env:TEMP $entry.Key
    if (-not (Test-Path $path)) {
        Invoke-WebRequest "$wxBase/$($entry.Key)" -OutFile $path
    }
    $actual = (Get-FileHash -Algorithm SHA1 $path).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) { throw "Checksum mismatch for $($entry.Key)" }
    & 7z x -y "-o$wxRoot" $path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not extract $($entry.Key)" }
}
$wxLib = Join-Path $wxRoot "lib\vc14x_dll"
$env:PATH = "$wxLib;$env:PATH"
$toolchain = Join-Path $vcpkg "scripts\buildsystems\vcpkg.cmake"

Invoke-NativeLogged {
    cmake -S $repo -B $build -G "Visual Studio 17 2022" -A Win32 `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DVCPKG_TARGET_TRIPLET=$triplet" `
        "-DVCPKG_OVERLAY_TRIPLETS=$overlayTriplets" `
        "-DwxWidgets_ROOT_DIR=$wxRoot" `
        "-DwxWidgets_LIB_DIR=$wxLib" `
        -DCMAKE_BUILD_TYPE=Release `
        -DXTIDAL_STANDALONE_API=ON `
        -DXTIDAL_BUILD_AUTHORING_TOOLS=OFF `
        -DOCPN_BUILD_TEST=OFF
} (Join-Path $logDir "configure.log") "Windows CMake configure"
Invoke-NativeLogged `
    { cmake --build $build --config Release --parallel 2 } `
    (Join-Path $logDir "build.log") "Windows build"
Invoke-NativeLogged `
    { cmake --install $build --config Release --prefix $stage } `
    (Join-Path $logDir "install.log") "Windows staged installation"

$plugin = Get-ChildItem $stage -Recurse -File -Filter "offlinetides_pi.dll" |
    Select-Object -First 1
if (-not $plugin) { throw "Staged OfflineTides DLL was not found" }
$dumpbin = Find-Dumpbin
$headers = & $dumpbin /headers $plugin.FullName 2>&1
$headers | Set-Content -Encoding utf8 (Join-Path $logDir "plugin-headers.log")
if (-not ($headers -match "14C machine \(x86\)")) {
    throw "Packaged OfflineTides plugin is not an x86 PE binary"
}
$dependencies = & $dumpbin /dependents $plugin.FullName 2>&1
$dependencies | Set-Content -Encoding utf8 `
    (Join-Path $logDir "plugin-dependencies.log")
if ($dependencies -match "(?i)(jsoncpp|sodium|zstd)\.dll") {
    throw "Windows package retains an unbundled OfflineTides runtime dependency"
}

Push-Location $build
Invoke-NativeLogged `
    { cpack -G TGZ -C Release --config CPackConfig.cmake } `
    (Join-Path $logDir "package.log") "Windows TGZ package"
Pop-Location
$archives = @(Get-ChildItem $build -File -Filter "offlinetides_pi-*.tar.gz")
if ($archives.Count -ne 1) { throw "Expected exactly one Windows TGZ package" }
$archive = $archives[0]
$metadataPath = $archive.FullName.Substring(
    0, $archive.FullName.Length - ".tar.gz".Length) + ".xml"
if (-not (Test-Path $metadataPath)) { throw "Windows package XML is missing" }
$metadata = Get-Item $metadataPath
if (-not (Select-String -Quiet -Path $metadata.FullName -Pattern "<target>msvc")) {
    throw "Windows metadata target is invalid"
}
Copy-Item $archive.FullName,$metadata.FullName $packageDir
$checksums = foreach ($file in @($archive,$metadata)) {
    $hash = (Get-FileHash -Algorithm SHA256 $file.FullName).Hash.ToLowerInvariant()
    "$hash  $($file.Name)"
}
$checksums | Set-Content -Encoding ascii (Join-Path $packageDir "checksums.txt")
$xml = [xml](Get-Content -Raw $metadata.FullName)
$version = $xml.plugin.SelectSingleNode("version").InnerText.Trim()
$result = [ordered]@{
    schema = "offlinetides-target-result-v1"
    target = "windows-x86"
    repository_commit = (git rev-parse HEAD)
    plugin_version = $version
    operating_system = "Windows Server 2022"
    architecture = "x86"
    compiler = "Visual Studio 2022 MSVC"
    build_status = "passed"
    test_status = "build-qualified"
    package_status = "passed"
    metadata_validation_status = "passed"
    installation_status = "staged-only"
    plugin_load_status = "not-run"
    package_filename = $archive.Name
    package_checksum_sha256 = (Get-FileHash -Algorithm SHA256 $archive.FullName).Hash.ToLowerInvariant()
    result_classification = "build-and-package-only"
}
$result | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 `
    (Join-Path $artifact "result.json")
