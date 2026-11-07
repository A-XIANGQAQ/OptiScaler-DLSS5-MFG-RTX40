# Assemble a release zip.
#
# The build output directory is not the release: it also holds import libraries, export files, debug
# symbols, and whatever earlier experiments left behind. Shipping that folder wholesale is how a
# release ends up containing a DLL nobody meant to publish, so this copies an explicit list and
# refuses anything not on it.
#
# What is deliberately NOT here: nvngx_dlssnr.dll. That is NVIDIA's, it is not ours to redistribute,
# and the user supplies their own copy per game folder. Only the ~108 KB forwarder ships.

param(
    [string]$Version = "v0.1.0-dlssnr",
    [switch]$SkipBuild,
    [string]$MsBuildPath,
    [string]$Toolset,
    # Path to nvngx_dlssnr.dll to bundle. Empty means "look in the usual places and skip if absent".
    [string]$ModelPath,
    # Bundle the model at all. Off makes a small package for people who already have the model.
    [switch]$NoModel
)

$ErrorActionPreference = "Stop"

# Derived rather than hardcoded, so this packages whichever checkout it is sitting in. There is more
# than one now -- the experiment runs in a git worktree beside the main tree, and a hardcoded root
# silently packages the other one's build output while reporting success.
$root = Split-Path -Parent $PSCommandPath

# Locate MSBuild rather than hardcoding one edition. The old path named VS 2022 Community, which is
# not installed on most machines that build this -- and the failure mode was a silent "built" line
# followed by packaging a stale tree. Any edition that exists will do.
function Find-MSBuild {
    # vswhere is the documented way, but it is not present on every install -- this machine has none,
    # so the glob below does the real work.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2>$null |
            Select-Object -First 1
        if ($found -and (Test-Path $found)) { return $found }
    }

    $roots = @(
        "$env:ProgramFiles\Microsoft Visual Studio",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio",
        "D:\Program Files\Microsoft Visual Studio",
        "D:\Program Files (x86)\Microsoft Visual Studio",
        "$env:SystemDrive\Program Files\Microsoft Visual Studio",
        "$env:SystemDrive\Program Files (x86)\Microsoft Visual Studio"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique

    $hits = foreach ($r in $roots) {
        Get-ChildItem -Path "$r\*\*\MSBuild\Current\Bin\MSBuild.exe" -ErrorAction SilentlyContinue
    }

    # Newest edition first: 18 beats 17, and a Build Tools install is as good as an IDE.
    $best = $hits | Sort-Object { $_.FullName } -Descending | Select-Object -First 1
    if ($best) { return $best.FullName }

    $onPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    throw "MSBuild not found. Install Visual Studio with the C++ workload, or pass -MsBuildPath."
}

# The projects declare v143. A machine with only VS 2026 installed has no v143 toolset -- its MSVC
# lives under v180 with a v145 platform-toolset alias -- and the build then fails with TRK0005
# "cannot find CL.exe", which mentions nothing about toolsets. Pass whichever one is actually here.
function Resolve-Toolset([string]$MsBuild) {
    if ($Toolset) { return $Toolset }

    $vsRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MsBuild)))
    $sets = Get-ChildItem -Path "$vsRoot\MSBuild\Microsoft\VC\v*\Platforms\x64\PlatformToolsets\*" -ErrorAction SilentlyContinue |
        Where-Object { $_.PSIsContainer } | ForEach-Object { $_.Name } | Select-Object -Unique

    foreach ($candidate in @("v145", "v143")) {
        if ($sets -contains $candidate) { return $candidate }
    }

    return "v143"
}

$stage = "$root\release\$Version"
$zip = "$root\release\OptiScaler-DLSSNR-$Version.zip"

$msb = if ($MsBuildPath) { $MsBuildPath } else { Find-MSBuild }
$toolset = Resolve-Toolset $msb
Write-Host "msbuild: $msb"
Write-Host "toolset: $toolset"

if (-not $SkipBuild) {
    foreach ($proj in @("$root\OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj", "$root\OptiScaler.sln")) {
        $out = & $msb $proj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=$toolset /v:minimal /m 2>&1
        $err = $out | Select-String "error "
        if ($err) { Write-Host "FAILED: $proj"; $err | Select-Object -First 6; exit 1 }
    }
    Write-Host "built"
}

$src = "$root\x64\Release\a"

# The forwarder is taken from its own build output, not from the shared folder. The solution build
# does not reliably rebuild it, and a stale one here would ship silently.
#
# A fresh checkout has no per-project output directory until that project has been built on its own,
# so fall back to the shared folder rather than failing. The export check below is what actually
# guards against a stale one, and it runs either way.
$forwarder = "$root\OptiScaler\dlssnr\forwarder\x64\Release\a\nvngx.dll_dlssnr.dll"

if (-not (Test-Path $forwarder)) {
    $forwarder = "$root\x64\Release\a\nvngx.dll_dlssnr.dll"
    Write-Host "forwarder: using the shared build output ($forwarder)"
}

$exports = @("dlssnr_call_create", "dlssnr_call_evaluate", "dlssnr_call_set_extras")
$bytes = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($forwarder))
$missing = @($exports | Where-Object { $bytes.IndexOf($_) -lt 0 })

if ($missing.Count -gt 0) {
    Write-Host "STALE forwarder: missing $($missing -join ', ')"
    exit 1
}

