<#
Fetches a Natural Earth shaded-relief raster into data/raster/ - the Phase 11
raster dataset (see DECISIONS.md and Phase 11 in CLAUDE.md). Never commit
data/; it's gitignored.

NE1_50M_SR_W is a 3-band RGB GeoTIFF in EPSG:4326, ~10800x5400 - big enough to
exercise overview selection and windowed reads, small enough to download in a
few seconds. The unit tests don't need it: they generate a tiny synthetic
GeoTIFF in-process so the suite stays self-contained.

Idempotent: safe to rerun, skips what's already there.
#>

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$dataDir = Join-Path $repoRoot "data\raster"
$downloadDir = Join-Path $repoRoot "data\_downloads"

New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

$name = "NE1_50M_SR_W"
$zipPath = Join-Path $downloadDir "$name.zip"
$tifPath = Join-Path $dataDir "$name\$name.tif"

if (Test-Path $tifPath) {
    Write-Output "[$name] already extracted, skipping"
} else {
    if (-not (Test-Path $zipPath)) {
        $url = "https://naciscdn.org/naturalearth/50m/raster/$name.zip"
        Write-Output "[$name] downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    }
    Write-Output "[$name] extracting"
    Expand-Archive -Path $zipPath -DestinationPath $dataDir -Force
}

Write-Output "Done. Raster in $dataDir - open it with cartograph_cli info/view."
