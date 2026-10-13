param(
    [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
    [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/pbr-textures",
    [switch]$SkipRegression,[switch]$SkipBenchmarks,[switch]$SkipCapture,[switch]$SkipDemos
)
$ErrorActionPreference='Stop'
$build=(Resolve-Path $BuildDirectory).Path
$cache=Get-Content -LiteralPath "$build/CMakeCache.txt"
if($cache -notcontains 'ENABLE_DX12:BOOL=ON' -or $cache -notcontains 'ENABLE_VULKAN:BOOL=OFF'){throw 'DX12-only configuration required'}
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
$oldDebug=$env:WHISP_DX12_DEBUG
Push-Location $build
try {
    if(!$SkipRegression){
        $env:WHISP_DX12_DEBUG='1'
        foreach($test in @('PbrTexturesRegression','PbrCoreRegression','LightingRegression','GpuInstancingRegression','MaterialSystemRegression','ShadowMappingRegression')){
            & "./$test.exe" --dx12 *> "$artifacts/$test.log"
            if($LASTEXITCODE -ne 0){throw "$test failed"}
        }
        & './ShadowQualityRegression.exe' *> "$artifacts/ShadowQualityRegression.log"
        if($LASTEXITCODE -ne 0){throw 'Shadow quality failed'}
    }
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    if(!$SkipBenchmarks){
        foreach($count in @(100,250,500,1000)){foreach($instancing in @(1,0)){
            [string[]]$extra=if($instancing){@()}else{@('--no-instancing')}
            $key="perf-$count-i$instancing"
            & './WhispEngine.exe' --pbr-textures-benchmark $count "$artifacts/$key.csv" @extra *> "$artifacts/$key.log"
            if($LASTEXITCODE -ne 0){throw "$key failed"}
            $samples=Import-Csv "$artifacts/$key.csv"
            if($samples.Count -ne 2400){throw 'Expected 8 blocks of 300 samples'}
            foreach($s in $samples){
                $draws=if($instancing){1}else{$count+1}
                if([int]$s.instancing -ne $instancing -or [int]$s.instances -ne ($count+1) -or [int]$s.drawCalls -ne $draws -or [int]$s.shadowDraws -ne $draws -or $s.gpuValid -ne '1'){throw "$key batching/timestamps mismatch"}
            }
            Write-Host "$key : Core / Normal / MR+AO / All, 2400 samples passed"
        }}
    }
    $env:WHISP_DX12_DEBUG='1'
    if(!$SkipDemos){foreach($mode in @('mapped','flat','core','lambert','unlit')){
        [string[]]$extra=switch($mode){'flat'{@('--no-normal-mapping')};'core'{@('--no-pbr-textures')};'lambert'{@('--lambert')};'unlit'{@('--no-lighting')};default{@()}}
        & './WhispEngine.exe' --pbr-textures-demo --frames 160 --capture-frame "$artifacts/editor-$mode.ppm" @extra *> "$artifacts/editor-$mode.log"
        if($LASTEXITCODE -ne 0){throw "$mode demo failed"}
    }}
    Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
    if(!$SkipCapture){
        $exe=(Resolve-Path "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe").Path
        $capture=Start-Process -FilePath $exe -ArgumentList @('-o',('"'+"$artifacts/textures-500.tracy"+'"'),'-a','127.0.0.1','-s','60','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$artifacts/tracy-capture.log" -RedirectStandardError "$artifacts/tracy-capture-error.log"
        [void]$capture.Handle
        try{
            & './WhispEngine.exe' --pbr-textures-benchmark 500 "$artifacts/trace-500.csv" --wait-tracy *> "$artifacts/trace-500.log"
            if($LASTEXITCODE -ne 0){throw 'Tracy engine run failed'}
            if(!$capture.WaitForExit(60000)){throw 'Tracy capture timeout'}
            if($capture.ExitCode -ne 0){throw 'Tracy capture failed'}
        }finally{if(!$capture.HasExited){$capture.Kill()};$capture.Dispose()}
    }
} finally {
    Pop-Location
    if($null -eq $oldDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$oldDebug}
}
