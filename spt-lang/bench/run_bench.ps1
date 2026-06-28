# ============================================================
# run_bench.ps1 — 运行 for_iter bench，对比 JIT 开/关性能
# 避免 PowerShell 函数返回 hashtable 的 unrolling 问题，
# 用 .NET Process 捕获 stdout，结果存入 [System.Collections.ArrayList]
# ============================================================
param(
    [string]$SptScript = "build\bin\sptscript.exe",
    [int]$Reps = 3,
    [string]$BenchFile = "bench\for_iter_baseline.spt",
    [string]$ResultsDir = "bench\results",
    [string]$ReportName = "baseline.md"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $SptScript)) { Write-Error "sptscript not found: $SptScript"; exit 1 }
if (-not (Test-Path $BenchFile)) { Write-Error "bench file not found: $BenchFile"; exit 1 }

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$sptAbs = (Resolve-Path $SptScript).Path
$benchAbs = (Resolve-Path $BenchFile).Path

# 用 .NET Process 捕获 stdout，返回原始文本
function Get-BenchOutput {
    param([string]$JitMode)
    if ($JitMode -eq "on") {
        $env:SPT_JIT = "on"; $env:SPT_JIT_HOT = "50"
    } else {
        $env:SPT_JIT = "0"; Remove-Item Env:SPT_JIT_HOT -ErrorAction SilentlyContinue
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $sptAbs
    $psi.Arguments = "`"$benchAbs`""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $proc.WaitForExit()
    return $stdout
}

# 从原始文本解析结果，返回 System.Collections.ArrayList of [hashtable]
# 每个元素: @{Name=...; Time=...; Sum=...}
function Parse-BenchOutput {
    param([string]$Text)
    $list = New-Object System.Collections.ArrayList
    foreach ($line in $Text -split "`n") {
        $line = $line.Trim()
        if ($line -match '^(\w+):\s+([\d.]+)s\s+sum=([+-]?\w+)\s*$') {
            $entry = @{ Name = $matches[1]; Time = [double]$matches[2]; Sum = $matches[3] }
            [void]$list.Add($entry)
        }
    }
    return ,$list
}

Write-Host "=== for_iter bench ==="
Write-Host "SptScript: $SptScript"
Write-Host "BenchFile: $BenchFile"
Write-Host "Reps: $Reps (best-of)"
Write-Host ""

# 收集 best-of-N 结果：用 hashtable 存 Name → best entry
$bestOff = @{}
$bestOn = @{}

Write-Host "Running JIT off..."
for ($i = 0; $i -lt $Reps; $i++) {
    $raw = Get-BenchOutput -JitMode "off"
    $parsed = Parse-BenchOutput -Text $raw
    Write-Host "  [off] rep $($i+1)/$Reps : $($parsed.Count) scenarios"
    foreach ($e in $parsed) {
        $name = $e['Name']
        $t = $e['Time']
        if (-not $bestOff.ContainsKey($name) -or $t -lt $bestOff[$name]['Time']) {
            $bestOff[$name] = $e
        }
    }
}

Write-Host "Running JIT on..."
for ($i = 0; $i -lt $Reps; $i++) {
    $raw = Get-BenchOutput -JitMode "on"
    $parsed = Parse-BenchOutput -Text $raw
    Write-Host "  [on] rep $($i+1)/$Reps : $($parsed.Count) scenarios"
    foreach ($e in $parsed) {
        $name = $e['Name']
        $t = $e['Time']
        if (-not $bestOn.ContainsKey($name) -or $t -lt $bestOn[$name]['Time']) {
            $bestOn[$name] = $e
        }
    }
}

# 收集场景名
$allNames = @()
foreach ($k in $bestOff.Keys) { $allNames += $k }
foreach ($k in $bestOn.Keys) { $allNames += $k }
$allNames = $allNames | Sort-Object -Unique

if ($allNames.Count -eq 0) {
    Write-Host "ERROR: no bench data captured."
    exit 1
}

# 生成报告
$report = New-Object System.Collections.ArrayList
[void]$report.Add("# for_iter 报告")
[void]$report.Add("")
[void]$report.Add("生成时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[void]$report.Add("SptScript: $SptScript")
[void]$report.Add("BenchFile: $BenchFile")
[void]$report.Add("Reps: $Reps (best-of)")
[void]$report.Add("")
[void]$report.Add("## 结果汇总")
[void]$report.Add("")
[void]$report.Add("| 场景 | JIT 关 (s) | JIT 开 (s) | 加速比 | sum 一致 |")
[void]$report.Add("|------|-----------|-----------|--------|---------|")

foreach ($name in $allNames) {
    $offHas = $bestOff.ContainsKey($name)
    $onHas = $bestOn.ContainsKey($name)
    $offTime = if ($offHas) { "{0:N4}" -f $bestOff[$name]['Time'] } else { "N/A" }
    $onTime  = if ($onHas) { "{0:N4}" -f $bestOn[$name]['Time']  } else { "N/A" }
    $speedup = "N/A"
    if ($offHas -and $onHas) {
        $ot = $bestOff[$name]['Time']; $nt = $bestOn[$name]['Time']
        if ($nt -gt 0) { $speedup = "{0:N2}x" -f ($ot / $nt) }
    }
    $match = if ($offHas -and $onHas -and ($bestOff[$name]['Sum'] -eq $bestOn[$name]['Sum'])) { "yes" } else { "N/A" }
    [void]$report.Add("| $name | $offTime | $onTime | $speedup | $match |")
}

[void]$report.Add("")
[void]$report.Add("## JIT 关 详细输出")
[void]$report.Add('```')
foreach ($name in ($bestOff.Keys | Sort-Object)) {
    $e = $bestOff[$name]
    [void]$report.Add("$name : $($e['Time'])s sum=$($e['Sum'])")
}
[void]$report.Add('```')
[void]$report.Add("")
[void]$report.Add("## JIT 开 详细输出")
[void]$report.Add('```')
foreach ($name in ($bestOn.Keys | Sort-Object)) {
    $e = $bestOn[$name]
    [void]$report.Add("$name : $($e['Time'])s sum=$($e['Sum'])")
}
[void]$report.Add('```')

$reportPath = Join-Path $ResultsDir $ReportName
($report -join "`n") | Out-File -FilePath $reportPath -Encoding utf8
Write-Host ""
Write-Host "Report saved to $reportPath"
Write-Host ""
Write-Host "=== Summary ==="
foreach ($name in $allNames) {
    $offHas = $bestOff.ContainsKey($name)
    $onHas = $bestOn.ContainsKey($name)
    $off = if ($offHas) { $bestOff[$name]['Time'] } else { 0 }
    $on  = if ($onHas) { $bestOn[$name]['Time']  } else { 0 }
    $speedup = if ($on -gt 0) { "{0:N2}x" -f ($off / $on) } else { "N/A" }
    Write-Host ("{0,-20} JIT off={1:N4}s  JIT on={2:N4}s  speedup={3}" -f $name, $off, $on, $speedup)
}
