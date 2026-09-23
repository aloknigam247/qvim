<#
.SYNOPSIS
    Enforce 100% coverage of the lines a change adds or modifies (patch / diff
    coverage). Fails (exit 1) if any changed, coverable, non-exempt line is left
    uncovered. Works for both the C++ side (cobertura reports) and the Android
    side (JaCoCo reports).

.DESCRIPTION
    The overall floor guards the whole codebase; this guard is per-change: every
    executable line a PR touches (under the configured diff paths) must be
    exercised by tests. It complements the floor — a PR can sit above the floor
    while still shipping untested new code, which this catches.

    Method:
      1. Diff the merge-base of -BaseRef and HEAD against HEAD, restricted to
         -DiffPath, and collect the NEW-side line numbers of added / modified
         lines per file (keyed by repo-relative path).
      2. Union the coverage report(s) into a per-(file,line) hit map (a line is
         covered if ANY report hit it). Coverage is collected against HEAD, so
         report line numbers and the diff's new-side line numbers refer to the
         same source.
      3. A changed line is CONSIDERED only if it appears as an executable line in
         a report (comments, blanks, braces, declarations, and excluded classes
         never appear, so they are not counted — standard diff-coverage
         semantics; JaCoCo excludes therefore pass through untouched).
      4. Considered lines are dropped when exempted by a source marker (below).
         Every remaining considered line must have hits > 0.

    Exemption markers (read from the HEAD source, since neither coverage tool has
    reliable per-line C++/Kotlin exclusion): a line carrying a trailing
    "// no-cover" is exempt; a "// no-cover:start" ... "// no-cover:end" pair
    exempts the enclosed block. The "//" form is valid in both C++ and Kotlin.
    Use them ONLY for genuinely uncoverable lines — GUI / SceneGraph glue that
    needs a live surface, platform calls, or coverage-tool artifacts. Each marker
    is visible in the diff, so an exemption is a reviewable decision. The gate
    prints every exempted line so reviewers can audit them.

.PARAMETER ReportPath
    One or more coverage XML reports (cobertura or JaCoCo). Unioned per line.

.PARAMETER BaseRef
    The ref the change is measured against (e.g. origin/main or the PR base sha).
    The diff runs from merge-base(BaseRef, HEAD) to HEAD.

.PARAMETER Format
    Report format: "cobertura" (default, C++) or "jacoco" (Android).

.PARAMETER DiffPath
    Pathspecs restricting the diff to production sources. Defaults to the C++
    "src"/"include"; pass "android/app/src/main" for the Android module.

.PARAMETER SourceRoot
    JaCoCo only: repo-relative source root prepended to "<package>/<sourcefile>"
    to reconstruct a report line's repo-relative path so it matches git's paths.

.PARAMETER MinPatchRatio
    Required covered/considered ratio for changed lines. Defaults to 1.0 (100%).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][Alias("CoberturaPath")][string[]]$ReportPath,
    [Parameter(Mandatory)][string]$BaseRef,
    [ValidateSet("cobertura", "jacoco")][string]$Format = "cobertura",
    [string[]]$DiffPath = @("src", "include"),
    [string]$SourceRoot = "android/app/src/main/java",
    [double]$MinPatchRatio = 1.0
)

$ErrorActionPreference = "Stop"

