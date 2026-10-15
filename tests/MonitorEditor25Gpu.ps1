param([string]$OutputFile,[string]$StopMarker)
$ErrorActionPreference='Stop'
Set-Content -LiteralPath $OutputFile -Value 'timestamp, pstate, graphicsMHz, memoryMHz, powerW, temperatureC, utilizationPercent' -Encoding utf8
$monitor25Deadline=[DateTime]::UtcNow.AddSeconds(40)
while([DateTime]::UtcNow -lt $monitor25Deadline -and !(Test-Path -LiteralPath $StopMarker)){
    $monitor25Sample=& nvidia-smi --query-gpu=timestamp,pstate,clocks.gr,clocks.mem,power.draw,temperature.gpu,utilization.gpu --format=csv,noheader
    if($LASTEXITCODE -ne 0){throw 'nvidia-smi query failed'}
    Add-Content -LiteralPath $OutputFile -Value $monitor25Sample -Encoding utf8
    Start-Sleep -Milliseconds 250
}
