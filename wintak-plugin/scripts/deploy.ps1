[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$RestartWinTak
)

# Build PRISM for WinTAK and install it for this Windows user:
#   %APPDATA%\WinTAK\Plugins\PrismHud.WinTAK\PrismHud.WinTAK.dll
# registered in %APPDATA%\WinTAK\Plugins.xml (same scheme as RFSim.WinTAK).

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = Join-Path $root 'src\PrismHud.WinTAK\PrismHud.WinTAK.csproj'
$msbuild = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw 'Visual Studio 2022 MSBuild was not found.' }

& $msbuild $project /restore "/p:Configuration=$Configuration" /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "PRISM build failed with exit code $LASTEXITCODE." }

$built = Join-Path $root "src\PrismHud.WinTAK\bin\x64\$Configuration\net48\PrismHud.WinTAK.dll"
if (-not (Test-Path -LiteralPath $built)) { throw "Build did not produce $built." }

if ($RestartWinTak) {
    $running = @(Get-Process -Name WinTAK -ErrorAction SilentlyContinue)
    foreach ($p in $running) { [void]$p.CloseMainWindow() }
    if ($running.Count -gt 0) {
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
}
if (Get-Process -Name WinTAK -ErrorAction SilentlyContinue) {
    throw 'WinTAK is running and holds the plugin DLL. Close it (or pass -RestartWinTak) and run this again.'
}

$dir = Join-Path $env:APPDATA 'WinTAK\Plugins\PrismHud.WinTAK'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
Copy-Item -LiteralPath $built -Destination $dir -Force
$pdb = [IO.Path]::ChangeExtension($built, '.pdb')
if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination $dir -Force }

$manifestPath = Join-Path $env:APPDATA 'WinTAK\Plugins.xml'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Copy-Item -LiteralPath (Join-Path $env:ProgramFiles 'WinTAK\Plugins.xml') -Destination $manifestPath
}
[xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
$pluginPath = 'Plugins/PrismHud.WinTAK'
if ($null -eq $manifest.SelectSingleNode("/Plugins/Plugin[@path='$pluginPath']")) {
    $entry = $manifest.CreateElement('Plugin')
    $entry.SetAttribute('path', $pluginPath)
    [void]$manifest.DocumentElement.AppendChild($entry)
    $settings = [System.Xml.XmlWriterSettings]::new()
    $settings.Indent = $true
    $settings.Encoding = [System.Text.UTF8Encoding]::new($false)
    $writer = [System.Xml.XmlWriter]::Create($manifestPath, $settings)
    try { $manifest.Save($writer) } finally { $writer.Dispose() }
}
Write-Host "Installed $dir\PrismHud.WinTAK.dll"

if ($RestartWinTak) {
    Start-Process -FilePath (Join-Path $env:ProgramFiles 'WinTAK\WinTAK.exe')
}
