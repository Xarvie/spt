# 生成覆盖率报告（本地使用）
# 用法: .\scripts\gen_coverage.ps1
param([string]$Preset = "clang-coverage")

Write-Host "Building with coverage preset..."
cmake --preset $Preset
cmake --build --preset $Preset

Write-Host "Running tests..."
ctest --preset $Preset --timeout 30

Write-Host "Generating coverage report..."
$root = (Get-Location).Path
$buildDir = "build-cov"
$profraw = Get-ChildItem "$buildDir" -Filter "*.profraw" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (!$profraw) {
    Write-Error "No .profraw file found. Did tests run?"
    exit 1
}

$profdataPath = "$root/coverage.profdata"
llvm-profdata merge -o $profdataPath $profraw.FullName
# llvm-cov 需要绝对路径，否则报 "could not read profile data"
llvm-cov show -format=html -output-dir=coverage-report `
    -instr-profile="$profdataPath" `
    "$root/$buildDir/bin/sptscript.exe" `
    "$root/src/vm/" "$root/src/codegen/" "$root/src/frontend/"

Write-Host "Coverage report generated at: coverage-report/index.html"
