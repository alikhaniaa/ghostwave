param([string]$Version = '0.2.0', [string]$Executable = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if ($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9.+_-]*$') {
        throw 'Version must be a filename-safe version identifier.'
    }
    if ($Executable) {
        $gameExecutable = Get-Item -LiteralPath $Executable -ErrorAction Stop
        if ($gameExecutable.PSIsContainer) { throw 'Executable must identify a file.' }
    } else {
        $supportedExecutables = @('build/ghostwave.exe', 'build/Release/ghostwave.exe',
                                 'build-cmake/ghostwave.exe', 'build-cmake/Release/ghostwave.exe')
        $gameExecutable = $supportedExecutables | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            ForEach-Object { Get-Item -LiteralPath $_ } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if (!$gameExecutable) { throw 'No supported game build found. Build first or pass -Executable.' }
    }
    Write-Output "Packaging executable: $($gameExecutable.FullName)"
    $distributionRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
    $packageName = "Ghostwave-$Version-windows-x64"
    $packageRoot = [IO.Path]::GetFullPath((Join-Path $distributionRoot $packageName))
    $stagingRoot = [IO.Path]::GetFullPath((Join-Path $distributionRoot ('.package-stage-' + [guid]::NewGuid().ToString('N'))))
    $stagingPackage = Join-Path $stagingRoot $packageName
    $stagingZip = Join-Path $stagingRoot "$packageName.zip"
    # These exact resolved children are the only recursive cleanup targets.
    if ((Split-Path -Parent $packageRoot) -ne $distributionRoot -or
        (Split-Path -Parent $stagingRoot) -ne $distributionRoot) {
        throw 'Package paths must stay directly inside the workspace dist directory.'
    }
    New-Item -ItemType Directory -Force $stagingPackage | Out-Null
    Copy-Item -LiteralPath $gameExecutable.FullName -Destination (Join-Path $stagingPackage 'ghostwave.exe')
    $labExecutable = Join-Path $gameExecutable.DirectoryName 'field_lab.exe'
    if (Test-Path -LiteralPath $labExecutable -PathType Leaf) {
        Copy-Item -LiteralPath $labExecutable -Destination (Join-Path $stagingPackage 'field_lab.exe')
    }
    Copy-Item -LiteralPath assets,config,docs -Recurse -Destination $stagingPackage
    Copy-Item -LiteralPath README.md -Destination $stagingPackage
    Compress-Archive -Path (Join-Path $stagingPackage '*') -DestinationPath $stagingZip
    # Publish only after the complete fresh tree has been archived successfully.
    # Replacing a generated version directory removes stale files from old builds.
    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    Move-Item -LiteralPath $stagingPackage -Destination $packageRoot
    Move-Item -LiteralPath $stagingZip -Destination "$packageRoot.zip" -Force
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath "$packageRoot.zip"
    "$($hash.Hash)  $packageName.zip" | Set-Content -LiteralPath "$packageRoot.zip.sha256" -Encoding ascii
    $hash
} finally { Pop-Location }
