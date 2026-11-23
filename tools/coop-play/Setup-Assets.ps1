# Setup-Assets.ps1
#
# Puts the game data next to A51.exe so that the co-op build can start.
#
# The data is the open "Dreamland-51 Asset Pack" of the Area 51 PC port
# (https://github.com/Source2Spy/area51-pc/releases/tag/ASSETS-10.11.2026-RC1).
# This script asks before it downloads anything (about 1.5 GB), checks the file against the
# fingerprint this build was tested with, and merges it into this folder.
# Your original Area 51 installation is neither needed nor touched.
#
# You normally do not start this yourself: PLAY-COOP-2-PLAYERS.cmd runs it on the first start.
# By hand:  powershell -NoProfile -ExecutionPolicy Bypass -File .\Setup-Assets.ps1
#
# Options
#   -AssetsZip <file>          use an Asset Pack zip you already downloaded (no download)
#   -Yes                       do not ask before downloading (automatic tests only)
#   -AcceptDifferentAssets     accept an Asset Pack that is not the tested one

[CmdletBinding()]
param(
    [string] $AssetsZip = '',
    [switch] $Yes,
    [switch] $AcceptDifferentAssets
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$AssetUrl        = 'https://github.com/Source2Spy/area51-pc/releases/download/ASSETS-10.11.2026-RC1/Dreamlnd51-Assets-RC1.zip'
$AssetBytes      = 1575841821
$AssetSha256     = '50e2403ae3e14ff561fdc1ebcb432c08edf4d0260bf0b9d18b7f034adc31bc32'
$NeedFreeGB      = 7
$MarkerName      = '.coop-assets-ok'
$OriginalInstall = 'D:\Games\AREA-51'

function Stop-Setup {
    param([string] $Message)
    Write-Host ''
    Write-Host ('STOPPED: ' + $Message) -ForegroundColor Red
    exit 1
}

$Here = $PSScriptRoot
if ([string]::IsNullOrEmpty($Here)) { $Here = (Get-Location).ProviderPath }
$Here = $Here.TrimEnd('\')

# Never work inside the original install.
if (($Here -ieq $OriginalInstall) -or ($Here -ilike ($OriginalInstall + '\*'))) {
    Stop-Setup ('This folder is inside your original Area 51 install (' + $Here + '). Extract the co-op zip into a new folder of its own, for example C:\Games\Area51-Coop.')
}

if (-not (Test-Path -LiteralPath (Join-Path $Here 'A51.exe'))) {
    Stop-Setup 'A51.exe is not in this folder. Extract the whole zip first (right click, Extract All), then start this from the extracted folder.'
}
if (-not (Test-Path -LiteralPath (Join-Path $Here 'SHADERS'))) {
    Stop-Setup 'The SHADERS folder is missing. Extract the whole zip first (right click, Extract All).'
}

# Files that came out of a downloaded zip carry a "from the internet" mark that makes Windows
# ask again and again.  Clear it on the few files in this folder (not on the game data).
Get-ChildItem -LiteralPath $Here -File -ErrorAction SilentlyContinue | Unblock-File -ErrorAction SilentlyContinue

$MarkerPath = Join-Path $Here $MarkerName
if ((Test-Path -LiteralPath $MarkerPath) -and (Test-Path -LiteralPath (Join-Path $Here 'LEVELS'))) {
    Write-Host 'The game data is already in place. Nothing to do.'
    exit 0
}

# Free disk space on the drive of this folder (skipped for network paths).
try {
    $Root   = [System.IO.Path]::GetPathRoot($Here)
    $FreeGB = [math]::Round((New-Object System.IO.DriveInfo $Root).AvailableFreeSpace / 1GB, 1)
    if ($FreeGB -lt $NeedFreeGB) {
        Stop-Setup ('Only ' + $FreeGB + ' GB are free on ' + $Root + '; about ' + $NeedFreeGB + ' GB are needed (1.5 GB download plus 4 GB of game data). Free some space or extract the co-op zip to another drive.')
    }
}
catch {
    Write-Host 'Could not check the free disk space; continuing.'
}

$Work    = Join-Path $Here '_t'
$Extract = Join-Path $Work 'x'
New-Item -ItemType Directory -Force -Path $Work | Out-Null

#------------------------------------------------------------------------------
# 1. The Asset Pack: a file you already have, or a download after asking
#------------------------------------------------------------------------------
$Zip = $AssetsZip
if ($Zip -ne '') {
    if (-not (Test-Path -LiteralPath $Zip)) { Stop-Setup ('The Asset Pack file was not found: ' + $Zip) }
    $Zip = (Resolve-Path -LiteralPath $Zip).ProviderPath
}
else {
    $Zip = Join-Path $Work 'Dreamlnd51-Assets-RC1.zip'
    $Have = $false
    if (Test-Path -LiteralPath $Zip) {
        $Size = (Get-Item -LiteralPath $Zip).Length
        if ($Size -eq $AssetBytes) { $Have = $true }
        elseif ($Size -gt $AssetBytes) { Remove-Item -LiteralPath $Zip -Force }
    }

    if (-not $Have) {
        if (-not $Yes) {
            Write-Host ''
            Write-Host 'This co-op build needs the game data. It comes from the open "Dreamland-51 Asset Pack"'
            Write-Host 'of the Area 51 PC port project (github.com/Source2Spy/area51-pc, release ASSETS-10.11.2026-RC1).'
            Write-Host ''
            Write-Host '  Download : about 1.5 GB'
            Write-Host ('  Disk use : about 4 GB, inside this folder: ' + $Here)
            Write-Host '  Your original Area 51 install is not used and not touched.'
            Write-Host ''
            $Answer = Read-Host 'Download it now? Type Y and press Enter to start, anything else to cancel'
            if ($Answer -notmatch '^[yY]') { Stop-Setup 'Cancelled. Nothing was downloaded.' }
        }

        $Curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if ($Curl) {
            Write-Host 'Downloading. You can stop with Ctrl+C and start again later; it continues where it stopped.'
            $Old = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            & $Curl.Source -L --fail --retry 5 --retry-delay 3 -C - -o $Zip $AssetUrl
            $Code = $LASTEXITCODE
            $ErrorActionPreference = $Old
            if ($Code -ne 0) {
                Stop-Setup ('The download failed (curl exit code ' + $Code + '). Check the internet connection and start this again; it continues where it stopped.')
            }
        }
        else {
            Write-Host 'Downloading (no progress display, please wait)...'
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $AssetUrl -OutFile $Zip -UseBasicParsing
        }
    }
}

#------------------------------------------------------------------------------
# 2. Check it is the pack this build was tested with
#------------------------------------------------------------------------------
Write-Host 'Checking the Asset Pack (SHA-256), a few seconds...'
$Sha = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLower()
if ($Sha -ne $AssetSha256) {
    if ($AcceptDifferentAssets) {
        Write-Host ('WARNING: this is not the Asset Pack the build was tested with (SHA-256 ' + $Sha + '). Going on because you asked for it.')
    }
    else {
        Stop-Setup ("The Asset Pack is not the one this build was tested with.`r`n  expected SHA-256 " + $AssetSha256 + "`r`n  found    SHA-256 " + $Sha + "`r`nThe project may have replaced the file. Tell me, or start this script with -AcceptDifferentAssets to try it anyway.")
    }
}

#------------------------------------------------------------------------------
# 3. Unpack, merge the PC textures into Main-Assets, move everything next to A51.exe
#------------------------------------------------------------------------------
if (Test-Path -LiteralPath $Extract) { Remove-Item -LiteralPath $Extract -Recurse -Force }
Write-Host 'Unpacking (about 4 GB, a few minutes)...'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $Extract)

$Main = Get-ChildItem -LiteralPath $Extract -Directory -Recurse -Depth 2 -Filter 'Main-Assets' | Select-Object -First 1
if (-not $Main) { Stop-Setup 'The Asset Pack has no Main-Assets folder, so it is not what this build expects.' }
$Textures = Join-Path $Main.Parent.FullName 'PC-Textures'
if (-not (Test-Path -LiteralPath $Textures)) { Stop-Setup 'The Asset Pack has no PC-Textures folder next to Main-Assets, so it is not what this build expects.' }

function Merge-Folder {
    param([string] $From, [string] $To)
    & robocopy.exe $From $To /E /MOVE /IS /NFL /NDL /NJH /NJS /NP /R:2 /W:2 | Out-Null
    if ($LASTEXITCODE -ge 8) { Stop-Setup ('Moving files failed (robocopy code ' + $LASTEXITCODE + ') for ' + $From) }
}

Write-Host 'Putting the game data in place...'
Merge-Folder $Main.FullName $Here
Merge-Folder $Textures      $Here

if (-not (Test-Path -LiteralPath (Join-Path $Here 'LEVELS'))) {
    Stop-Setup 'After unpacking there is no LEVELS folder here, so the Asset Pack is not what this build expects.'
}

Remove-Item -LiteralPath $Work -Recurse -Force
Set-Content -LiteralPath $MarkerPath -Value $Sha -Encoding ASCII
Write-Host 'Done. The game data is in place.'
exit 0
