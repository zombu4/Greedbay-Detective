# Install test, Windows (PLAN 11 "Install / uninstall"): the setup .exe,
# silent and per user -> the app runs and makes its work folder -> the
# silent uninstall (keeps the saved lookups and history) -> install again,
# run -> the app's own --uninstall-data=delete, then the uninstaller. After
# each step only the listed paths are checked; nothing is searched for.
#
# usage: windows.ps1 <the setup .exe> <app identifier>
param([string]$Setup, [string]$Identifier)
$ErrorActionPreference = 'Stop'

$install = Join-Path $env:LOCALAPPDATA "Programs\$Identifier"
$program = Join-Path $install "bin\$Identifier.exe"
$uninstaller = Join-Path $install 'Uninstall.exe'
$work = Join-Path $env:LOCALAPPDATA $Identifier
$entry = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$Identifier"

function Fail([string]$why) { throw "INSTALL TEST FAILED: $why" }

function Install-App {
    $setup = Start-Process -FilePath $Setup -ArgumentList '/S' -Wait -PassThru
    if ($setup.ExitCode -ne 0) { Fail "setup exited with $($setup.ExitCode)" }
    if (-not (Test-Path $program)) { Fail "the program is not at $program" }
    if (-not (Test-Path $entry)) { Fail "no Apps & features entry at $entry" }
}

# Runs the app until it has made its work folder (at most 90 s), then stops it.
function Run-App {
    $app = Start-Process -FilePath $program -PassThru
    $waited = 0
    while (-not (Test-Path (Join-Path $work '.workfolder'))) {
        if ($waited -ge 90) { Fail 'the app made no work folder in 90 s' }
        if ($app.HasExited) { Fail "the app ended ($($app.ExitCode)) before it made its work folder" }
        Start-Sleep -Seconds 1
        $waited++
    }
    Start-Sleep -Seconds 3
    Stop-Process -Id $app.Id
    $app.WaitForExit()
    if (-not (Test-Path (Join-Path $work "$Identifier.sqlite"))) { Fail 'the app made no database' }
}

# The uninstaller copies itself away and returns at once; wait until the
# program and its Apps & features entry are gone (at most 120 s).
function Uninstall-Silently {
    Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait | Out-Null
    $waited = 0
    while ((Test-Path $program) -or (Test-Path $entry)) {
        if ($waited -ge 120) { Fail 'the uninstaller did not remove the program in 120 s' }
        Start-Sleep -Seconds 1
        $waited++
    }
    if (Test-Path $install) { Fail "the program folder is still there: $install" }
}

if (Test-Path $work) { Fail "a work folder exists before the test: $work" }

# A silent uninstall keeps the history.
Install-App
Run-App
Uninstall-Silently
if (-not (Test-Path (Join-Path $work "$Identifier.sqlite"))) { Fail 'the silent uninstall removed the database' }
if (-not (Test-Path (Join-Path $work '.workfolder'))) { Fail 'the silent uninstall removed the marker' }
if (Test-Path (Join-Path $work 'logs')) { Fail 'the silent uninstall left the logs' }
if (Test-Path (Join-Path $work 'browser')) { Fail 'the silent uninstall left the browser profiles' }

# Delete everything.
Install-App
Run-App
$data = Start-Process -FilePath $program -ArgumentList '--uninstall-data=delete' -Wait -PassThru
if ($data.ExitCode -ne 0) { Fail "--uninstall-data=delete exited with $($data.ExitCode)" }
if (Test-Path $work) { Fail "the work folder is still there: $work" }
Uninstall-Silently
Write-Output 'install test passed'
