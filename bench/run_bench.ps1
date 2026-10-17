# run_bench.ps1 -- benchmarks the VM (and, for comparison, the tree-walking interpreter) on bench\bench_*.mylang.
#
# Run it from anywhere:
#     powershell -ExecutionPolicy Bypass -File bench\run_bench.ps1
#
# Options:
#     -Runs 5          timed runs per script on the VM; the best and the median are reported (default 5)
#     -TreeRuns 1      timed runs per script on the tree-walker, which is slow, so one run is the default
#     -SkipTree        benchmark the VM only (much faster)
#     -Name fib,*loop* only scripts whose name (without "bench_" and ".mylang") matches one of these wildcards
#     -Save            append the results, with date / git commit / machine, to bench\results.md
#     -Label "text"    a note stored with the saved results, e.g. "before break/continue"
#     -Exe path        the interpreter to use (default: mylang.exe in the repository root)
#
# For numbers that mean anything:
#   * build in RELEASE mode (cmake --build build --config Release, or g++ -O2). A Debug build, or any
#     sanitizer build, is many times slower and says nothing about real speed.
#   * close other heavy programs, and compare runs made on the same machine in the same state.
#   * trust the best time more than the median, and only trust differences larger than a few percent: even
#     the best of 5 runs of the same binary can vary by a few percent.
# The time includes starting the process (a few milliseconds), so very short scripts are not meaningful.
#
# Every script prints a few checksum values, so the runner also checks that each run produced the same output
# as the first run and that the VM and the tree-walker agree. A disagreement is reported as MISMATCH: a
# benchmark that got faster by computing the wrong answer is not a result.

param(
    [int]$Runs = 5,
    [int]$TreeRuns = 1,
    [switch]$SkipTree,
    [string[]]$Name,
    [switch]$Save,
    [string]$Label = '',
    [string]$Exe = ''
)

