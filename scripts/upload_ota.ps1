param([string]$IpAddress = '192.168.3.219')

$projectRoot = Split-Path -Parent $PSScriptRoot
$secretsPath = Join-Path $projectRoot 'include/secrets.h'
$secrets = [System.IO.File]::ReadAllText($secretsPath)
$match = [regex]::Match($secrets, 'OTA_PASSWORD\s*=\s*"([^"\r\n]+)"')
if (-not $match.Success) { throw 'OTA_PASSWORD ausente em include/secrets.h.' }

$pio = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.platformio/penv/Scripts/pio.exe'
if (-not (Test-Path $pio)) { $pio = (Get-Command pio -ErrorAction Stop).Source }

$previousFlags = $env:PLATFORMIO_UPLOAD_FLAGS
try {
    $env:PLATFORMIO_UPLOAD_FLAGS = '--auth=' + $match.Groups[1].Value
    Push-Location $projectRoot
    try {
        & $pio run -e cyd_ota -t upload --upload-port $IpAddress 2>&1 |
            ForEach-Object {
                $line = $_.ToString().Replace($match.Groups[1].Value, '[REDACTED]')
                if ($line -notmatch '^(Uploading:|System\.Management\.Automation\.RemoteException$)') { $line }
            }
        if ($LASTEXITCODE -ne 0) { throw "Upload OTA falhou (codigo $LASTEXITCODE)." }
    } finally {
        Pop-Location
    }
} finally {
    $env:PLATFORMIO_UPLOAD_FLAGS = $previousFlags
}
