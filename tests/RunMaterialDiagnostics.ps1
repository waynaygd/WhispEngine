param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/materials",
    [string]$CaptureExecutable="$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe",
    [switch]$SkipRegression,
    [switch]$SkipCapture,
    [switch]$SkipStress
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
$build=(Resolve-Path $BuildDirectory).Path
if(!$SkipCapture){$captureExe=(Resolve-Path $CaptureExecutable).Path}
$previousDebug=$env:WHISP_DX12_DEBUG
try {
    if(!$SkipStress){
        # Comparison runs use the same disconnected Tracy state as the baseline.
        & "$PSScriptRoot/RunShadowDiagnostics.ps1" -BuildDirectory $build -ArtifactDirectory "$artifacts/after" -SkipRegression -SkipActive -SkipCapture
    }
    Push-Location $build
    try {
        if(!$SkipRegression){
            $env:WHISP_DX12_DEBUG='1'
            foreach($test in @('MaterialSystemRegression','LightingRegression','GpuInstancingRegression','ShadowMappingRegression')) {
                & "./$test.exe" --dx12 *> (Join-Path $artifacts "$test-dx12.log")
                if($LASTEXITCODE -ne 0){throw "$test failed"}
            }
            & './WhispEngine.exe' --stability-scenario (Join-Path $artifacts 'lifecycle.csv') *> (Join-Path $artifacts 'lifecycle.log')
            if($LASTEXITCODE -ne 0){throw 'Lifecycle failed'}
            & './WhispEngine.exe' --stress 500 --frames 200 --no-interpolation *> (Join-Path $artifacts 'interpolation-off.log')
            if($LASTEXITCODE -ne 0){throw 'Interpolation OFF failed'}
        }
        Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
        & './WhispEngine.exe' --material-benchmark 100 (Join-Path $artifacts 'multi-material.csv') *> (Join-Path $artifacts 'multi-material.log')
        if($LASTEXITCODE -ne 0){throw 'Multi-material benchmark failed'}
        $samples=Import-Csv (Join-Path $artifacts 'multi-material.csv')
        if($samples.Count -ne 1800){throw 'Multi-material sample count mismatch'}
        foreach($sample in $samples) {
            $draws=if($sample.instancing -eq '1'){4}else{400}
            $shadowDraws=if($sample.shadows -eq '1'){$draws}else{0}
            if([int]$sample.drawCalls -ne $draws -or [int]$sample.shadowDraws -ne $shadowDraws -or [int]$sample.instances -ne 400 -or [int]$sample.objects -ne 200){throw 'Multi-material ranges/batching mismatch'}
            if($sample.gpuValid -ne '1'){throw 'Invalid GPU timestamps'}
        }
        $env:WHISP_DX12_DEBUG='1'
        & './WhispEngine.exe' --material-demo --frames 150 --capture-frame (Join-Path $artifacts 'material-editor.ppm') *> (Join-Path $artifacts 'material-editor.log')
        if($LASTEXITCODE -ne 0){throw 'Material editor demo failed'}
        Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
        if(!$SkipCapture) {
            $captureArgs=@('-o',('"'+(Join-Path $artifacts 'material-500.tracy')+'"'),'-a','127.0.0.1','-s','60','-f')
            $capture=Start-Process -FilePath $captureExe -ArgumentList $captureArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artifacts 'tracy-capture.log') -RedirectStandardError (Join-Path $artifacts 'tracy-capture-error.log')
            # Acquire the handle while running: Windows PowerShell otherwise may
            # leave ExitCode unset when a redirected child exits before it is read.
            [void]$capture.Handle
            & './WhispEngine.exe' --shadow-benchmark 500 (Join-Path $artifacts 'tracy-500.csv') --wait-tracy *> (Join-Path $artifacts 'tracy-500.log')
            $code=$LASTEXITCODE
            if($code -ne 0){if(!$capture.HasExited){$capture.Kill()};throw "Tracy engine failed ($code)"}
            if(!$capture.WaitForExit(60000)){if(!$capture.HasExited){$capture.Kill()};throw 'Tracy capture timeout'}
            $capture.WaitForExit()
            if($capture.ExitCode -ne 0){throw 'Tracy capture failed'}
        }
        foreach($log in @('material-editor.log','lifecycle.log','interpolation-off.log','MaterialSystemRegression-dx12.log','LightingRegression-dx12.log','GpuInstancingRegression-dx12.log','ShadowMappingRegression-dx12.log')) {
            $path=Join-Path $artifacts $log
            if((Test-Path $path) -and (Select-String -Path $path -Pattern 'DX12 validation failed' -Quiet)){throw "$log contains DX12 validation errors"}
        }
        Write-Host 'Material diagnostics passed: multi-material batching, editor demo and requested regression/capture runs.'
    } finally {Pop-Location}
} finally {
    if($null -eq $previousDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$previousDebug}
}
