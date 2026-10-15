param(
 [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
 [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/light-system",
 [switch]$SkipRegression,[switch]$SkipBenchmarks,[switch]$SkipDemos,[switch]$SkipCapture
)
$ErrorActionPreference='Stop'
$build=(Resolve-Path $BuildDirectory).Path
$cache=Get-Content -LiteralPath "$build/CMakeCache.txt"
if($cache -notcontains 'ENABLE_DX12:BOOL=ON' -or $cache -notcontains 'ENABLE_VULKAN:BOOL=OFF'){throw 'DX12-only build required'}
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts=(Resolve-Path $ArtifactDirectory).Path
$oldDebug=$env:WHISP_DX12_DEBUG
$ini=Join-Path $build "imgui.ini";$savedIni=if(Test-Path -LiteralPath $ini){[IO.File]::ReadAllBytes($ini)}else{$null}
function Set-DemoLayout([bool]$Inspector){
 $lightingCollapsed=if($Inspector){1}else{0};$inspectorCollapsed=if($Inspector){0}else{1}
 $lightingPos=if($Inspector){"5,660"}else{"950,35"};$inspectorPos=if($Inspector){"950,35"}else{"5,660"}
 $layout="[Window][Scene Hierarchy]`nPos=5,35`nSize=170,640`nCollapsed=0`n`n[Window][Viewport]`nPos=180,35`nSize=760,520`nCollapsed=0`n`n[Window][Lighting]`nPos=$lightingPos`nSize=320,640`nCollapsed=$lightingCollapsed`n`n[Window][Inspector]`nPos=$inspectorPos`nSize=320,650`nCollapsed=$inspectorCollapsed`n"
 foreach($name in @('Physics Stress Test','Statistics','Asset Browser','Material Editor')){$layout+="`n[Window][$name]`nPos=5,680`nSize=170,200`nCollapsed=1`n"}
 [IO.File]::WriteAllText($ini,$layout)
}
Push-Location $build
try {
 if(!$SkipRegression){
  $env:WHISP_DX12_DEBUG='1'
  & './LightSystemRegression.exe' --dx12 *> "$artifacts/light-readback.log";if($LASTEXITCODE -ne 0){throw 'Light regression failed'}
  $shader=Join-Path $build 'shaders/dx12/light_cull.hlsl';$original=[IO.File]::ReadAllBytes($shader)
  try{[IO.File]::WriteAllText($shader,'intentional invalid compute shader for regression');& './LightSystemRegression.exe' --dx12-fallback *> "$artifacts/compute-fallback.log";if($LASTEXITCODE -ne 0){throw 'Compute fallback failed'}}finally{[IO.File]::WriteAllBytes($shader,$original)}
  foreach($name in @('LightingRegression','GpuInstancingRegression','MaterialSystemRegression','PbrCoreRegression','PbrTexturesRegression','IblRegression','ShadowMappingRegression')){& "./$name.exe" --dx12 *> "$artifacts/$name.log";if($LASTEXITCODE -ne 0){throw "$name failed"}}
 }
 Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
 if(!$SkipBenchmarks){
  foreach($count in @(10,50,100,250,500)){foreach($layout in @('distributed','overlap')){
   [string[]]$extra=if($layout -eq 'overlap'){@('--light-overlap')}else{@()};$key="perf-$count-$layout"
   & './WhispEngine.exe' --light-benchmark $count "$artifacts/$key.csv" @extra *> "$artifacts/$key.log"
   if($LASTEXITCODE -ne 0){throw "$key failed"}
   $rows=Import-Csv "$artifacts/$key.csv";if($rows.Count -ne 1200){throw "$key sample count"}
   foreach($s in $rows){if([int]$s.lightTotal -ne $count -or $s.drawCalls -ne '1' -or $s.instances -ne '101' -or $s.gpuValid -ne '1'){throw "$key lights/batching/timestamps mismatch"}}
   Write-Host "${key}: 1200 samples, Forward / Tiled verified"
  }}
 }
 $env:WHISP_DX12_DEBUG='1'
 if(!$SkipDemos){
  foreach($count in @(0,1,10,50,100,250,500)){
   Set-DemoLayout $false
   & './WhispEngine.exe' --light-stress $count --frames 240 --capture-frame "$artifacts/editor-stress-$count.ppm" *> "$artifacts/editor-stress-$count.log";if($LASTEXITCODE -ne 0){throw 'Light stress demo failed'}
  }
  Set-DemoLayout $false
  & './WhispEngine.exe' --light-stress 500 --no-light-gizmos --frames 240 --capture-frame "$artifacts/editor-stress-500-clean.ppm" *> "$artifacts/editor-stress-500-clean.log";if($LASTEXITCODE -ne 0){throw 'Clean stress capture failed'}
  foreach($mode in @('tiled','forward','unlit','no-normal')){
   Set-DemoLayout $true
   [string[]]$extra=switch($mode){'forward'{@('--forward-lights')};'unlit'{@('--no-lighting')};'no-normal'{@('--no-normal-mapping')};default{@()}}
   & './WhispEngine.exe' --light-demo --frames 240 --capture-frame "$artifacts/editor-$mode.ppm" @extra *> "$artifacts/editor-$mode.log";if($LASTEXITCODE -ne 0){throw 'Imported light demo failed'}
  }
 }
 Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
 if(!$SkipCapture){
  $exe=(Resolve-Path "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe").Path
  $capture=Start-Process -FilePath $exe -ArgumentList @('-o',('"'+"$artifacts/lights-500.tracy"+'"'),'-a','127.0.0.1','-s','60','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$artifacts/tracy-capture.log" -RedirectStandardError "$artifacts/tracy-error.log"
  [void]$capture.Handle
  try{& './WhispEngine.exe' --light-benchmark 500 "$artifacts/trace-500.csv" --wait-tracy *> "$artifacts/trace-500.log";if($LASTEXITCODE -ne 0){throw 'Tracy run failed'};if(!$capture.WaitForExit(60000)){throw 'Tracy timeout'};if($capture.ExitCode -ne 0){throw 'Tracy capture failed'}}finally{if(!$capture.HasExited){$capture.Kill()};$capture.Dispose()}
 }
}finally{if($null -ne $savedIni){[IO.File]::WriteAllBytes($ini,$savedIni)};Pop-Location;if($null -eq $oldDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$oldDebug}}