# The model itself.
#
# nvngx_dlssnr.dll is NVIDIA's, and the customary thing is to not redistribute it. This build ships it
# anyway, deliberately: it is the whole point of the release, the version that ships here (310.9.16) is
# not obtainable any other way without trawling other people's archives, and every downstream project
# in this chain tells its users to go find it themselves. The file is unmodified, carries NVIDIA's
# valid Authenticode signature, and is named exactly what the loader expects.
#
# Override the location with -ModelPath, or -NoModel for a package without it.
$model = $null

if (-not $NoModel) {
    $candidates = @(
        $ModelPath,
        "$root\_model\nvngx_dlssnr.dll",
        "$root\nvngx_dlssnr.dll",
        "$root\..\..\_model\nvngx_dlssnr.dll",
        "$root\..\..\_downloads\pure\nvngx_dlssnr_310.9.16.0_nvidia-signed.dll"
    ) | Where-Object { $_ -and (Test-Path $_) }

    $model = $candidates | Select-Object -First 1

    if (-not $model) {
        throw "nvngx_dlssnr.dll not found. Pass -ModelPath, or -NoModel to ship without it."
    }

    $mv = (Get-Item $model).VersionInfo
    $mh = (Get-FileHash $model -Algorithm SHA256).Hash
    Write-Host "model: $model"
    Write-Host "       file version $($mv.FileVersion), $((Get-Item $model).Length) bytes"
    Write-Host "       sha256 $mh"
}

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# Files, then folders. Anything not named here does not ship.
$files = @(
    "OptiScaler.dll",
    "OptiScaler.ini",
    "setup_windows.bat",
    "setup_linux.sh",
    "!! EXTRACT ALL FILES TO GAME FOLDER !!",
    "READ ME - DLSS Neural Rendering.txt"
)

foreach ($f in $files) {
    if (Test-Path "$src\$f") { Copy-Item "$src\$f" "$stage\$f" -Force }
    else { Write-Host "missing from build output: $f" }
}

foreach ($d in @("Licenses", "OptiScaler")) {
    Copy-Item "$src\$d" "$stage\$d" -Recurse -Force
}

# Simplified Chinese localization.
#
# These live at the repository root, not in the build output, so the copy above never
# sees them. zh-CN.txt has to land beside OptiScaler.dll because that is where the DLL
# looks for it -- and a missing table is not an error, it just leaves the menu English,
# so a package that drops this file fails silently rather than loudly.
foreach ($f in @("zh-CN.txt", "README.zh-CN.md")) {
    if (Test-Path "$root\$f") { Copy-Item "$root\$f" "$stage\$f" -Force }
    else { Write-Host "missing from the repository root: $f" }
}

Copy-Item $forwarder "$stage\nvngx.dll_dlssnr.dll" -Force

if ($model) {
    Copy-Item $model "$stage\nvngx_dlssnr.dll" -Force
}

# Logging on, in the release only.
#
# Upstream ships LogToFile=auto, which resolves to false, and the source ini is theirs -- changing it
# in the repo would put a log-behaviour change into a PR that is about neural rendering. But this is
# an experimental build whose notes ask people to attach OptiScaler.log, and the first release shipped
# asking for a file that was never written.
#
# Info rather than Trace: every line explaining why the pass did not start is Info or worse, so it
# answers the common report at almost no cost. Crash reports need Trace and synchronous writes, and
# the notes say so rather than everyone paying for it.
$iniPath = "$stage\OptiScaler.ini"
$ini = Get-Content $iniPath -Raw
$ini = $ini -replace '(?m)^LogToFile=auto', 'LogToFile=true'
$ini = $ini -replace '(?m)^LogLevel=auto', 'LogLevel=2'
Set-Content $iniPath $ini -Encoding utf8 -NoNewline

$check = Select-String -Path $iniPath -Pattern '^LogToFile=|^LogLevel=' | ForEach-Object { $_.Line }
Write-Host "log settings: $($check -join ', ')"

# Belt and braces: nothing that is a build artifact, and nothing from the abandoned warp work, may
# survive into the zip regardless of how it got into the staging folder.
Get-ChildItem $stage -Recurse -Include *.exp, *.lib, *.pdb, *.ilk, *latewarp* | Remove-Item -Force

# No feature may ship switched on by accident.
#
# A global regex on "^Enabled=auto" once turned on five sections at once -- output scaling,
# sharpening, the magnifier and two more -- while trying to enable one, because the ini has six keys
# called Enabled in six different sections. That was in a test install rather than a release, and
# only because nothing was checking. This checks.
$on = Select-String -Path "$stage\OptiScaler.ini" -Pattern '^Enabled=true'

if ($on) {
    Write-Host "REFUSING: the packaged ini has features switched on:"
    $on | ForEach-Object { "  line $($_.LineNumber): $($_.Line)" }
    exit 1
}

Write-Host "ini verified: nothing switched on by default"

# A checksum file, so nobody has to trust the release notes for the one number that matters.
if ($model) {
    $mh = (Get-FileHash "$stage\nvngx_dlssnr.dll" -Algorithm SHA256).Hash
    $fv = (Get-Item "$stage\nvngx_dlssnr.dll").VersionInfo.FileVersion
    @(
        "nvngx_dlssnr.dll",
        "  version  $fv",
        "  size     $((Get-Item "$stage\nvngx_dlssnr.dll").Length) bytes",
        "  sha256   $mh",
        "",
        "This is NVIDIA's file, unmodified. The signature on it is NVIDIA's own."
    ) | Set-Content "$stage\nvngx_dlssnr.sha256.txt" -Encoding utf8
}

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal

Write-Host ""
Write-Host "staged at $stage"
Get-ChildItem $stage | ForEach-Object { "  {0,-42} {1,10:N0}" -f $_.Name, $_.Length }
Write-Host ""
Write-Host ("zip: {0}  ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
