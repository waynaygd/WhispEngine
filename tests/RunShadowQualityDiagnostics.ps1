param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/shadow-quality",
    [switch]$SkipRegression,
    [switch]$SkipCapture,
    [switch]$SkipBenchmarks
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
$previousDebug=$env:WHISP_DX12_DEBUG
Push-Location (Resolve-Path $BuildDirectory).Path
try {
    if(!$SkipRegression) {
        $env:WHISP_DX12_DEBUG='1'
        foreach($test in @('ShadowQualityRegression','ShadowMappingRegression','MaterialSystemRegression','LightingRegression','GpuInstancingRegression')) {
            $arguments=if($test -eq 'ShadowQualityRegression'){@('--check',"$artifacts/after-images")}else{@('--dx12')}
            & "./$test.exe" @arguments *> "$artifacts/$test.log"
            if($LASTEXITCODE -ne 0){throw "$test failed"}
        }
        & './WhispEngine.exe' --stability-scenario "$artifacts/lifecycle.csv" *> "$artifacts/lifecycle.log"
        if($LASTEXITCODE -ne 0){throw 'Lifecycle failed'}
        & './WhispEngine.exe' --stress 500 --frames 200 --no-interpolation *> "$artifacts/interpolation-off.log"
        if($LASTEXITCODE -ne 0){throw 'Interpolation OFF failed'}
    }
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    if(!$SkipBenchmarks) {
        foreach($profile in @('after-fixed','after-default')) {
            New-Item -ItemType Directory -Force -Path "$artifacts/$profile" | Out-Null
            foreach($count in @(100,250,500,1000)) {
                $extra=if($profile -eq 'after-fixed'){@('--shadow-fixed','--shadow-resolution','1024','--shadow-receiver-bias','.0003','--shadow-normal-bias','0')}else{@()}
                & './WhispEngine.exe' --shadow-benchmark $count "$artifacts/$profile/shadow-$count.csv" @extra *> "$artifacts/$profile/shadow-$count.log"
                if($LASTEXITCODE -ne 0){throw "$profile $count failed"}
                $samples=Import-Csv "$artifacts/$profile/shadow-$count.csv"
                if($samples.Count -ne 1800){throw 'Sample count mismatch'}
                foreach($s in $samples) {
                    $draws=if($s.instancing -eq '1'){1}else{$count+1}
                    $shadowDraws=if($s.shadows -eq '1'){$draws}else{0}
                    if([int]$s.drawCalls -ne $draws -or [int]$s.shadowDraws -ne $shadowDraws -or $s.gpuValid -ne '1'){throw 'Batching/timestamps mismatch'}
                }
                Write-Host "$profile ${count}: 1800 samples and batching passed"
            }
        }
    }
    $env:WHISP_DX12_DEBUG='1'
    foreach($mode in @('off','hard','soft','visibility','coverage')) {
        $extra=switch($mode){'off'{@('--no-shadows')};'hard'{@('--hard-shadows')};'visibility'{@('--shadow-debug','1')};'coverage'{@('--shadow-debug','2')};default{@()}}
        & './WhispEngine.exe' --shadow-demo --frames 160 --capture-frame "$artifacts/editor-$mode.ppm" @extra *> "$artifacts/editor-$mode.log"
        if($LASTEXITCODE -ne 0){throw "$mode editor demo failed"}
    }
    & './WhispEngine.exe' --material-demo --frames 120 --capture-frame "$artifacts/material-slots.ppm" *> "$artifacts/material-slots.log"
    if($LASTEXITCODE -ne 0){throw 'Material slots demo failed'}
    & './WhispEngine.exe' --stress 500 --frames 200 --capture-frame "$artifacts/stress-500.ppm" *> "$artifacts/stress-500.log"
    if($LASTEXITCODE -ne 0){throw 'Active stress failed'}
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    if(!$SkipCapture) {
        $captureExe=(Resolve-Path "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe").Path
        $capture=Start-Process -FilePath $captureExe -ArgumentList @('-o',('"'+"$artifacts/quality-500.tracy"+'"'),'-a','127.0.0.1','-s','60','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$artifacts/tracy-capture.log" -RedirectStandardError "$artifacts/tracy-capture-error.log"
        [void]$capture.Handle
        & './WhispEngine.exe' --shadow-benchmark 500 "$artifacts/tracy-500.csv" --wait-tracy *> "$artifacts/tracy-500.log"
        if($LASTEXITCODE -ne 0){if(!$capture.HasExited){$capture.Kill()};throw 'Tracy engine failed'}
        if(!$capture.WaitForExit(60000)){if(!$capture.HasExited){$capture.Kill()};throw 'Tracy capture timeout'}
        $capture.WaitForExit();if($capture.ExitCode -ne 0){throw 'Tracy capture failed'}
    }
    foreach($log in @(Get-ChildItem $artifacts -Filter '*.log' -Recurse)) {
        if($log.Name -like 'build*' -or $log.Name -like 'ctest*'){continue}
        if(Select-String -LiteralPath $log.FullName -Pattern 'DX12 validation failed' -Quiet){throw "$($log.Name) contains DX12 ERROR/CORRUPTION"}
    }
} finally {
    Pop-Location
    if($null -eq $previousDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$previousDebug}
}
