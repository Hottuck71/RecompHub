# Builds RecompHub.exe into dist\ with MinGW-w64 gcc.
#   .\build.ps1                      gcc on PATH
#   .\build.ps1 -Toolchain <folder>  a MinGW folder with bin\ include\ lib\ (any recomp's builder\toolchain works)
#   .\build.ps1 -Test                also builds and runs tests\hub_test.c
param([string]$Toolchain = "", [switch]$Test)
$ErrorActionPreference = 'Continue'  # gcc warnings go to stderr; exit codes decide
$root = $PSScriptRoot
if ($Toolchain) {
    $env:PATH = "$Toolchain\bin;$env:PATH"
    $inc = @("-I", "$Toolchain\include"); $lib = @("-L", "$Toolchain\lib")
} else { $inc = @(); $lib = @() }
New-Item -ItemType Directory -Force "$root\dist", "$root\obj" | Out-Null
$libs = "-lwinhttp", "-lws2_32", "-lcomctl32", "-lcomdlg32", "-lshell32", "-lole32", "-luuid", "-ladvapi32", "-lgdi32", "-luser32"
Push-Location "$root\src"
try {
    # --preprocessor=gcc: windres can't run its default preprocessor from a folder with spaces in its path
    windres hub.rc --preprocessor=gcc --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED @inc -O coff -o "$root\obj\hub_rc.o"
    if ($LASTEXITCODE) { throw "windres failed" }
    gcc -O2 -Wall -municode -mwindows -static @inc @lib -o "$root\dist\RecompHub.exe" hub.c "$root\obj\hub_rc.o" @libs
    if ($LASTEXITCODE) { throw "gcc failed" }
    if ($Test) {
        gcc -O0 -g -Wall -municode -static @inc @lib -I . -o "$root\obj\hub_test.exe" "$root\tests\hub_test.c" @libs
        if ($LASTEXITCODE) { throw "test build failed" }
        & "$root\obj\hub_test.exe"
        if ($LASTEXITCODE) { throw "tests failed" }
    }
} finally { Pop-Location }
Get-Item "$root\dist\RecompHub.exe" | Select-Object Name, Length
