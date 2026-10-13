param(
 [string]$BuildDirectory="$PSScriptRoot/../out/build/shadows-dx12",
 [string]$ArtifactDirectory="$PSScriptRoot/../out/diagnostics/ibl",
 [switch]$SkipRegression,[switch]$SkipBenchmarks,[switch]$SkipDemos,[switch]$SkipCapture
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
 if(!$SkipRegression){$env:WHISP_DX12_DEBUG='1';foreach($name in @('IblRegression','LightingRegression','GpuInstancingRegression','PbrCoreRegression','PbrTexturesRegression','MaterialSystemRegression','ShadowMappingRegression')){
  & "./$name.exe" --dx12 *> "$artifacts/$name.log";if($LASTEXITCODE -ne 0){throw "$name failed"}
 }; & './ShadowQualityRegression.exe' *> "$artifacts/ShadowQualityRegression.log";if($LASTEXITCODE -ne 0){throw 'Shadow quality failed'}}
 Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
 if(!$SkipBenchmarks){foreach($count in @(100,250,500,1000)){foreach($instancing in @(1,0)){
  [string[]]$extra=if($instancing){@()}else{@('--no-instancing')};$key="perf-$count-i$instancing"
  & './WhispEngine.exe' --ibl-benchmark $count "$artifacts/$key.csv" @extra *> "$artifacts/$key.log"
  if($LASTEXITCODE -ne 0){throw "$key failed"};$samples=Import-Csv "$artifacts/$key.csv"
  if($samples.Count -ne 2400){throw "$key sample count"}
  foreach($s in $samples){$draws=if($instancing){1}else{$count+1};$output=if([int]$s.iblMask){1}else{0}
   if([int]$s.instancing -ne $instancing -or [int]$s.instances -ne ($count+1) -or [int]$s.drawCalls -ne $draws -or [int]$s.shadowDraws -ne $draws -or [int]$s.outputDrawCalls -ne $output -or $s.gpuValid -ne '1' -or [int64]$s.environmentGpuBytes -eq 0){throw "$key batching/HDR/timestamps mismatch"}
  };Write-Host "$key OFF / diffuse / specular / full: 2400 samples verified"
 }}}
 $env:WHISP_DX12_DEBUG='1'
 if(!$SkipDemos){foreach($mode in @('full','off','diffuse','specular','rotated','no-normal','no-tone')){
  [string[]]$extra=switch($mode){'off'{@('--no-ibl')};'diffuse'{@('--ibl-diffuse')};'specular'{@('--ibl-specular')};'rotated'{@('--environment-rotation','90')};'no-normal'{@('--no-normal-mapping')};'no-tone'{@('--no-tone-mapping')};default{@()}}
  & './WhispEngine.exe' --ibl-demo --frames 240 --capture-frame "$artifacts/editor-$mode.ppm" @extra *> "$artifacts/editor-$mode.log"
  if($LASTEXITCODE -ne 0){throw "$mode demo failed"}
 }}
 Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
 if(!$SkipCapture){
  $exe=(Resolve-Path "$PSScriptRoot/../out/diagnostics/tracy-tools/tracy-capture.exe").Path
  $capture=Start-Process -FilePath $exe -ArgumentList @('-o',('"'+"$artifacts/ibl-500.tracy"+'"'),'-a','127.0.0.1','-s','60','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$artifacts/tracy-capture.log" -RedirectStandardError "$artifacts/tracy-capture-error.log"
  [void]$capture.Handle
  try{& './WhispEngine.exe' --ibl-benchmark 500 "$artifacts/trace-500.csv" --wait-tracy *> "$artifacts/trace-500.log";if($LASTEXITCODE -ne 0){throw 'Tracy run failed'};if(!$capture.WaitForExit(60000)){throw 'Tracy capture timeout'};if($capture.ExitCode -ne 0){throw 'Tracy capture failed'}}finally{if(!$capture.HasExited){$capture.Kill()};$capture.Dispose()}
 }
} finally {Pop-Location;if($null -eq $oldDebug){Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue}else{$env:WHISP_DX12_DEBUG=$oldDebug}}
