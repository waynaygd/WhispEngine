param([string]$BuildDirectory='out/build/shadows-dx12')
$ErrorActionPreference='Stop'
$trace24Repo=Split-Path -Parent $PSScriptRoot
$trace24Build=[IO.Path]::GetFullPath((Join-Path $trace24Repo $BuildDirectory))
$trace24Out=Join-Path $trace24Repo 'out/diagnostics/editor24'
$trace24Saved=@{}
foreach($name in @('imgui.ini','editor-preferences.json')){
    $path=Join-Path $trace24Build $name
    $trace24Saved[$name]=if(Test-Path -LiteralPath $path){[IO.File]::ReadAllBytes($path)}else{$null}
}
$trace24Capture=$null
Push-Location $trace24Build
try {
    $capturePath=Join-Path $trace24Out 'aa500.tracy'
    $captureExe=Join-Path $trace24Repo 'out/diagnostics/tracy-tools/tracy-capture.exe'
    $trace24Capture=Start-Process -FilePath $captureExe -ArgumentList @('-o',('"'+$capturePath+'"'),'-a','127.0.0.1','-s','30','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $trace24Out 'tracy-capture.log') -RedirectStandardError (Join-Path $trace24Out 'tracy-capture-error.log')
    & (Join-Path $trace24Build 'WhispEngine.exe') --render-benchmark 500 (Join-Path $trace24Out 'tracy-500.csv') --diagnostic-viewport 744 411 --editor-gradient --aa-quality 2 --wait-tracy *> (Join-Path $trace24Out 'tracy-engine.log')
    if($LASTEXITCODE -ne 0){throw 'Tracy engine run failed'}
    if(!$trace24Capture.WaitForExit(50000)){throw 'Tracy capture did not finish within bounded wait'}
    if($trace24Capture.ExitCode -ne 0){throw 'Tracy capture failed'}
    & (Join-Path $trace24Repo 'out/diagnostics/tracy-tools/tracy-csvexport.exe') $capturePath > (Join-Path $trace24Out 'tracy-zones.csv')
    if($LASTEXITCODE -ne 0){throw 'Tracy export failed'}
} finally {
    if($null -ne $trace24Capture -and !$trace24Capture.HasExited){$trace24Capture.Kill()}
    foreach($name in @('imgui.ini','editor-preferences.json')){
        $path=Join-Path $trace24Build $name
        if($null -ne $trace24Saved[$name]){[IO.File]::WriteAllBytes($path,$trace24Saved[$name])}elseif(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
    }
    Pop-Location
}