# Match key: repo-relative, forward-slashed, lower-cased. Both the git diff paths
# and each report's reconstructed paths are reduced to this so they collide.
function Get-MatchKey([string]$path) { return $path.Replace("\", "/").ToLowerInvariant() }

# A cobertura class filename is an absolute build path; reduce it to its
# repo-relative src/... or include/... tail so it matches git's paths.
function Get-CoberturaKey([string]$path) {
    $p = $path.Replace("\", "/").ToLowerInvariant()
    if ($p -match "(^|/)((src|include)/.+)$") { return $Matches[2] }
    return $null
}

$mergeBase = (git merge-base $BaseRef HEAD).Trim()
if (-not $mergeBase) { Write-Error "could not find merge-base of $BaseRef and HEAD."; exit 1 }
Write-Host "patch coverage ($Format): diffing $mergeBase..HEAD over $($DiffPath -join ', ')"

# --- 1. changed: key -> @{ Path = <original repo-rel>; Lines = <hashset int> } --
$changed = @{}
$curKey = $null
$newLine = 0
$diff = git diff --unified=0 --diff-filter=AMR $mergeBase HEAD -- $DiffPath
foreach ($line in $diff) {
    if ($line.StartsWith("+++ ")) {
        $raw = $line.Substring(4).Trim()
        if ($raw -eq "/dev/null") { $curKey = $null; continue }
        $orig = $raw -replace "^b/", ""
        $curKey = Get-MatchKey $orig
        if (-not $changed.ContainsKey($curKey)) {
            $changed[$curKey] = @{ Path = $orig; Lines = [System.Collections.Generic.HashSet[int]]::new() }
        }
        continue
    }
    if ($line.StartsWith("@@")) {
        if ($line -match "\+(\d+)(?:,(\d+))?") { $newLine = [int]$Matches[1] }
        continue
    }
    if ($null -eq $curKey) { continue }
    if ($line.StartsWith("+") -and -not $line.StartsWith("+++")) {
        [void]$changed[$curKey].Lines.Add($newLine)
        $newLine++
    }
    # With --unified=0 there are no context lines; "-" lines and diff metadata do
    # not exist on the new side, so the cursor only advances on "+".
}

# --- 2. union hit map: "key|line" -> summed hits ----------------------------
$hits = @{}
foreach ($path in $ReportPath) {
    if (-not (Test-Path -LiteralPath $path)) { Write-Error "report not found: $path"; exit 1 }
    [xml]$report = Get-Content -LiteralPath $path -Raw
    if ($Format -eq "cobertura") {
        foreach ($pkg in $report.coverage.packages.package) {
            foreach ($cls in $pkg.classes.class) {
                $key = Get-CoberturaKey ([string]$cls.filename)
                if (-not $key) { continue }
                foreach ($ln in $cls.lines.line) {
                    $k = "$key|$($ln.number)"
                    if (-not $hits.ContainsKey($k)) { $hits[$k] = 0 }
                    $hits[$k] += [int]$ln.hits
                }
            }
        }
    } else {
        foreach ($pkg in $report.report.package) {
            foreach ($sf in $pkg.sourcefile) {
                $key = Get-MatchKey "$SourceRoot/$($pkg.name)/$($sf.name)"
                foreach ($ln in $sf.line) {
                    $k = "$key|$($ln.nr)"
                    if (-not $hits.ContainsKey($k)) { $hits[$k] = 0 }
                    $hits[$k] += [int]$ln.ci   # covered instructions; >0 => covered
                }
            }
        }
    }
}

# --- 3+4. evaluate considered lines, honouring exemption markers ------------
$considered = 0
$coveredCnt = 0
$uncovered = [System.Collections.Generic.List[string]]::new()
$exempted = [System.Collections.Generic.List[string]]::new()

foreach ($key in ($changed.Keys | Sort-Object)) {
    $entry = $changed[$key]
    if ($entry.Lines.Count -eq 0) { continue }

    # Resolve exemption markers from the HEAD source (original-case path so it
    # reads correctly on case-sensitive filesystems).
    $exemptLines = [System.Collections.Generic.HashSet[int]]::new()
    if (Test-Path -LiteralPath $entry.Path) {
        $srcLines = Get-Content -LiteralPath $entry.Path
        $inBlock = $false
        for ($i = 0; $i -lt $srcLines.Count; $i++) {
            $text = $srcLines[$i]
            if ($text -match "//\s*no-cover:start") { $inBlock = $true }
            if ($inBlock -or ($text -match "//\s*no-cover(?!:)")) { [void]$exemptLines.Add($i + 1) }
            if ($text -match "//\s*no-cover:end") { $inBlock = $false }
        }
    }

    foreach ($ln in ($entry.Lines | Sort-Object)) {
        $k = "$key|$ln"
        if (-not $hits.ContainsKey($k)) { continue } # non-executable / excluded — not considered
        $where = "$($entry.Path):$ln"
        if ($exemptLines.Contains($ln)) { $exempted.Add($where); continue }
        $considered++
        if ($hits[$k] -gt 0) { $coveredCnt++ } else { $uncovered.Add($where) }
    }
}

$ratio = if ($considered -gt 0) { $coveredCnt / $considered } else { 1.0 }
$ratioPct = [math]::Round($ratio * 100, 3)

Write-Host ""
Write-Host "Patch coverage over changed lines:"
Write-Host ("  considered lines : {0}" -f $considered)
Write-Host ("  covered lines    : {0}" -f $coveredCnt)
Write-Host ("  ratio            : {0}%" -f $ratioPct)
if ($exempted.Count -gt 0) {
    Write-Host ("  exempted lines   : {0} (// no-cover)" -f $exempted.Count)
    foreach ($e in $exempted) { Write-Host "    - $e" }
}
Write-Host ("Required: patch ratio >= {0} ({1}%)" -f $MinPatchRatio, [math]::Round($MinPatchRatio * 100, 3))

if ($ratio -lt $MinPatchRatio) {
    Write-Host ""
    Write-Host ("FAIL: {0} changed line(s) not covered by tests:" -f $uncovered.Count) -ForegroundColor Red
    foreach ($u in $uncovered) { Write-Host "  $u" -ForegroundColor Red }
    Write-Host "Add tests for the lines above, or mark genuinely uncoverable lines with // no-cover (see scripts/coverage_patch.ps1)." -ForegroundColor Red
    exit 1
}

Write-Host "OK: all changed coverable lines are tested." -ForegroundColor Green
exit 0
