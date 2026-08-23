<#
Fetches TIGER/Line roads shapefiles for every New Jersey county and extracts
them into data/nj-roads/ - the Phase 4 performance benchmarking dataset (see
DECISIONS.md and Phase 4 in README.md). Never commit data/; it's gitignored.

Idempotent: safe to rerun, skips counties already downloaded/extracted.
#>

$ErrorActionPreference = "Stop"

$year = 2023
$stateFips = "34"  # New Jersey
$countyFips = @(
    "001", "003", "005", "007", "009", "011", "013", "015", "017", "019", "021",
    "023", "025", "027", "029", "031", "033", "035", "037", "039", "041"
)

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$dataDir = Join-Path $repoRoot "data\nj-roads"
$downloadDir = Join-Path $repoRoot "data\_downloads"

New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

foreach ($county in $countyFips) {
    $geoid = "$stateFips$county"
    $fileName = "tl_${year}_${geoid}_roads"
    $zipPath = Join-Path $downloadDir "$fileName.zip"
    $shpPath = Join-Path $dataDir "$fileName.shp"

    if (Test-Path $shpPath) {
        Write-Output "[$geoid] already extracted, skipping"
        continue
    }

    if (-not (Test-Path $zipPath)) {
        $url = "https://www2.census.gov/geo/tiger/TIGER$year/ROADS/$fileName.zip"
        Write-Output "[$geoid] downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    }

    Write-Output "[$geoid] extracting"
    Expand-Archive -Path $zipPath -DestinationPath $dataDir -Force
}

Write-Output "Done. NJ roads data in $dataDir - open it with cartograph_cli info/view."
