@echo off
rem Builds, tests and writes the release zip dist\EnshroudedLiveMap-<version>.zip:
rem Shroudtopia mod, standalone loader, map assets including HD, instructions and license notices.
cd /d "%~dp0"
call "%~dp0build.cmd" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "$v = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'VERSION (\d+\.\d+\.\d+)').Groups[1].Value;" ^
  "$stage = 'dist\stage'; Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $stage | Out-Null;" ^
  "New-Item -ItemType Directory \"$stage\mods\" | Out-Null; Copy-Item build\livemap.dll \"$stage\mods\";" ^
  "Copy-Item build\iphlpapi.dll, build\EnshroudedWithLivemap.exe, package\README.txt, package\README-HD.txt, LICENSE, THIRD_PARTY_NOTICES.md $stage;" ^
  "New-Item -ItemType Directory \"$stage\livemap\" | Out-Null;" ^
  "Copy-Item web\index.html, web\maps.json, web\pois.json, web\pois.json.gz, web\loot.json, web\loot.json.gz, web\weather.json, web\weather.json.gz, web\shroud.png \"$stage\livemap\";" ^
  "Copy-Item web\icons, web\tiles, web\tiles-hd, web\ui, web\fonts \"$stage\livemap\" -Recurse;" ^
  "$zip = \"dist\EnshroudedLiveMap-$v.zip\"; Remove-Item $zip -ErrorAction SilentlyContinue;" ^
  "& \"$env:SystemRoot\System32\tar.exe\" -a -c -f $zip -C $stage iphlpapi.dll EnshroudedWithLivemap.exe mods livemap README.txt README-HD.txt LICENSE THIRD_PARTY_NOTICES.md; if ($LASTEXITCODE) { throw 'tar failed' };" ^
  "Remove-Item $stage -Recurse -Force;" ^
  "Write-Output \"$zip\"; $stream = [IO.File]::OpenRead('build\iphlpapi.dll'); $sha = [Security.Cryptography.SHA256]::Create(); try { 'iphlpapi.dll sha256 ' + [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() } finally { $stream.Dispose(); $sha.Dispose() }" || exit /b 1
