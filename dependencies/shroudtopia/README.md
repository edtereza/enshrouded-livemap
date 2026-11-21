# Bundled Shroudtopia loader

Upstream: https://github.com/s0t7x/shroudtopia (MIT, Miguel Oppermann).

These x64 binaries match the official Shroudtopia 0.1.1 release published on
2026-02-16. Both SHA-256 hashes were verified against the downloaded official
archive on 2026-11-21:
https://github.com/s0t7x/shroudtopia/releases/tag/0.1.1

Official archive SHA-256: `564269ad0cef7e7d863c37b2f20c4b5e4d6fb8566fe84c7c39cf737930e495b3`.

They are pinned third-party binaries, not compiled by the Livemap workflow.

| File | SHA-256 |
| --- | --- |
| shroudtopia.dll | 53CEEE2DD51C12B7DB1371092EDDCD1118ABEAEABA6E3B482986A071AB9B1A11 |
| winmm.dll | 2519AB074597C39F9241456711370CAC907E86957833A7AE16705714CE6D7297 |

The package script verifies these hashes and includes the upstream MIT license.
The standard ZIP contains only Livemap and Shroudtopia files. Shroudtopia
requires Microsoft Visual C++ Redistributable x64, installed separately when
using that ZIP. The -full.zip also includes the unmodified app-local x64 CRT
DLLs from Visual Studio VC/Redist and their license notice. Each package has
its own SHA-256 file manifest. Windows system DLLs are never bundled.

No Flight Mod, basics mod, server executable, credentials, player saves or
existing server configuration is distributed.
