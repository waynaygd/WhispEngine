param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/x64-release",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/lighting",
    [string]$CaptureExecutable="$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe",
    [switch]$SkipCapture
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
if(!$SkipCapture){$captureExe=(Resolve-Path $CaptureExecutable).Path}
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    foreach($test in @('LightingRegression','GpuInstancingRegression')) {
        & "./$test.exe" --dx12 *> (Join-Path $artifacts "$test-dx12.log")
        if($LASTEXITCODE -ne 0){throw "$test DX12 regression failed"}
    }
    foreach($count in @(100,250,500,1000)) {
        $name="lighting-$count"
        $arguments=@('--lighting-benchmark',"$count",(Join-Path $artifacts "$name.csv"))
        if(!$SkipCapture) {
            $captureArgs=@('-o',('"'+(Join-Path $artifacts "$name.tracy")+'"'),'-a','127.0.0.1','-s','60','-f')
            $capture=Start-Process -FilePath $captureExe -ArgumentList $captureArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artifacts "$name-capture.log") -RedirectStandardError (Join-Path $artifacts "$name-capture-error.log")
            $arguments+='--wait-tracy'
        }
        & './WhispEngine.exe' @arguments *> (Join-Path $artifacts "$name.log")
        $code=$LASTEXITCODE
        if(!$SkipCapture){
            if($code -ne 0){$capture.Kill();throw "$name engine failed ($code)"}
            if(!$capture.WaitForExit(60000)){$capture.Kill();throw "$name capture timeout"}
            if($capture.ExitCode -ne 0){throw "$name capture failed"}
        }
        if($code -ne 0){throw "$name engine failed ($code)"}
        $samples=Import-Csv (Join-Path $artifacts "$name.csv")
        if($samples.Count -ne 2400){throw "$name sample count mismatch"}
        foreach($sample in $samples) {
            $block=[math]::Floor(([int]$sample.frame-100)/300)
            if([int]$sample.instancing -ne ($block%2) -or [int]$sample.lighting -ne [int](($block%4) -ge 2)){throw "$name comparison switches changed"}
            $draws=if($sample.instancing -eq '1'){1}else{$count+1}
            if([int]$sample.drawCalls -ne $draws -or [int]$sample.instances -ne $count+1){throw "$name batching mismatch"}
        }
        Write-Host "$name passed: 2400 samples, four lighting/instancing modes"
    }
    foreach($count in @(100,250,500,1000)) {
        & './WhispEngine.exe' --stress $count --frames 200 --diagnostics (Join-Path $artifacts "active-$count.csv") *> (Join-Path $artifacts "active-$count.log")
        if($LASTEXITCODE -ne 0){throw "Active $count failed"}
    }
    & './WhispEngine.exe' --stress 500 --frames 200 --no-interpolation *> (Join-Path $artifacts 'interpolation-off.log')
    if($LASTEXITCODE -ne 0){throw 'Interpolation OFF failed'}
    & './WhispEngine.exe' --stress 500 --frames 200 --no-lighting --no-instancing *> (Join-Path $artifacts 'unlit-ordinary.log')
    if($LASTEXITCODE -ne 0){throw 'Unlit ordinary failed'}
    & './WhispEngine.exe' --stability-scenario (Join-Path $artifacts 'lifecycle.csv') *> (Join-Path $artifacts 'lifecycle.log')
    if($LASTEXITCODE -ne 0){throw 'Lifecycle failed'}
} finally {Pop-Location}
