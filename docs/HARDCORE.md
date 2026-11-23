# Optional Guardian integration

Guardian 4.1.0 includes all native hardcore engine hooks, death detection, equipment-to-tombstone transfer, respawn/reset and teleport logic in guardian.dll. It works without Live Map.

Live Map 2.0.0 only forwards exact manager/death observations through GuardianNativeBackendVersion (ABI 2), GuardianNativeManager and GuardianNativeDeath when those exports are available. Without Guardian, the bridge is a no-op; ordinary map functionality remains available. No native hardcore simulation, program-registration or inventory hooks are installed by Live Map.

Update or disable older Live Map 1.14.x modules when using the integrated Guardian backend: those older modules installed the same engine hooks and cannot be combined safely. Preserve bootDelay=0 in shroudtopia.json and merge configurations without disabling other installed modules.

All native backend source, ABI tests and third-party notices now belong to the Guardian project. See its docs/HARDCORE.md for equipment transfer, grave capacity and interrupted-reset behavior.
