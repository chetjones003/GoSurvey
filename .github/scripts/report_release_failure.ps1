$ErrorActionPreference = 'Stop'

foreach ($required in 'GITHUB_REPOSITORY', 'GITHUB_REPOSITORY_OWNER', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'GITHUB_REF_NAME', 'GITHUB_SERVER_URL', 'GITHUB_SHA', 'GH_TOKEN') {
  if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($required))) {
    throw "Required GitHub Actions environment variable '$required' is missing."
  }
}

$repository = $env:GITHUB_REPOSITORY
$owner = $env:GITHUB_REPOSITORY_OWNER
$runId = $env:GITHUB_RUN_ID
$attempt = $env:GITHUB_RUN_ATTEMPT
$branch = $env:GITHUB_REF_NAME
$runUrl = "$env:GITHUB_SERVER_URL/$repository/actions/runs/$runId"
$title = "Release workflow failed on $branch (run $runId)"

$jobsJson = gh api "repos/$repository/actions/runs/$runId/jobs?per_page=100"
if ($LASTEXITCODE -ne 0) { throw 'Could not retrieve failed release jobs from GitHub.' }
$jobs = ($jobsJson | ConvertFrom-Json).jobs
$failedDetails = [Collections.Generic.List[string]]::new()
foreach ($job in $jobs | Where-Object { $_.conclusion -eq 'failure' }) {
  $failedSteps = @($job.steps | Where-Object { $_.conclusion -eq 'failure' })
  if ($failedSteps.Count -eq 0) {
    $failedDetails.Add("- **$($job.name):** job failed; [open job logs]($($job.html_url)).")
  } else {
    foreach ($step in $failedSteps) {
      $failedDetails.Add("- **$($job.name) / $($step.name):** failed; [open job logs]($($job.html_url)).")
    }
  }
}
if ($failedDetails.Count -eq 0) {
  $failedDetails.Add('- GitHub reported the run as failed but did not identify a failed job in the first 100 jobs.')
}

$body = @"
<!-- release-run-id:$runId -->

@$owner, the **$branch** release workflow failed on attempt **$attempt**.

- Commit: ``$env:GITHUB_SHA``
- Workflow run: [$runUrl]($runUrl)
- Run logs: [failed jobs]($runUrl)

### Failed jobs and steps

$($failedDetails -join "`n")

This issue was created automatically by the release workflow. Reruns of this workflow run reuse this issue.
"@

$issuesJson = gh issue list --repo $repository --state all --limit 100 --search "in:title $runId" --json number,title,state
if ($LASTEXITCODE -ne 0) { throw 'Could not check for an existing release failure issue.' }
$existing = @($issuesJson | ConvertFrom-Json | Where-Object { $_.title -eq $title } | Select-Object -First 1)

if ($existing.Count -gt 0) {
  $issueNumber = $existing[0].number
  if ($existing[0].state -eq 'CLOSED') {
    gh issue reopen $issueNumber --repo $repository
    if ($LASTEXITCODE -ne 0) { throw "Could not reopen existing release failure issue #$issueNumber." }
  }
  $comment = "@$owner, rerun attempt **$attempt** also failed. [Open the run and logs]($runUrl).`n`n$($failedDetails -join "`n")"
  gh issue comment $issueNumber --repo $repository --body $comment
  if ($LASTEXITCODE -ne 0) { throw "Could not update existing release failure issue #$issueNumber." }
  Write-Host "Updated release failure issue #$issueNumber for rerun attempt $attempt."
  exit 0
}

$bodyFile = Join-Path $env:RUNNER_TEMP "release-failure-$runId.md"
try {
  [IO.File]::WriteAllText($bodyFile, $body, [Text.UTF8Encoding]::new($false))
  gh issue create --repo $repository --title $title --label bug --label needs-triage --body-file $bodyFile
  if ($LASTEXITCODE -ne 0) { throw 'Could not create the release failure issue.' }
} finally {
  if (Test-Path -LiteralPath $bodyFile) { Remove-Item -LiteralPath $bodyFile -Force }
}
