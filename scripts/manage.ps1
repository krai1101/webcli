param(
    [Parameter(Mandatory=$true)][ValidateSet('update','uninstall','cleanup')][string]$Operation,
    [Parameter(Mandatory=$true)][string]$Executable,
    [int]$ParentId
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'WebCLI\versions'))
$executablePath = [IO.Path]::GetFullPath($Executable)
$directory = Split-Path -Parent $executablePath
if ((Split-Path -Parent $directory) -ne $root -or
    (Split-Path -Leaf $directory) -notmatch '^v\d+\.\d+\.\d+-[0-9a-f]{12}$' -or
    (Split-Path -Leaf $executablePath) -ne 'webcli.exe') {
    throw 'This is not a script-managed installation. Use the installation command or remove your extracted ZIP manually.'
}
foreach ($path in @((Split-Path -Parent $root), $root, $directory)) {
    if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing to manage an installation through a directory link.'
    }
}
function Assert-InstallationTargets {
    foreach ($target in @(Get-ChildItem -LiteralPath $root -Force)) {
        if (-not $target.PSIsContainer -or $target.Name -notmatch '^v\d+\.\d+\.\d+-[0-9a-f]{12}$' -or
            $target.Parent.FullName -ne $root -or
            ($target.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Unexpected content in installation directory; removal stopped.'
        }
    }
    if (Get-ChildItem -LiteralPath $root -Recurse -Force | Where-Object {
        $_.Attributes -band [IO.FileAttributes]::ReparsePoint
    } | Select-Object -First 1) {
        throw 'Unexpected directory or file link in installation; removal stopped.'
    }
}
if ($Operation -eq 'update') {
    $version = (& $executablePath --version) -replace '^WebCLI ', ''
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the installed version.' }
    & (Join-Path $PSScriptRoot 'install.ps1') -CurrentVersion $version
    exit 0
}
if ($Operation -eq 'cleanup') {
    try {
        Wait-Process -Id $ParentId -ErrorAction SilentlyContinue
        # Validate the exact, installer-owned targets before recursive removal.
        Assert-InstallationTargets
        Remove-Item -LiteralPath $root -Recurse -Force
    } catch {
        $_ | Out-String | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'uninstall-error.txt')
        exit 1
    }
    $work = [IO.Path]::GetFullPath($PSScriptRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ((Split-Path -Parent $work) -eq $tempRoot -and
        (Split-Path -Leaf $work) -like 'webcli-manage-*') {
        Remove-Item -LiteralPath $work -Recurse -Force
    }
    exit 0
}
Assert-InstallationTargets
# Other live installations may still own DLLs. Never terminate a user's prompt.
foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name = 'webcli.exe'")) {
    if ($process.ProcessId -eq $ParentId -or -not $process.ExecutablePath -or
        -not $process.ExecutablePath.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }
    $parent = Get-Process -Id $process.ParentProcessId -ErrorAction SilentlyContinue
    if (-not $parent -and $process.CommandLine -match '\s--worker$') {
        Stop-Process -Id $process.ProcessId -ErrorAction Stop
    } else {
        throw 'Quit all running WebCLI prompts and close their browser instances before uninstalling.'
    }
}
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$entries = @($userPath -split ';' | Where-Object {
    $_ -and -not $_.TrimEnd('\').StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
})
# Launch before changing PATH so a helper-start failure leaves installation intact.
$quotedScript = '"' + (Join-Path $PSScriptRoot 'manage.ps1') + '"'
$quotedExe = '"' + $executablePath + '"'
$helper = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -WindowStyle Hidden -PassThru -WorkingDirectory ([IO.Path]::GetTempPath()) -ArgumentList @(
    '-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$quotedScript,
    '-Operation','cleanup','-Executable',$quotedExe,'-ParentId',"$ParentId")
try {
    [Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'User')
} catch {
    Stop-Process -Id $helper.Id -ErrorAction SilentlyContinue
    throw
}
Write-Host 'Removing WebCLI after this command exits. Saved browsing data is kept.'
Write-Host 'Open a new terminal to refresh PATH.'
Write-Host "If removal fails, details are written to $PSScriptRoot\uninstall-error.txt"
