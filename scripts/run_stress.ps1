# 运行压力测试（带 ASan）
# 用法: .\scripts\run_stress.ps1
param([string]$Preset = "clang-asan")

Write-Host "Building with sanitize preset..."
cmake --preset $Preset
cmake --build --preset $Preset

Write-Host "Running stress tests with ASan..."
# LeakSanitizer (detect_leaks) 仅 Linux 支持，Windows 上不可用
if ($IsLinux) {
    $env:ASAN_OPTIONS = "detect_leaks=1:abort_on_error=1:print_stacktrace=1"
    $env:UBSAN_OPTIONS = "print_stacktrace=1:abort_on_error=1"
} else {
    # Windows: clang-cl 动态链接 ASan，需把 clang_rt.asan_dynamic-x86_64.dll 所在目录加入 PATH
    $env:ASAN_OPTIONS = "abort_on_error=1:print_stacktrace=1"
    $asanDir = "C:/env/LLVM/lib/clang/22/lib/windows"
    if (Test-Path $asanDir) {
        $env:PATH = "$env:PATH;$asanDir"
    }
}

ctest --preset $Preset -R "14_stress" --output-on-failure

Write-Host "Stress tests complete."
