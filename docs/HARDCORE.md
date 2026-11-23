# Guardian hardcore native backend

Live Map 1.14.0 supplies the dedicated-server engine backend for Guardian 3.2.0.
Install both Shroudtopia modules and restart the server. Set Guardian's hardcore to true and keep Live Map Enabled=1 and Deaths=1. No client modification or EMF runtime is required.

The native program is registered at world setup. Exact PE timestamp 6aeb143e, image size 0x1dde000, unique code patterns and matching RVAs are mandatory. All required functions are resolved before installing either hook. A mismatch leaves regular Live Map available and refuses hardcore.

Only deaths matched by account hash trigger Guardian's independently checked SteamID/session identity. Nearest-position and display-name matches never authorize resets. A new live death must pass through an observed dead/not-ready state before the alive/ready state; waiting for a manual respawn does not time out.

After Guardian's background worker persists the reference backup and pending marker, the ECS program removes owned inventory stacks using one native transaction (rollback on refusal). Shared base chests are excluded. After the simulation completes and native jobs have been joined, it resets level/XP and skill ranks, destroys verified player-owned skill impacts, then queues native Teleport to the unique PlayerSpawnPoint of type StartLocation. Knowledge is never cleared. Completion requires matching session, level 1, zero XP/pending XP, empty owned inventory, no unlocked ranks, a changed teleport counter and position within 25 metres of the start point. Guardian persists the resulting reset on the next save.

Unverified layouts, missing/ambiguous spawn points and native failures do not authorize a successful reset. Backups and pending markers remain for recovery; no kick/disconnect is requested. A pending reset is reapplied on reconnect after a crash or interrupted session. Deaths remain subject to the existing event deduplication window.

The ABI contracts and inventory/teleport approach derive from [EMF engine research](https://github.com/matenm/emf-enshrouded), Copyright (c) 2026 EMF contributors, MIT. License: dependencies/emf/LICENSE.txt. This is an adapted implementation, not an EMF binary dependency.
