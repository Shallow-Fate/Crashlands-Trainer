$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$release = Join-Path $taskRoot 'dist\CrashlandsTrainer-v1.3.0'
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination (Join-Path $release 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'vendor\minhook\LICENSE.txt') -Destination (Join-Path $release 'MinHook-LICENSE.txt') -Force
$files = @('CrashlandsTrainer.exe', 'CrashlandsTrainer.dll', 'README.md', 'MinHook-LICENSE.txt') | ForEach-Object { Join-Path $release $_ }
$zip = Join-Path $taskRoot 'dist\CrashlandsTrainer-1.3.0.zip'
Compress-Archive -LiteralPath $files -DestinationPath $zip -Force
Get-Item -LiteralPath $zip | Select-Object FullName,Length
