# 运行 clang-tidy 静态分析（本地使用）
# 用法: .\scripts\run_tidy.ps1
# 需要本地有 clang 和 ninja（clang-tidy 需要 compile_commands.json）
param([string]$BuildDir = "build-tidy")

Write-Host "Configuring for clang-tidy (compile_commands.json)..."
cmake -B $BuildDir -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

Write-Host "Running clang-tidy on src/..."
$sources = Get-ChildItem -Path src -Filter "*.c" -Recurse
$warningCount = 0
foreach ($src in $sources) {
    Write-Host "  Checking $($src.FullName)..."
    $output = clang-tidy -p $BuildDir $src.FullName 2>&1
    $output | ForEach-Object { Write-Host $_ }
    if ($output -match "warning:") { $warningCount++ }
}

Write-Host "clang-tidy analysis complete. Files with warnings: $warningCount"
