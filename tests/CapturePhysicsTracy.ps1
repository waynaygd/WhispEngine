param(
    [string]$BuildDirectory = "$PSScriptRoot/../out/build/x64-release",
    [string]$CaptureExecutable = "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe",
    [string]$ArtifactDirectory = "$PSScriptRoot/../out/diagnostics",
    [switch]$SettledRun,
    [string]$EngineExecutable = 'WhispEngine.exe',
    [int]$Frames = 5000
)
$ErrorActionPreference = 'Stop'
$captureExe = (Resolve-Path $CaptureExecutable).Path
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts = (Resolve-Path $ArtifactDirectory).Path
$name = if ($SettledRun) { 'engine-settled' } else { 'engine-lifecycle' }
$captureArguments = @('-o', ('"' + (Join-Path $artifacts "$name.tracy") + '"'), '-a','127.0.0.1','-s','120','-f')
$capture = Start-Process -FilePath $captureExe -ArgumentList $captureArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artifacts "$name-capture.log") -RedirectStandardError (Join-Path $artifacts "$name-capture-error.log")
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    $arguments = @('--stability-scenario', (Join-Path $artifacts "$name.csv"), '--wait-tracy')
    if ($SettledRun) { $arguments = @('--stress','500','--frames',"$Frames",'--diagnostics',(Join-Path $artifacts "$name.csv"),'--wait-tracy') }
    & (Join-Path (Get-Location).Path $EngineExecutable) @arguments *> (Join-Path $artifacts "$name.log")
    $engineCode = $LASTEXITCODE
} finally { Pop-Location }
$capture.WaitForExit()
Write-Host "Engine exit=$engineCode; capture exit=$($capture.ExitCode)"
if ($engineCode -ne 0 -or $capture.ExitCode -ne 0) { throw 'Capture run failed; inspect logs.' }
