<#
.SYNOPSIS
    Compute UNION line coverage over qvim's own src/ + include/ from a cobertura
    report and enforce a floor. Fails (exit 1) if coverage regresses below it.

.DESCRIPTION
    Every qvim test executable statically links qvim_lib, so a cobertura report
    produced by Microsoft.CodeCoverage.Console contains one <class> per (source
    file, test module) pair — the same source line appears dozens of times, once
    per module that linked it. The report's own line-rate / lines-covered totals
    therefore double-count massively and are meaningless as a project figure.

    This script collapses that: a source line counts as covered if ANY module
    hit it, and each (file, line) is counted exactly once. Only files under
    qvim's src/ or include/ are considered (the collector's runsettings already
    restricts Sources, but we re-filter defensively so a stray path can't skew
    the number).

    The floor is stored as BOTH an absolute covered-line count and a ratio,
    because the current baseline (1996 / 2595 = 76.917%) sits just under a naive
    77% and a rounded gate would go red immediately. Ratchet -MinCovered /
    -MinRatio upward as issue #40 raises coverage.

.PARAMETER CoberturaPath
    One or more paths to cobertura XML emitted by the coverage collector. When
    several are given (one per parallel test tier), their per-line coverage is
    unioned: a line counts as covered if ANY report hit it. Because every test
    carries exactly one tier label, the union of the tiers equals a single
    whole-suite run, so the floor is identical either way.

.PARAMETER MinCovered
    Minimum number of distinct covered source lines required (floor).

.PARAMETER MinRatio
    Minimum covered/total ratio required (floor), 0..1.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$CoberturaPath,
    [int]$MinCovered = 1996,
    [double]$MinRatio = 0.769
)

$ErrorActionPreference = "Stop"

# key "<normalized-file>|<line>" -> $true once any module in any report hit it.
$covered = @{}
# key "<normalized-file>|<line>" present (value ignored) -> line exists at all.
$all = @{}

foreach ($path in $CoberturaPath) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Error "cobertura report not found: $path"; exit 1
    }
    [xml]$report = Get-Content -LiteralPath $path -Raw
    foreach ($pkg in $report.coverage.packages.package) {
        foreach ($cls in $pkg.classes.class) {
            $file = [string]$cls.filename
            if ($file -notmatch '\\(src|include)\\') { continue }
            $norm = $file.ToLowerInvariant()
            foreach ($ln in $cls.lines.line) {
                $key = "$norm|$($ln.number)"
                $all[$key] = $true
                if ([int]$ln.hits -gt 0) { $covered[$key] = $true }
            }
        }
    }
}

$total       = $all.Count
$coveredCnt  = $covered.Count
$ratio       = if ($total -gt 0) { $coveredCnt / $total } else { 0 }
$ratioPct    = [math]::Round($ratio * 100, 3)

Write-Host "Union line coverage over src/ + include/:"
Write-Host ("  covered lines : {0}" -f $coveredCnt)
Write-Host ("  total lines   : {0}" -f $total)
Write-Host ("  ratio         : {0}% ({1})" -f $ratioPct, ([math]::Round($ratio, 5)))
Write-Host ""
Write-Host ("Floor: covered >= {0} AND ratio >= {1} ({2}%)" -f $MinCovered, $MinRatio, [math]::Round($MinRatio * 100, 3))

$fail = $false
if ($coveredCnt -lt $MinCovered) {
    Write-Host ("FAIL: covered lines {0} < floor {1}." -f $coveredCnt, $MinCovered) -ForegroundColor Red
    $fail = $true
}
if ($ratio -lt $MinRatio) {
    Write-Host ("FAIL: ratio {0} < floor {1}." -f ([math]::Round($ratio, 5)), $MinRatio) -ForegroundColor Red
    $fail = $true
}

if ($fail) {
    Write-Error "Coverage regressed below the floor. Raise coverage (see issue #40) or, if this is an intentional ratchet, update -MinCovered / -MinRatio."
    exit 1
}

Write-Host "OK: coverage meets the floor." -ForegroundColor Green
exit 0
