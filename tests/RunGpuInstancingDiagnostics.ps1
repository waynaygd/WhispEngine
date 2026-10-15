param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/x64-release",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/instancing",
    [string]$CaptureExecutable="$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe"
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
$captureExe=(Resolve-Path $CaptureExecutable).Path
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    & './GpuInstancingRegression.exe' --dx12 *> (Join-Path $artifacts 'dx12-pixels-final.log')
    if($LASTEXITCODE -ne 0){throw 'DX12 pixel regression failed'}
    foreach($count in @(100,250,500,1000)) {
        $name="paused-$count"
        $captureArgs=@('-o',('"'+(Join-Path $artifacts "$name.tracy")+'"'),'-a','127.0.0.1','-s','60','-f')
        $capture=Start-Process -FilePath $captureExe -ArgumentList $captureArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artifacts "$name-capture.log") -RedirectStandardError (Join-Path $artifacts "$name-capture-error.log")
        & './WhispEngine.exe' --render-benchmark $count (Join-Path $artifacts "$name.csv") --wait-tracy *> (Join-Path $artifacts "$name.log")
        $code=$LASTEXITCODE;$capture.WaitForExit()
        Write-Host "$name engine=$code capture=$($capture.ExitCode)"
        if($code -ne 0 -or $capture.ExitCode -ne 0){throw "$name failed"}
    }
    foreach($count in @(100,250,500,1000)) {
        $arguments=@('--stress',"$count",'--frames','200','--diagnostics',(Join-Path $artifacts "active-$count.csv"))
        if($count -le 250){$arguments+='--serial'}
        & './WhispEngine.exe' @arguments *> (Join-Path $artifacts "active-$count.log")
        if($LASTEXITCODE -ne 0){throw "Active $count smoke failed"}
    }
    & './WhispEngine.exe' --stress 500 --frames 200 --no-interpolation *> (Join-Path $artifacts 'active-interpolation-off.log')
    if($LASTEXITCODE -ne 0){throw 'Interpolation OFF smoke failed'}
    & './WhispEngine.exe' --stability-scenario (Join-Path $artifacts 'lifecycle.csv') *> (Join-Path $artifacts 'lifecycle.log')
    if($LASTEXITCODE -ne 0){throw 'Lifecycle failed'}
} finally {Pop-Location}
