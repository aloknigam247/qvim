# Creates a GitHub issue from a triage draft markdown file in one shot.
#
# The draft's first "# " heading becomes the issue title; the remainder is the body.
# Ensures each requested label exists (creates missing ones), then creates the
# issue assigned to the current user, with a native parent link when requested.
#
# Usage (run from anywhere inside the repo):
#   & .\.github\skills\triage\scripts\new_issue.ps1 `
#     -Draft tmp\triage_issue_<guid>.md -Label @("bug", "tech-debt") -Parent 123

param(
    [Parameter(Mandatory)][string]$Draft,    # path to draft md (first '# ' line = title)
    [Parameter(Mandatory)][string[]]$Label,  # one or more category labels
    [ValidateRange(1, [int]::MaxValue)][int]$Parent
)

$ErrorActionPreference = "Stop"
$labelArgs = @()
$parentArgs = @()

# Keep the body file in the repository's ignored storage.
$root = git rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) { throw "Cannot determine the repository root." }
$root = $root.Trim()
Set-Location $root

if ($Parent) {
    $Label = @($Label | Where-Object { $_ -ine "sub-issue" }) + "sub-issue"
    $parentArgs = @("--parent", "$Parent")
}

$lines = @(Get-Content $Draft)
$heading = $lines | Select-String -Pattern "^# " | Select-Object -First 1
if (-not $heading) { throw "Draft must contain a '# ' title heading." }
$title = $heading.Line -replace "^# ", ""
if ([string]::IsNullOrWhiteSpace($title)) { throw "Draft title must not be empty." }

$existing = gh label list --json name --jq ".[].name"
if ($LASTEXITCODE -ne 0) { throw "Cannot list repository labels." }
foreach ($l in $Label) {
    if ($existing -cnotcontains $l) {
        gh label create "$l" --description "$l" --force | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot ensure label '$l'; no issue creation attempted." }
    }
    $labelArgs += @("--label", $l)
}

New-Item -ItemType Directory -Force -Path "tmp" | Out-Null
$body = "tmp\triage_body_$([guid]::NewGuid().ToString()).md"
try {
    Set-Content -Encoding utf8 -Path $body -Value @($lines | Select-Object -Skip $heading.LineNumber)
    $url = gh issue create --title "$title" --body-file "$body" @labelArgs @parentArgs --assignee "@me"
    if ($LASTEXITCODE -ne 0) {
        throw "Issue creation failed. Inspect GitHub before retrying to avoid duplicates. Output: $url"
    }
    if ([string]::IsNullOrWhiteSpace($url)) {
        throw "Issue creation returned no URL. Inspect GitHub before retrying to avoid duplicates."
    }
}
finally {
    Remove-Item $body -ErrorAction SilentlyContinue
}

Write-Output "TITLE: $title"
Write-Output "URL: $url"
