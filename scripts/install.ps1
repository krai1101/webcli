param([string]$CurrentVersion = '')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    -not [Environment]::Is64BitOperatingSystem -or
    $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or
    $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
    throw 'This installer requires Windows x64. Use install.sh on Linux or macOS.'
}
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$repository = 'https://github.com/krai1101/webcli'
$release = Invoke-RestMethod 'https://api.github.com/repos/krai1101/webcli/releases/latest'
$tag = $release.tag_name
if ($tag -notmatch '^v\d+\.\d+\.\d+$') { throw 'No public stable release found.' }
if ($CurrentVersion -and [version]$CurrentVersion -ge [version]$tag.Substring(1)) {
    Write-Host "WebCLI $CurrentVersion is already up to date."
    exit 0
}
$asset = "webcli-$tag-windows-x64.zip"
$work = Join-Path ([IO.Path]::GetTempPath()) ('webcli-install-' + [Guid]::NewGuid())
[IO.Directory]::CreateDirectory($work) | Out-Null
try {
    Write-Host "Downloading WebCLI $tag (Windows x64)..."
    foreach ($name in @($asset, "$asset.sha256")) {
        Invoke-WebRequest -UseBasicParsing "$repository/releases/download/$tag/$name" -OutFile (Join-Path $work $name)
    }
    $archive = Join-Path $work $asset
    $expected = ((Get-Content -LiteralPath "$archive.sha256" -TotalCount 1) -split '\s+')[0]
    if ($expected -notmatch '^[0-9a-f]{64}$') { throw 'Invalid release checksum.' }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
        throw 'Release checksum does not match.'
    }
    $unpacked = Join-Path $work 'unpacked'
    Expand-Archive -LiteralPath $archive -DestinationPath $unpacked
    $source = Join-Path $unpacked ([IO.Path]::GetFileNameWithoutExtension($asset))
    $executable = Join-Path $source 'webcli.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Release executable is missing.' }
    & $executable --version
    if ($LASTEXITCODE -ne 0) {
        throw 'The release cannot run. Install the Microsoft Visual C++ x64 Redistributable and retry.'
    }

    # Keep the executable and its DLLs together; do not replace running versions.
    $root = Join-Path $env:LOCALAPPDATA 'WebCLI\versions'
    $destination = Join-Path $root ("$tag-" + $expected.Substring(0, 12))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    if (-not (Test-Path -LiteralPath $destination)) {
        Move-Item -LiteralPath $source -Destination $destination
    }
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $entries = @($userPath -split ';' | Where-Object {
        $_ -and -not $_.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
    })
    [Environment]::SetEnvironmentVariable('Path', (@($destination) + $entries -join ';'), 'User')
    $env:Path = $destination + ';' + $env:Path
    Write-Host "Installed: $destination"
    Write-Host 'Run: webcli. Open a new terminal in other applications to refresh PATH.'
} finally {
    # Only remove the freshly allocated installer directory under the OS temp root.
    $resolved = [IO.Path]::GetFullPath($work)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved).StartsWith('webcli-install-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
