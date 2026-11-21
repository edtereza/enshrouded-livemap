Enshrouded Live Map 1.11.3
=========================

SHROUDTOPIA (CLIENT AND SERVER)
Install Shroudtopia: https://github.com/s0t7x/shroudtopia
Copy mods/livemap.dll into mods/ and livemap/ beside the game/server executable.
Merge "Livemap": { "active": true } into the mods object in shroudtopia.json.
Start through Shroudtopia normally. Keep livemap.ini beside the executable.
Remove Livemap's iphlpapi.dll when using this method. Use only one loading method.
Restart the game/server to disable or unload Livemap; live unloading is unsupported.

ALTERNATIVE LOCAL GAME (Windows Steam build 26430625, SVN 1287369)
Copy iphlpapi.dll, EnshroudedWithLivemap.exe and the livemap folder next to enshrouded.exe.
Keep Steam running, then start the game through EnshroudedWithLivemap.exe and enter a local
world or host a session. Open http://localhost:15680/ (or the Port from livemap.ini).
Fresh local installations bind to 127.0.0.1 and disable public-IP lookup and chat announcements.
Local-world player positioning has been checked in a running Windows game on this build.
It reads the world hosted by this game process. Joining someone else's server requires
that server's Livemap for authoritative server data.
Flame-level querying through A2S is available in dedicated-server mode.
Guardian character sheets require Guardian-generated files beside the same executable.

Server-side mod for Enshrouded dedicated servers. The server serves a live web map: players
with names and walking direction, in-game time, weather, the game's map markers, the Flame
Altars you built, and the ore
veins and chests your group finds. Players install nothing; they open a link in the browser.

Recognized Steam server builds: 25988414, 26083076 and 26430253 (Enshrouded 1.0.1.2,
SVN 1287369). On build 26430253, ore finds and live updates to discovered places are
unavailable. Existing records can still be loaded and explored area still updates.
On other builds the mod stays inactive and livemap.log says "unknown server build".

WINDOWS SERVER (ALTERNATIVE PROXY LOADING)
1. Stop the server.
2. Copy iphlpapi.dll and the livemap folder next to enshrouded_server.exe.
3. Start the server. The mod writes livemap.ini and livemap.log.
4. Open the game port for TCP too (see PORT).

LINUX / DOCKER (WINE)
1. Stop the container.
2. Copy iphlpapi.dll and the livemap folder next to enshrouded_server.exe.
3. Add iphlpapi=n,b to WINEDLLOVERRIDES
   (with Shared XP: WINEDLLOVERRIDES=powrprof=n,b;iphlpapi=n,b).
4. Publish the game port for TCP too, e.g. 15637:15637/tcp next to 15637:15637/udp.
5. Start the container.

HD MAP
The main package includes both livemap/tiles and livemap/tiles-hd.
Switch between Game map and HD at the bottom left. No separate HD download is needed.
See README-HD.txt for the HD artwork credits.

THE LINK
LANGUAGE: interface and lore texts automatically follow the browser's preferred language.
Portuguese browsers use Português (Brasil); other or unidentified languages use English.
The Markers panel includes "Bilhetes e lore": 568 placed lore pages with localized
titles and text. Click a marker to read it. Read/unread progress is not tracked.
Four markers have no localized text in the extracted resources and show "Texto indisponível".
All players appear in one list below the clock, with online players first.
Offline players show "offline" in place of deaths and coordinates.
Player names are remembered in livemap/players.json across restarts. Existing names are
imported from the Livemap log, explored areas, discovered places and death records.
Players with no remaining historical record appear after their next connection.
Click the player's colored indicator to open the character sheet.
The indicator is enabled when Guardian has generated guardian/<SteamID>/server_save.html.
Older guardian/ or guardiao-dados/ character pages are also supported.
The sheet opens in a full-screen dialog.
The SteamID is recorded on connection in each player's entry in livemap/players.json.
SteamIDs come from Guardian's session records in guardian/events.jsonl
(or older sessao records in guardiao-dados/registro.jsonl),
matched to the connected player's handle.
Existing associations from livemap/player-accounts.json are imported automatically.
Existing offline players need to connect once after this update to establish that association.

livemap.log shows the link to share: "map link: http://<your ip>:<port>/".
The chat shows it to everyone who joins, in a short form to type.

PORT
The map uses the game's query port number over TCP (the game itself only uses UDP).
Home server: set the router's port forwarding for that port to "TCP and UDP".
Rented server: often open already; otherwise ask for TCP on the game port, or set Port= in
livemap.ini to a TCP port you have.

SETTINGS (livemap.ini, restart the server after changes)
Title=                 browser tab title (empty or missing = Enshrouded Live Map)
Port=auto              the game's port number over TCP, or a fixed number
PublicUrl=             your own address, e.g. https://map.example.com (empty = public IP)
DetectPublicAddress=1  0 = never ask api.ipify.org for the public IP
AnnounceInChat=1       the map link in the chat when a player joins
RequireKey=0           1 = require a key for map access
Key=<random>           used with RequireKey=1; 3 to 64 letters, digits, - or _
Finds=1                ore veins and looted chests on the map
Bases=1                the players' Flame Altars on the map
Discovered=1           which places each player discovered and where they have been
                       (filter and fog of war in the marker panel)

With open access, anyone who can reach the map can see player positions and Flame Altars.

LOOTED CHESTS
A looted chest counts as full again after 30 minutes (not inside a Flame Altar's build zone).
Other time: add "chestRefillMinutes": 60, at the top of livemap\maps.json (0 = never).

CHECK
livemap.log shows "map link: http://..." and, after a player joins, the chat shows
"Live map: ...". Open the link from outside your network (phone without Wi-Fi).

UNINSTALL
Stop the server and delete iphlpapi.dll, the livemap folder, livemap.ini and livemap.log.

Maintained version and releases: https://github.com/edtereza/enshrouded-livemap
Original creation by oggii; original MIT copyright notice is preserved in LICENSE.
Original source: https://github.com/oggii/enshrouded-livemap
Original Nexus mod: https://www.nexusmods.com/enshrouded/mods/209

SHARED GUARDIAN ASSETS
Guardian 3.1.0 generates shared CSS, JavaScript, catalogs and WebP images in
guardian/assets/<version>/. Live Map serves these through /assets/ with the
same access-key requirement as the map. Keep this directory when copying
character sheets. The files are generated by Guardian and are not duplicated
in this Live Map package. Older standalone sheets remain supported.

UPDATE NOTICES
Livemap always checks its GitHub stable release during mod initialization, before hooks
and HTTP startup, then daily in the background. The startup check is synchronous.
New-version notices appear once in livemap.log and hourly in the console.
Informational only: no version enforcement, automatic download or installation.
Release page: https://github.com/edtereza/enshrouded-livemap/releases/latest

STARTUP CONSOLE
The [Livemap] banner shows version and mode. Startup reports the version check,
HTTP listener and map address, or a startup failure. Console notices also work with Log=0.
