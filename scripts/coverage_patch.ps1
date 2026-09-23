<#
.SYNOPSIS
    Enforce 100% coverage of the lines a change adds or modifies (patch / diff
    coverage) over qvim's src/ + include/. Fails (exit 1) if any changed,
    coverable, non-exempt line is left uncovered.

.DESCRIPTION
    The overall floor (coverage_union.ps1) guards the whole codebase; this guard
    is per-change: every executable src/ + include/ line a PR touches must be
    exercised by the test suite. It complements the floor — a PR can sit above
    90% overall while still shipping untested new code, which this catches.

    Method:
      1. Diff the merge-base of -BaseRef and HEAD against HEAD, restricted to
         src/ and include/, and collect the NEW-side line numbers of added /
         modified lines.
      2. Union the tier coberturas into a per-(file,line) hit map (a line is
         covered if ANY tier hit it) exactly as coverage_union.ps1 does. Coverage
         is collected against HEAD, so cobertura line numbers and the diff's
         new-side line numbers refer to the same source.
      3. A changed line is CONSIDERED only if it appears as an executable line in
         the cobertura (comments, blanks, braces, and declarations never appear,
         so they are not counted — standard diff-coverage semantics).
      4. Considered lines are dropped when exempted by a source marker (see
         below). Every remaining considered line must have hits > 0.

    Exemption markers (read from the HEAD source, since the coverage tool has no
    per-line C++ exclusion): a line carrying a trailing "// no-cover" is exempt;
    a "// no-cover:start" ... "// no-cover:end" pair exempts the enclosed block.
    Use them ONLY for genuinely uncoverable lines — SceneGraph / GUI glue that
    needs a live render surface, platform (Win32) calls, or coverage-tool
    artifacts (case labels, trailing returns after an exhaustive switch). Each
    marker is visible in the diff, so an exemption is a reviewable decision.
    The gate prints every exempted line so reviewers can audit them.

.PARAMETER CoberturaPath
    One or more cobertura XML reports (one per tier). Unioned like the floor.

.PARAMETER BaseRef
    The ref the change is measured against (e.g. origin/main or the PR base sha).
    The diff runs from merge-base(BaseRef, HEAD) to HEAD.

.PARAMETER MinPatchRatio
    Required covered/considered ratio for changed lines. Defaults to 1.0 (100%).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$CoberturaPath,
    [Parameter(Mandatory)][string]$BaseRef,
    [double]$MinPatchRatio = 1.0
)

$ErrorActionPreference = "Stop"

# Normalise a path to its src/... or include/... tail, forward-slashed and
# lower-cased, so cobertura's absolute paths and git's repo-relative paths key
# the same line.
function Get-CoverageKeyPath([string]$path) {
    $p = $path.Replace("\", "/").ToLowerInvariant()
    if ($p -match "(^|/)(src|include)/(.+)$") { return "$($Matches[2])/$($Matches[3])" }
    return $null
}

$mergeBase = (git merge-base $BaseRef HEAD).Trim()
if (-not $mergeBase) { Write-Error "could not find merge-base of $BaseRef and HEAD."; exit 1 }
Write-Host "patch coverage: diffing $mergeBase..HEAD over src/ + include/"

# --- 1. changed (file -> set of new-side line numbers) ----------------------
$changed = @{}   # keyPath -> hashset of int line numbers
$curFile = $null
$newLine = 0
$diff = git diff --unified=0 --diff-filter=AMR $mergeBase HEAD -- src include
foreach ($line in $diff) {
    if ($line.StartsWith("+++ ")) {
        # "+++ b/src/Foo.cpp" or "+++ /dev/null"
        $raw = $line.Substring(4).Trim()
        if ($raw -eq "/dev/null") { $curFile = $null; continue }
        $curFile = Get-CoverageKeyPath ($raw -replace "^b/", "")
        if ($curFile -and -not $changed.ContainsKey($curFile)) {
            $changed[$curFile] = [System.Collections.Generic.HashSet[int]]::new()
        }
        continue
    }
    if ($line.StartsWith("@@")) {
        # "@@ -a,b +c,d @@" — c is the new-side start line.
        if ($line -match "\+(\d+)(?:,(\d+))?") { $newLine = [int]$Matches[1] }
        continue
    }
    if ($null -eq $curFile) { continue }
    if ($line.StartsWith("+") -and -not $line.StartsWith("+++")) {
        [void]$changed[$curFile].Add($newLine)
        $newLine++
    }
    # With --unified=0 there are no context lines; "-" removed lines and diff
    # metadata do not exist on the new side, so the cursor only advances on "+".
}

# --- 2. union hit map -------------------------------------------------------
$hits = @{}   # "keyPath|line" -> summed hits
foreach ($path in $CoberturaPath) {
    if (-not (Test-Path -LiteralPath $path)) { Write-Error "cobertura not found: $path"; exit 1 }
    [xml]$report = Get-Content -LiteralPath $path -Raw
    foreach ($pkg in $report.coverage.packages.package) {
        foreach ($cls in $pkg.classes.class) {
            $key = Get-CoverageKeyPath ([string]$cls.filename)
            if (-not $key) { continue }
            foreach ($ln in $cls.lines.line) {
                $k = "$key|$($ln.number)"
                if (-not $hits.ContainsKey($k)) { $hits[$k] = 0 }
                $hits[$k] += [int]$ln.hits
            }
        }
    }
}

# --- 3+4. evaluate considered lines, honouring exemption markers ------------
$considered = 0
$coveredCnt = 0
$uncovered = [System.Collections.Generic.List[string]]::new()
$exempted = [System.Collections.Generic.List[string]]::new()

foreach ($file in ($changed.Keys | Sort-Object)) {
    $lineSet = $changed[$file]
    if ($lineSet.Count -eq 0) { continue }

    # Resolve the marker state per physical line from the HEAD source.
    $srcPath = $file  # repo-relative, forward-slashed, lower-cased tail
    # Recover a real on-disk path (case-insensitive match under src/ or include/).
    $disk = Get-ChildItem -Recurse -File -Path (Split-Path $file -Parent) -ErrorAction SilentlyContinue |
        Where-Object { (Get-CoverageKeyPath $_.FullName) -eq $file } | Select-Object -First 1
    $exemptLines = [System.Collections.Generic.HashSet[int]]::new()
    if ($disk) {
        $srcLines = Get-Content -LiteralPath $disk.FullName
        $inBlock = $false
        for ($i = 0; $i -lt $srcLines.Count; $i++) {
            $text = $srcLines[$i]
            if ($text -match "//\s*no-cover:start") { $inBlock = $true }
            if ($inBlock -or ($text -match "//\s*no-cover(?!:)")) { [void]$exemptLines.Add($i + 1) }
            if ($text -match "//\s*no-cover:end") { $inBlock = $false }
        }
    }

    foreach ($ln in ($lineSet | Sort-Object)) {
        $k = "$file|$ln"
        if (-not $hits.ContainsKey($k)) { continue } # non-executable — not considered
        if ($exemptLines.Contains($ln)) { $exempted.Add("${file}:$ln"); continue }
        $considered++
        if ($hits[$k] -gt 0) { $coveredCnt++ } else { $uncovered.Add("${file}:$ln") }
    }
}

$ratio = if ($considered -gt 0) { $coveredCnt / $considered } else { 1.0 }
$ratioPct = [math]::Round($ratio * 100, 3)

Write-Host ""
Write-Host "Patch coverage over changed src/ + include/ lines:"
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
