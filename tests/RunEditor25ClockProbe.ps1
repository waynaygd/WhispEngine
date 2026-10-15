param()
$ErrorActionPreference='Stop'
$clock25Repo=Split-Path -Parent $PSScriptRoot
$clock25Out=Join-Path $clock25Repo 'out/diagnostics/editor25'
Copy-Item -LiteralPath (Join-Path $clock25Out 'baseline-runtime/EditorBaseline.exe') -Destination (Join-Path $clock25Out 'baseline-runtime/WhispEngine.exe')
foreach($phase in @('before-clock','after-clock')){
    $probe=$null
    try {
        $marker=Join-Path $clock25Out ($phase+'-stop')
        if(Test-Path -LiteralPath $marker){Remove-Item -LiteralPath $marker}
        $probe=Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList @('-NoProfile','-File',('"'+(Join-Path $PSScriptRoot 'MonitorEditor25Gpu.ps1')+'"'),'-OutputFile',('"'+(Join-Path $clock25Out ($phase+'-clocks.csv'))+'"'),'-StopMarker',('"'+$marker+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $clock25Out ($phase+'-monitor.log')) -RedirectStandardError (Join-Path $clock25Out ($phase+'-monitor-error.log'))
        if($phase -eq 'before-clock') {& (Join-Path $PSScriptRoot 'RunEditor25Diagnostics.ps1') -BuildDirectory out/diagnostics/editor25/baseline-runtime -Executable WhispEngine.exe -Phase $phase -Cases @('500')}
        else {& (Join-Path $PSScriptRoot 'RunEditor25Diagnostics.ps1') -Phase $phase -Cases @('500')}
    } finally {if($null -ne $probe){Set-Content -LiteralPath $marker -Value stop;if(!$probe.WaitForExit(5000)){$probe.Kill();$probe.WaitForExit()}}}
}
