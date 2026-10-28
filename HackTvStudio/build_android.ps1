# HackTv Studio - Android APK build (Qt 6.11.1, qmake flow)
# Usage:  .\build_android.ps1 x86_64   (emulator)
#         .\build_android.ps1 arm64    (car head unit)
param([string]$Abi = "x86_64", [switch]$NoGl, [string]$QtVer = "6.11.1")
# Native tools (clang/gradle) write warnings to stderr; do NOT treat those as
# terminating errors. We check $LASTEXITCODE after each step instead.
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot

switch ($Abi) {
    "x86_64" { $kit = "android_x86_64"; $andAbi = "x86_64" }
    "x86"    { $kit = "android_x86"; $andAbi = "x86" }
    "arm64"  { $kit = "android_arm64_v8a"; $andAbi = "arm64-v8a" }
    "armv7"  { $kit = "android_armv7"; $andAbi = "armeabi-v7a" }
    default  { throw "Abi must be x86_64, x86, arm64 or armv7" }
}

$env:ANDROID_SDK_ROOT = "$env:LOCALAPPDATA\Android\Sdk"
$env:ANDROID_HOME      = $env:ANDROID_SDK_ROOT
$env:ANDROID_NDK_ROOT  = "$env:LOCALAPPDATA\Android\Sdk\ndk\android-ndk-r26b"
$env:JAVA_HOME         = "C:\Program Files\Android\Android Studio\jbr"
$qt   = "C:\Qt\$QtVer\$kit"
if (-not (Test-Path $qt)) { throw "Qt kit yok: $qt  (Maintenance Tool ile $QtVer / $kit kurulmali)" }
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

$suffix = ""
$qmakeArgs = @("$root\HackTvStudio.pro", "-spec", "android-clang", "CONFIG+=release", "ANDROID_ABIS=$andAbi")
if ($NoGl) {
    # Spectrum plotter uses QPainter only, so it can drop OpenGL entirely for head
    # units whose GL driver crashes the app on launch (software raster rendering).
    $qmakeArgs += "DEFINES+=PLOTTER_NO_GL"
    $suffix = "-nogl"
    Write-Host "== NO-GL build (software spectrum)"
}
Write-Host "== qmake ($kit)"
Run "$qt\bin\qmake.bat" $qmakeArgs "qmake_$Abi"

Write-Host "== make (compile native .so)"
Run $make @("-j8") "make_$Abi"

if ($QtVer -like "6.5*") {
    # Qt 6.5 ships AGP 7.4.1, whose aapt2 cannot parse the android-36 platform
    # (new resource-table format). androiddeployqt still generates the full Gradle
    # project before its own gradle run fails on that, so: run `make apk` once
    # (ignore that expected failure), rewrite the SDK levels to android-33, then
    # drive Gradle again ourselves.
    $andBuild = "$bdir\android-build"
    Write-Host "== make apk (ilk gecis; android-36 aapt2 hatasi bekleniyor)"
    & $make apk 2>&1 | Tee-Object "$root\log_apkgen_$Abi.txt" | Out-Null
    if (-not (Test-Path "$andBuild\gradlew.bat")) { throw "gradle projesi uretilemedi ($andBuild)" }

    Write-Host "== gradle.properties -> android-33 / build-tools 34 (AGP 7.4.1 uyumu)"
    $gp = "$andBuild\gradle.properties"
    $txt = Get-Content $gp -Raw
    $txt = $txt -replace "androidCompileSdkVersion=.*", "androidCompileSdkVersion=android-33"
    $txt = $txt -replace "qtTargetSdkVersion=.*", "qtTargetSdkVersion=33"
    $txt = $txt -replace "androidBuildToolsVersion=.*", "androidBuildToolsVersion=34.0.0"
    Set-Content $gp $txt -Encoding UTF8 -NoNewline

    Write-Host "== gradlew assembleDebug (android-33)"
    Run "$andBuild\gradlew.bat" @("-p", $andBuild, "assembleDebug") "gradle_$Abi"
} else {
    Write-Host "== make apk (androiddeployqt + gradle)"
    Run $make @("apk") "apk_$Abi"
}

$apk = Get-ChildItem -Recurse "$bdir" -Filter *.apk -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -match "debug|release" } | Select-Object -First 1
if ($apk) {
    Copy-Item $apk.FullName "$root\..\HackTvStudio-$Abi$suffix.apk" -Force
    Write-Host "== HAZIR: $root\..\HackTvStudio-$Abi$suffix.apk  ($([math]::Round($apk.Length/1MB,1)) MB)"
} else {
    throw "APK not produced under $bdir"
}
