Enshrouded Live Map: included HD map style
============================================

A second, colourful map style for the Live Map: the HD map picture made sharp with the game's
own 1 m relief (cliffs, banks, paths and ruins come from the game data), as tiles, so a
browser loads only what is on screen.

INSTALL
The main package includes both map styles. Copy the livemap folder beside
enshrouded_server.exe or enshrouded.exe, following README.txt for your loading method.
Keep both livemap/tiles and livemap/tiles-hd together with livemap/maps.json.
Reload the map page and switch between "Game map" and "HD" at the bottom left.
Replacing map tiles alone does not require a server restart.

UNINSTALL
To hide HD, remove its entry from the maps array in livemap/maps.json before deleting
livemap/tiles-hd. To use the original map without deleting files, select "Game map".

CREDITS
HD map picture supplied by Nick, as shipped in the Enshrouded Minimap fork by Aerox912
(Sparkweave Minimap, Nexus Mods #143), based on elxokker's Enshrouded Minimap. Enshrouded
and its world: Keen Games. Relief, alignment and tiles: tools/enhance_map.py of the Live Map.
