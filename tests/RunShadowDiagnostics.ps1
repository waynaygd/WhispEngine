param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/shadows",
    [string]$CaptureExecutable="$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe",
    [int[]]$Counts=@(100,250,500,1000),
    [int]$Resolution=1024,
    [switch]$SkipCapture,
    [switch]$SkipRegression,
    [switch]$SkipActive
)
$ErrorActionPreference='Stop'
$previousDxDebug=$env:WHISP_DX12_DEBUG
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
if(!$SkipCapture){$captureExe=(Resolve-Path $CaptureExecutable).Path}
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    if(!$SkipRegression) {
        $env:WHISP_DX12_DEBUG='1'
        foreach($test in @('ShadowMappingRegression','LightingRegression','GpuInstancingRegression')) {
            & "./$test.exe" --dx12 *> (Join-Path $artifacts "$test-dx12.log")
            if($LASTEXITCODE -ne 0){throw "$test DX12 regression failed"}
        }
        Remove-Item Env:WHISP_DX12_DEBUG
    }
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    foreach($count in $Counts) {
        $name="shadow-$count-$Resolution"
        $arguments=@('--shadow-benchmark',"$count",(Join-Path $artifacts "$name.csv"),'--shadow-resolution',"$Resolution")
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
        if(Select-String -Path (Join-Path $artifacts "$name.log") -Pattern "DX12 validation failed" -Quiet){throw "$name DX12 validation failed"}
        $samples=Import-Csv (Join-Path $artifacts "$name.csv")
        if($samples.Count -ne 1800){throw "$name sample count mismatch"}
        foreach($sample in $samples) {
            $block=[math]::Floor(([int]$sample.frame-100)/300)
            if([int]$sample.instancing -ne ($block%2) -or [int]$sample.shadows -ne [int]($block -ge 2) -or [int]$sample.pcf -ne [int]($block -ge 4)){throw "$name comparison switches changed"}
            $draws=if($sample.instancing -eq '1'){1}else{$count+1}
            $shadowDraws=if($sample.shadows -eq '1'){$draws}else{0}
            if([int]$sample.drawCalls -ne $draws -or [int]$sample.shadowDraws -ne $shadowDraws -or [int]$sample.instances -ne $count+1){throw "$name batching mismatch"}
            if($sample.gpuValid -ne '1'){throw "$name GPU timestamps invalid"}
        }
        Write-Host "$name passed: six modes, 1800 samples, GPU timings and draw counts verified"
    }
    if(!$SkipActive) {
        $env:WHISP_DX12_DEBUG='1'
        foreach($count in @(100,250,500,1000)) {
            & './WhispEngine.exe' --stress $count --frames 300 --diagnostics (Join-Path $artifacts "active-$count.csv") --capture-frame (Join-Path $artifacts "active-$count.ppm") *> (Join-Path $artifacts "active-$count.log")
            if($LASTEXITCODE -ne 0){throw "Active $count failed"}
            if(Select-String -Path (Join-Path $artifacts "active-$count.log") -Pattern "DX12 validation failed" -Quiet){throw "Active $count DX12 validation failed"}
        }
        & './WhispEngine.exe' --stability-scenario (Join-Path $artifacts 'lifecycle.csv') *> (Join-Path $artifacts 'lifecycle.log')
        if($LASTEXITCODE -ne 0){throw 'Lifecycle failed'}
        if(Select-String -Path (Join-Path $artifacts 'lifecycle.log') -Pattern 'DX12 validation failed' -Quiet){throw 'Lifecycle DX12 validation failed'}
        & './WhispEngine.exe' --stress 500 --frames 300 --no-interpolation *> (Join-Path $artifacts 'interpolation-off.log')
        if($LASTEXITCODE -ne 0){throw 'Interpolation OFF failed'}
        Remove-Item Env:WHISP_DX12_DEBUG
    }
} finally {
    if($null -eq $previousDxDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$previousDxDebug}
    Pop-Location
}
