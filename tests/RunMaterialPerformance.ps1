param([string]$Root="$PSScriptRoot/../out/lab3-evidence",[switch]$Capture)
$ErrorActionPreference='Stop'
$rootPath=(Resolve-Path $Root).Path
if(!$Capture -and (Test-Path "$rootPath/raw/*.csv")){throw 'Raw CSV already exists. Use a newly prepared isolated -Root to preserve previous evidence.'}
$build=Join-Path $rootPath 'local-build'
$runtime=Join-Path $rootPath 'runtime'
New-Item -ItemType Directory -Force $runtime | Out-Null
foreach($name in @('assets','shaders')){Copy-Item (Join-Path $build $name) $runtime -Recurse -Force}
Copy-Item (Join-Path $build 'MaterialPerformanceProbe.exe') $runtime -Force
foreach($dll in Get-ChildItem $build -Filter *.dll){Copy-Item $dll.FullName $runtime -Force}
for($i=0;$i -lt 500;$i++){@{version=4;name="Perf $i";shaderPath='dx12/textured.hlsl';texturePath='defaults/texture';baseColor=@(0.7,0.7,0.7,1);roughness=0.5;castShadows=$false} | ConvertTo-Json -Compress | Set-Content (Join-Path $runtime "assets/materials/perf-$i.json")}
$previousDebug=$env:WHISP_DX12_DEBUG
try {
 Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
 Push-Location $runtime
 try {
  if($Capture){
   New-Item -ItemType Directory -Force "$rootPath/captures" | Out-Null
   foreach($count in @(1,500)){
    & 'C:/Program Files/RenderDoc/renderdoccmd.exe' capture -w -d $runtime -c "$rootPath/captures/current-$count" "$runtime/MaterialPerformanceProbe.exe" $count 0 "../captures/excluded-timing-$count.csv" capture *> "$rootPath/captures/launch-$count.log"
    if($LASTEXITCODE -ne 0){throw "RenderDoc launch failed: $count"}
   }
  }else{
   New-Item -ItemType Directory -Force "$rootPath/raw" | Out-Null
   # Separate process per run; interleave material counts to limit order drift.
   foreach($run in 1..3){foreach($instances in @(0,1)){foreach($count in @(1,10,100,500)){
    $name="m$count-i$instances-run$run"
    & './MaterialPerformanceProbe.exe' $count $instances "../raw/$name.csv" *> "$rootPath/raw/$name.log"
    $code=$LASTEXITCODE
    Add-Content "$rootPath/attempts.csv" "$name,$code,$([DateTime]::UtcNow.ToString('o'))"
    if($code -ne 0){throw "Benchmark failed: $name ($code), retained artifacts"}
   }}}
  }
 }finally{Pop-Location}
}finally{if($null -eq $previousDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$previousDebug}}


