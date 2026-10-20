# HackTv Studio - Android APK build (Qt 6.11.1, qmake flow)
# Usage:  .\build_android.ps1 x86_64   (emulator)
#         .\build_android.ps1 arm64    (car head unit)
param([string]$Abi = "x86_64")
# Native tools (clang/gradle) write warnings to stderr; do NOT treat those as
# terminating errors. We check $LASTEXITCODE after each step instead.
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot

switch ($Abi) {
    "x86_64" { $kit = "android_x86_64"; $andAbi = "x86_64" }
    "arm64"  { $kit = "android_arm64_v8a"; $andAbi = "arm64-v8a" }
    default  { throw "Abi must be x86_64 or arm64" }
}

$env:ANDROID_SDK_ROOT = "$env:LOCALAPPDATA\Android\Sdk"
$env:ANDROID_HOME      = $env:ANDROID_SDK_ROOT
$env:ANDROID_NDK_ROOT  = "$env:LOCALAPPDATA\Android\Sdk\ndk\android-ndk-r26b"
$env:JAVA_HOME         = "C:\Program Files\Android\Android Studio\jbr"
$qt   = "C:\Qt\6.11.1\$kit"
$make = "C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe"

function Run($file, $arglist, $logname) {
    # Run a native tool, merge stderr into stdout, tee to a log, and fail only
    # on a non-zero exit code (not on stderr text).
    & $file @arglist 2>&1 | Tee-Object "$root\log_$logname.txt" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "$logname failed (exit $LASTEXITCODE)" }
}

$bdir = "$root\android-build-$Abi"
if (Test-Path $bdir) { Remove-Item -Recurse -Force $bdir -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force $bdir | Out-Null
Set-Location $bdir

Write-Host "== qmake ($kit)"
Run "$qt\bin\qmake.bat" @("$root\HackTvStudio.pro", "-spec", "android-clang", "CONFIG+=release", "ANDROID_ABIS=$andAbi") "qmake_$Abi"

Write-Host "== make (compile native .so)"
Run $make @("-j8") "make_$Abi"

Write-Host "== make apk (androiddeployqt + gradle)"
Run $make @("apk") "apk_$Abi"

$apk = Get-ChildItem -Recurse "$bdir" -Filter *.apk -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -match "debug|release" } | Select-Object -First 1
if ($apk) {
    Copy-Item $apk.FullName "$root\..\HackTvStudio-$Abi.apk" -Force
    Write-Host "== HAZIR: $root\..\HackTvStudio-$Abi.apk  ($([math]::Round($apk.Length/1MB,1)) MB)"
} else {
    throw "APK not produced under $bdir"
}
