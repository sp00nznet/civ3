# One-click setup for the Civilization III: Conquests static recompilation.
# Run it by double-clicking Setup.cmd in the repo folder; the README's "Step by
# step" is the same thing by hand, command for command.
#
# It copies your install into game\, builds the function catalog, lifts the
# whole executable to C, compiles the 32-bit host, and leaves a shortcut
# (Civ3 Recomp.cmd) that runs it. Each step is skipped when its output already
# exists (-Force redoes them). Everything it does goes to setup.log.
param([switch]$Force, [string]$Game = "")

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
# The toolkit sits beside this repo as tools\ (the pcrecomp house layout);
# PCRECOMP points somewhere else, the same knob run_lift.py and CMake read.
$Toolkit = if ($env:PCRECOMP) { (Resolve-Path $env:PCRECOMP).Path } else { Join-Path (Split-Path -Parent $Root) 'tools' }
$T = Join-Path $Toolkit 'tools'
$Log = Join-Path $Root 'setup.log'
Set-Location $Root
function Log($t) { Add-Content -Path $Log -Value $t -Encoding UTF8 }
Log "==== setup $(Get-Date -Format s)"

function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c; Log $t }
function Step($n, $t) { Write-Host ""; Say "[$n/6] $t" 'Cyan' }
function Fail($t) {
  Say "" ; Say "Setup stopped: $t" 'Red'
  Say "The details are in $Log. Fix that and run Setup.cmd again; finished steps are skipped." 'Yellow'
  Read-Host "Press Enter to close" | Out-Null; exit 1
}
function Ask($q) { $a = Read-Host "$q [Y/n]"; return -not ($a -match '^[nN]') }   # Enter = yes
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
# Run a program with its output in the log. Windows PowerShell turns a native
# program's stderr into errors, so 'Stop' is off while it runs.
function Exec([string[]]$cmd) {
  $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $rest = @($cmd | Select-Object -Skip 1)   # @(): a one-element slice would splat as characters
  & $cmd[0] @rest 2>&1 | ForEach-Object { Log "$_" }
  $code = $LASTEXITCODE
  $ErrorActionPreference = $old
  return $code
}
function Run($what, [string[]]$cmd) {
  Say "  $what..."
  $code = Exec $cmd
  if ($code -ne 0) { Fail "$what failed (exit code $code)." }
}

Clear-Host
Say "Civilization III: Conquests - static recompilation setup" 'White'
Say "You need your installed copy of Civilization III Complete (Steam) and about 4 GB free."
Say "The long steps (catalog, lift, compile) take about 45 minutes, once."

# ---------------------------------------------------------------- tools
Step 1 "Checking the tools the pipeline needs"
# A Python that answers "Python 3.x". The Store's placeholder "python" (the
# one that opens the Store) answers nothing, so asking is the reliable test.
function Find-Python {
  foreach ($c in @(@('py', '-3'), @('python'), @('python3'))) {
    if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $rest = @($c | Select-Object -Skip 1)
    $v = (& $c[0] @rest --version 2>&1 | Out-String).Trim()
    $ErrorActionPreference = $old
    if ($v -match '^Python 3\.(\d+)' -and [int]$Matches[1] -ge 10) { return ,$c }
  }
  return $null
}
$pyargs = Find-Python
if (-not $pyargs) {
  Say "  Python 3.10 or newer is not installed."
  if (-not (Ask "  Install Python 3.12 now (winget, for your user only, about 30 MB)?")) { Fail "Python 3 is required." }
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail "winget is missing. Install 'App Installer' from the Microsoft Store, or install Python yourself."
  }
  Exec @('winget', 'install', '-e', '--id', 'Python.Python.3.12', '--scope', 'user', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
  Refresh-Path
  $pyargs = Find-Python
  if (-not $pyargs) { Fail "Python installed, but Windows has not picked it up yet: close this window and run Setup.cmd again." }
}
Say "  Python: $($pyargs -join ' ')"

if ((Exec ($pyargs + @('-c', 'import pefile, capstone'))) -ne 0) {
  Say "  The Python packages pefile and capstone are missing (about 20 MB)."
  if (-not (Ask "  Install them now (pip, for your user only)?")) { Fail "pefile and capstone are required." }
  Run "Installing pefile and capstone" ($pyargs + @('-m', 'pip', 'install', '--user', 'pefile', 'capstone'))
}