$ErrorActionPreference = 'Stop'
$benchDir = $PSScriptRoot
$repoDir  = Split-Path $benchDir -Parent
if (-not $Exe) {
    $Exe = Join-Path $repoDir 'mylang.exe'
    if (-not (Test-Path $Exe)) { $Exe = Join-Path $repoDir 'mylang' }  # Linux / macOS build
}
if (-not (Test-Path $Exe)) { throw "Cannot find the interpreter ($Exe). Build first, or pass -Exe." }
# With "powershell -File", "-Name fib,loop" arrives as ONE string "fib,loop", so split on commas here.
if ($Name) { $Name = @($Name | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
if ($Runs -lt 1) { throw "-Runs must be at least 1." }
if ($TreeRuns -lt 1) { throw "-TreeRuns must be at least 1." }

function Invoke-Timed([string]$engine, [string]$scriptPath) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $Exe
    if ($engine -eq 'vm') { $psi.Arguments = '--vm "' + $scriptPath + '"' } else { $psi.Arguments = '"' + $scriptPath + '"' }
    $psi.WorkingDirectory       = $repoDir
    $psi.UseShellExecute        = $false
    $psi.CreateNoWindow         = $true
    $psi.RedirectStandardInput  = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p  = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $outTask = $p.StandardOutput.ReadToEndAsync()   # read both streams at once so neither pipe can fill up and block
    $errTask = $p.StandardError.ReadToEndAsync()
    $p.WaitForExit()
    $sw.Stop()
    return [pscustomobject]@{
        Ms  = $sw.Elapsed.TotalMilliseconds
        Out = ($outTask.Result -replace "`r`n", "`n")
        Err = ($errTask.Result -replace "`r`n", "`n")
    }
}

function Get-Median([double[]]$values) {
    $s = @($values | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return $s[[int](($n - 1) / 2)] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

function Format-Ms([double]$ms) {
    return ([math]::Round($ms)).ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture) + ' ms'
}

# Runs one script on one engine $count times. Returns the times and whether every run gave the same output with no stderr.
function Measure-Engine([string]$engine, [string]$scriptPath, [int]$count) {
    $times = New-Object System.Collections.Generic.List[double]
    $first = $null
    $status = 'ok'
    for ($i = 0; $i -lt $count; $i++) {
        $r = Invoke-Timed $engine $scriptPath
        if ($r.Err.Trim().Length -gt 0) { $status = 'ERROR: ' + ($r.Err.Trim() -split "`n")[0] }
        if ($null -eq $first) { $first = $r.Out } elseif ($r.Out -cne $first -and $status -eq 'ok') { $status = 'MISMATCH (runs differ)' }
        $times.Add($r.Ms)
    }
    return [pscustomobject]@{
        Best = ($times | Measure-Object -Minimum).Minimum
        Median = Get-Median $times.ToArray()
        Output = $first
        Status = $status
    }
}

$all = Get-ChildItem $benchDir -Filter 'bench_*.mylang' | Sort-Object Name
if ($Name) {
    $all = $all | Where-Object {
        $short = $_.BaseName.Substring(6)
        $hit = $false
        foreach ($pattern in $Name) { if ($short -like $pattern) { $hit = $true } }
        $hit
    }
}
if (-not $all) { Write-Host "No benchmark scripts matched in $benchDir."; exit 1 }

Write-Host ("Interpreter: {0}  (built {1})" -f $Exe, (Get-Item $Exe).LastWriteTime)
Write-Host ("VM runs per script: {0}{1}" -f $Runs, $(if ($SkipTree) { '   (tree-walker skipped)' } else { ", tree-walker runs: $TreeRuns" }))
Write-Host "Reminder: use a Release build; Debug / sanitizer builds are not comparable." -ForegroundColor DarkGray
Write-Host ""

$rows = @()
$bad = 0
foreach ($file in $all) {
    $short = $file.BaseName.Substring(6)
    Write-Host ("  running {0} ..." -f $short) -NoNewline
    $vm = Measure-Engine 'vm' $file.FullName $Runs
    $tree = $null
    if (-not $SkipTree) { $tree = Measure-Engine 'tree' $file.FullName $TreeRuns }
    Write-Host " done"

    $check = $vm.Status
    if ($check -eq 'ok' -and $tree) {
        if ($tree.Status -ne 'ok') { $check = 'tree-walker ' + $tree.Status }
        elseif ($tree.Output -cne $vm.Output) { $check = 'MISMATCH (VM and tree-walker differ)' }
    }
    if ($check -ne 'ok') { $bad++ }

    $row = [ordered]@{
        Name       = $short
        VmBest     = Format-Ms $vm.Best
        VmMedian   = Format-Ms $vm.Median
        TreeBest   = '-'
        Speedup    = '-'
        Check      = $check
    }
    if ($tree) {
        $row.TreeBest = Format-Ms $tree.Best
        $row.Speedup  = ([math]::Round($tree.Best / $vm.Best, 1)).ToString('0.0', [System.Globalization.CultureInfo]::InvariantCulture) + 'x'
    }
    $rows += [pscustomobject]$row
}

Write-Host ""
# A plain padded table: Format-Table prints nothing useful when output is redirected, and this behaves the same everywhere.
$headers = @('Benchmark', 'VM best', 'VM median', 'Tree best', 'VM speedup', 'Check')
$cells = @()
foreach ($r in $rows) { $cells += ,@($r.Name, $r.VmBest, $r.VmMedian, $r.TreeBest, $r.Speedup, $r.Check) }
$widths = @()
for ($c = 0; $c -lt $headers.Count; $c++) {
    $w = $headers[$c].Length
    foreach ($row in $cells) { if ($row[$c].Length -gt $w) { $w = $row[$c].Length } }
    $widths += $w
}
function Format-Row([string[]]$values) {
    $parts = @()
    for ($c = 0; $c -lt $values.Count; $c++) {
        if ($c -eq 0 -or $c -eq 5) { $parts += $values[$c].PadRight($widths[$c]) } else { $parts += $values[$c].PadLeft($widths[$c]) }
    }
    return ($parts -join '  ').TrimEnd()
}
Write-Host (Format-Row $headers) -ForegroundColor White
Write-Host ((($widths | ForEach-Object { '-' * $_ }) -join '  '))
foreach ($row in $cells) {
    if ($row[5] -eq 'ok') { Write-Host (Format-Row $row) } else { Write-Host (Format-Row $row) -ForegroundColor Red }
}
Write-Host ""

if ($Save) {
    $commit = 'unknown'
    $dirty = ''
    if (Get-Command git -ErrorAction SilentlyContinue) {
        try {
            $c = (& git -C $repoDir rev-parse --short HEAD 2>$null)
            if ($c) { $commit = $c.Trim() }
            $s = (& git -C $repoDir status --porcelain 2>$null)
            if ($s) { $dirty = ' (uncommitted changes)' }
        } catch { }
    }
    $machine = $env:PROCESSOR_IDENTIFIER
    if (-not $machine) { $machine = [System.Environment]::OSVersion.VersionString }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('')
    $lines.Add(('## {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm')))
    $lines.Add('')
    $lines.Add(('- commit: {0}{1}' -f $commit, $dirty))
    if ($Label) { $lines.Add(('- note: {0}' -f $Label)) }
    $lines.Add(('- machine: {0}, {1} logical processors' -f $machine, [System.Environment]::ProcessorCount))
    $lines.Add(('- runs: {0} on the VM, {1} on the tree-walker' -f $Runs, $(if ($SkipTree) { 0 } else { $TreeRuns })))
    $lines.Add('')
    $lines.Add('| benchmark | VM best | VM median | tree best | VM speedup | check |')
    $lines.Add('|---|---:|---:|---:|---:|---|')
    foreach ($r in $rows) { $lines.Add(('| {0} | {1} | {2} | {3} | {4} | {5} |' -f $r.Name, $r.VmBest, $r.VmMedian, $r.TreeBest, $r.Speedup, $r.Check)) }
    $resultsPath = Join-Path $benchDir 'results.md'
    if (-not (Test-Path $resultsPath)) {
        [System.IO.File]::WriteAllText($resultsPath, "# Benchmark results`r`n`r`nAppended by run_bench.ps1 -Save. Compare runs made on the same machine, in Release builds.`r`n")
    }
    [System.IO.File]::AppendAllText($resultsPath, (($lines -join "`r`n") + "`r`n"))
    Write-Host ("Saved to {0}" -f $resultsPath) -ForegroundColor Cyan
}

if ($bad -gt 0) { Write-Host ("{0} benchmark(s) did not check out (see the Check column)." -f $bad) -ForegroundColor Red; exit 1 }
exit 0