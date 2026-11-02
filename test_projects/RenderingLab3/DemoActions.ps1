param([Parameter(Mandatory=$true)][ValidateSet('BreakShader','RestoreShader','MissingTexture','RestoreAlternate','RestoreBase')][string]$Action)
$ErrorActionPreference='Stop'
$demoRoot=[IO.Path]::GetFullPath($PSScriptRoot)
if(!(Test-Path -LiteralPath (Join-Path $demoRoot 'RenderingLab3.whispproject'))){throw 'RenderingLab3 descriptor missing'}
# All targets are fixed paths inside this demo. Never modify engine shader sources.
switch($Action){
    BreakShader { [IO.File]::WriteAllText((Join-Path $demoRoot 'Assets/shaders/demo_surface.hlsl'),'invalid HLSL - intentional RenderingLab3 demonstration') }
    RestoreShader { Copy-Item -LiteralPath (Join-Path $demoRoot 'Recovery/demo_surface.hlsl.good') -Destination (Join-Path $demoRoot 'Assets/shaders/demo_surface.hlsl') }
    MissingTexture {
        $demoMaterial=Join-Path $demoRoot 'Assets/materials/demo_alternate.material.json'
        $data=Get-Content -LiteralPath $demoMaterial -Raw | ConvertFrom-Json
        $data.texturePath='textures/INTENTIONALLY_MISSING.png'
        [IO.File]::WriteAllText($demoMaterial,($data | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    }
    RestoreAlternate { Copy-Item -LiteralPath (Join-Path $demoRoot 'Recovery/demo_alternate.material.json') -Destination (Join-Path $demoRoot 'Assets/materials/demo_alternate.material.json') }
    RestoreBase { Copy-Item -LiteralPath (Join-Path $demoRoot 'Recovery/demo_base.material.json') -Destination (Join-Path $demoRoot 'Assets/materials/demo_base.material.json') }
}
Write-Host "RenderingLab3: $Action. Only demo copies were changed."
