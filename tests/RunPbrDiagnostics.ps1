param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/pbr",
    [switch]$SkipRegression,[switch]$SkipBenchmarks,[switch]$SkipCapture
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
$previousDebug=$env:WHISP_DX12_DEBUG
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    if(!$SkipRegression) {
        $env:WHISP_DX12_DEBUG='1'
        foreach($test in @('PbrCoreRegression','LightingRegression','GpuInstancingRegression','MaterialSystemRegression','ShadowMappingRegression')) {
            & "./$test.exe" --dx12 *> "$artifacts/$test.log"
            if($LASTEXITCODE -ne 0){throw "$test failed"}
        }
        & './WhispEngine.exe' --pbr --stability-scenario "$artifacts/lifecycle.csv" *> "$artifacts/lifecycle.log"
        if($LASTEXITCODE -ne 0){throw 'PBR lifecycle failed'}
        & './WhispEngine.exe' --stress 500 --pbr --no-interpolation --frames 200 *> "$artifacts/interpolation-off.log"
        if($LASTEXITCODE -ne 0){throw 'PBR interpolation OFF failed'}
    }
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    if(!$SkipBenchmarks) {
        foreach($count in @(100,250,500,1000)) {
            & './WhispEngine.exe' --pbr-benchmark $count "$artifacts/perf-$count.csv" *> "$artifacts/perf-$count.log"
            if($LASTEXITCODE -ne 0){throw "Benchmark $count failed"}
            $samples=Import-Csv "$artifacts/perf-$count.csv"
            if($samples.Count -ne 1200){throw 'Sample count mismatch'}
            foreach($s in $samples) {
                if($s.instancing -ne '1' -or $s.drawCalls -ne '1' -or $s.shadowDraws -ne '1' -or $s.shadows -ne '1' -or $s.gpuValid -ne '1' -or [int]$s.instances -ne ($count+1)){throw 'PBR batching/timestamps mismatch'}
            }
            Write-Host "${count}: 1200 samples; Lambert/PBR, main1 + shadow1 passed"
        }
    }
    $env:WHISP_DX12_DEBUG='1'
    foreach($mode in @('pbr','lambert','unlit')) {
        $extra=switch($mode){'lambert'{@('--lambert')};'unlit'{@('--no-lighting')};default{@()}}
        & './WhispEngine.exe' --pbr-demo --frames 160 --capture-frame "$artifacts/editor-$mode.ppm" @extra *> "$artifacts/editor-$mode.log"
        if($LASTEXITCODE -ne 0){throw "$mode demo failed"}
    }
    & './WhispEngine.exe' --material-demo --pbr --frames 160 --capture-frame "$artifacts/editor-imported.ppm" *> "$artifacts/editor-imported.log"
    if($LASTEXITCODE -ne 0){throw 'PBR imported slots demo failed'}
    & './WhispEngine.exe' --stress 500 --pbr --frames 200 --capture-frame "$artifacts/editor-stress.ppm" *> "$artifacts/editor-stress.log"
    if($LASTEXITCODE -ne 0){throw 'PBR stress scene failed'}
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    if(!$SkipCapture) {
        $captureExe=(Resolve-Path "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe").Path
        $capture=Start-Process -FilePath $captureExe -ArgumentList @('-o',('"'+"$artifacts/pbr-500.tracy"+'"'),'-a','127.0.0.1','-s','60','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$artifacts/tracy-capture.log" -RedirectStandardError "$artifacts/tracy-capture-error.log"
        [void]$capture.Handle
        & './WhispEngine.exe' --pbr-benchmark 500 "$artifacts/tracy-500.csv" --wait-tracy *> "$artifacts/tracy-500.log"
        if($LASTEXITCODE -ne 0){if(!$capture.HasExited){$capture.Kill()};throw 'Tracy engine failed'}
        if(!$capture.WaitForExit(60000)){if(!$capture.HasExited){$capture.Kill()};throw 'Tracy timeout'}
        $capture.WaitForExit();if($capture.ExitCode -ne 0){throw 'Tracy capture failed'}
    }
    foreach($log in Get-ChildItem $artifacts -Filter '*.log') {
        if($log.Name -like 'ctest*' -or $log.Name -like 'build*'){continue}
        if(Select-String -LiteralPath $log.FullName -Pattern 'DX12 validation failed' -Quiet){throw "$($log.Name) contains DX12 ERROR/CORRUPTION"}
    }
} finally {
    Pop-Location
    if($null -eq $previousDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$previousDebug}
}