if (-not (Test-Path (Join-Path $T 'lift\lift32.py'))) {
  Say "  The pcrecomp toolkit is not beside this folder ($Toolkit)."
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "git is needed to fetch pcrecomp. Install Git for Windows, then run Setup.cmd again." }
  if (-not (Ask "  Download it now (git, about 20 MB)?")) { Fail "pcrecomp is required at $Toolkit." }
  Run "Cloning pcrecomp" @('git', 'clone', '--depth', '1', 'https://github.com/sp00nznet/pcrecomp', $Toolkit)
}
if (-not (Test-Path (Join-Path $Toolkit 'runtime\native32\native32.c'))) {
  Fail "your pcrecomp at $Toolkit predates runtime\native32. Update it: git -C `"$Toolkit`" pull"
}
Say "  pcrecomp: $Toolkit"

# The compiler. Visual Studio (any edition, or the free Build Tools) with the
# C++ workload is a multi-GB install with its own installer, so it is not
# installed from here.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = if (Test-Path $vswhere) { (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) } else { $null }
if (-not $vs) {
  Fail "Visual Studio 2022 with 'Desktop development with C++' is needed to compile. Install the free Build Tools from https://visualstudio.microsoft.com/downloads/ (Tools for Visual Studio), then run Setup.cmd again."
}
Say "  Visual Studio: $vs"
foreach ($tool in @(@('cmake', 'Kitware.CMake', 'CMake, about 30 MB'), @('ninja', 'Ninja-build.Ninja', 'Ninja, about 1 MB'))) {
  if (Get-Command $tool[0] -ErrorAction SilentlyContinue) { continue }
  # Visual Studio ships both; its copies are on PATH inside build.cmd's vcvarsall.
  if ((Test-Path (Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$($tool[0]).exe")) -or
      (Test-Path (Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\$($tool[0]).exe"))) { continue }
  Say "  $($tool[0]) is not installed."
  if (-not (Ask "  Install $($tool[2]) now (winget)?")) { Fail "$($tool[0]) is required to build." }
  Exec @('winget', 'install', '-e', '--id', $tool[1], '--accept-package-agreements', '--accept-source-agreements') | Out-Null
  Refresh-Path
}

# ---------------------------------------------------------------- the game
Step 2 "Copying your copy of Civilization III Complete into game\"
function Is-Install([string]$d) { return ($d -and (Test-Path (Join-Path $d 'Conquests\Civ3Conquests.exe'))) }
function Find-Steam {
  $roots = @()
  foreach ($k in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
    try { $roots += (Get-ItemProperty $k -ErrorAction Stop).SteamPath } catch {}
    try { $roots += (Get-ItemProperty $k -ErrorAction Stop).InstallPath } catch {}
  }
  foreach ($r in ($roots | Where-Object { $_ } | Select-Object -Unique)) {
    $libs = @($r)
    $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
      $libs += (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' })
    }
    foreach ($l in $libs) {
      $d = Join-Path $l "steamapps\common\Sid Meier's Civilization III Complete"
      if (Is-Install $d) { return $d }
    }
  }
  return ""
}

if ((Test-Path 'game\Conquests\Civ3Conquests.exe') -and -not $Force) {
  Say "  Already in game\ (skipping)."
} else {
  $Game = $Game.Trim('"', ' ')
  if (-not (Is-Install $Game)) {
    $Game = Find-Steam
    if ($Game) { Say "  Found it in your Steam library: $Game" }
  }
  while (-not (Is-Install $Game)) {
    $Game = (Read-Host "  Paste the folder Civilization III Complete is installed in (the one with the Conquests folder)").Trim('"', ' ')
    if (-not (Is-Install $Game)) { Say "  No Conquests\Civ3Conquests.exe in that folder." 'Yellow' }
  }
  Say "  Copying $Game (about 1.8 GB)..."
  $code = Exec @('robocopy', $Game, (Join-Path $Root 'game'), '/E', '/NFL', '/NDL', '/NJH', '/NP')
  if ($code -ge 8) { Fail "copying the game failed (robocopy exit code $code)." }   # robocopy: <8 is success
}
New-Item -ItemType Directory -Force work | Out-Null
if (-not (Test-Path 'work\Civ3Conquests.exe') -or $Force) { Copy-Item 'game\Conquests\Civ3Conquests.exe' 'work\' -Force }

# ---------------------------------------------------------------- analyse
Step 3 "Finding the C++ vtables (seconds)"
if ((Test-Path 'work\vtable_seeds.json') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Scanning for vtables" ($pyargs + @("$T\cpp\vtable_scan.py", 'work\Civ3Conquests.exe', '--seeds', 'work\vtable_seeds.json')) }

# ---------------------------------------------------------------- catalog
Step 4 "Finding every function (about 10 minutes)"
if ((Test-Path 'work\functions.json') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Disassembling" ($pyargs + @("$T\disasm\disasm32.py", 'work\Civ3Conquests.exe', '-o', 'work\functions.json', '--seed-functions', 'work\vtable_seeds.json')) }

# ---------------------------------------------------------------- lift
Step 5 "Translating the game to C (about 4 minutes)"
if ((Test-Path 'src\recomp\gen\recomp_dispatch.c') -and -not $Force) { Say "  Already done (skipping)." }
else {
  $env:PCRECOMP = $Toolkit
  Run "Lifting" ($pyargs + @('run_lift.py', '--all'))
}

# ---------------------------------------------------------------- build
Step 6 "Compiling (about 30 minutes)"
if ((Test-Path 'build\civ3.exe') -and -not $Force) { Say "  Already built (skipping)." }
else {
  $env:CMAKE_ARGS = "-DPCRECOMP=$($Toolkit -replace '\\', '/')"
  Run "Building" @('cmd', '/c', (Join-Path $Root 'build.cmd'))
}

$sc = Join-Path $Root 'Civ3 Recomp.cmd'
Set-Content -Path $sc -Encoding ASCII -Value "@echo off`r`ncd /d `"%~dp0`"`r`nbuild\civ3.exe --run %*`r`n"
Write-Host ""
Say "Done. Double-click 'Civ3 Recomp.cmd' to run the recompiled game." 'Green'
Say "In the main menu, click an item once to select it and again to start it, as in the original."
Read-Host "Press Enter to close" | Out-Null
