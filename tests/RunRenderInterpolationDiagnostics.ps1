param(
    [string]$BuildDirectory = "$PSScriptRoot/../out/build/x64-release",
    [string]$ArtifactDirectory = "$PSScriptRoot/../out/diagnostics/interpolation",
    [string]$CaptureExecutable = "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts = (Resolve-Path $ArtifactDirectory).Path
$captureExe = (Resolve-Path $CaptureExecutable).Path
function Invoke-CapturedRun($exe, $arguments, $name) {
    $captureArgs = @('-o',('"'+(Join-Path $artifacts "$name.tracy")+'"'),'-a','127.0.0.1','-s','60','-f')
    $capture = Start-Process -FilePath $captureExe -ArgumentList $captureArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artifacts "$name-capture.log") -RedirectStandardError (Join-Path $artifacts "$name-capture-error.log")
    & $exe @arguments *> (Join-Path $artifacts "$name.log")
    $code = $LASTEXITCODE
    $capture.WaitForExit()
    Write-Host "$name engine=$code capture=$($capture.ExitCode)"
    if($code -ne 0 -or $capture.ExitCode -ne 0) {throw "Diagnostic failed: $name"}
}
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    Invoke-CapturedRun './RenderInterpolationRegression.exe' @('--wait-tracy') 'controlled-render'
    foreach($count in @(100,500,1000)) {
        foreach($mode in @('on','off')) {
            $name = "visual-$count-$mode"
            $arguments = @('--stress',"$count",'--frames','300','--diagnostics',(Join-Path $artifacts "$name.csv"),'--wait-tracy')
            if($mode -eq 'off') {$arguments += '--no-interpolation'}
            Invoke-CapturedRun './WhispEngine.exe' $arguments $name
        }
    }
    & './WhispEngine.exe' --stress 250 --frames 300 --serial --diagnostics (Join-Path $artifacts 'visual-250-serial.csv') *> (Join-Path $artifacts 'visual-250-serial.log')
    if($LASTEXITCODE -ne 0) {throw '250 Serial smoke failed'}
    & './WhispEngine.exe' --stability-scenario (Join-Path $artifacts 'lifecycle.csv') *> (Join-Path $artifacts 'lifecycle.log')
    if($LASTEXITCODE -ne 0) {throw 'Lifecycle failed'}
    & './WhispEngine.exe' --fixed-state --stress 500 --frames 300 --slow-frame 50 1000 --diagnostics (Join-Path $artifacts 'stall.csv') *> (Join-Path $artifacts 'stall.log')
    if($LASTEXITCODE -ne 0) {throw 'Fixed state/stall failed'}
} finally { Pop-Location }
