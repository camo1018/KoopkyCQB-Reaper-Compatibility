# Koopky CQB Reaper Compatibility

Keeps a Clear order in charge of movement when REAPER Improved AI is also loaded. Requires Koopky CQB and REAPER Improved AI.

Use this addon instead of Koopky CQB CRX Compatibility. The two AI mods are not meant to be loaded together, and each compatibility patch only wraps one of them.

## What it changes

While a Clear is running, Reaper's combat changes are held off for that squad. They are put back when the clear ends. A handoff to Garrison turns Reaper back on. Garrison itself is left alone.

Held off for the clear:

- The cover split that makes most of the squad stand, wait, and search cover where they already are
- Building-cover pulls, cover spacing, and the stance and run overrides on a combat move
- Reload-cover and the sidestep when a shot is blocked
- The door queue, and scheduling a door to shut behind them
- Grenade limits, medic hold-back, and smoke
- The investigation split
- Reaper's threatened recognition override. A clear keeps Koopky's recognition, including Sharp combat
- The morale fire rate. It is applied again when the clear ends
- A wedge formation. Single file is used for the clear, and the wedge is restored afterward

An in-progress Reaper cover move is cancelled once when the clear starts, so the squad is not left in that bound. Koopky still owns the route, the posts, and the doors. Soldiers still aim and fire.

These Reaper pieces stay on, because turning them off breaks boarding or there is nothing saved to put back:

- Boarding and dismount fixes, including the helicopter navmesh rebuild
- The stuck-door toggle, which only runs when a move is already stuck
- The reset that runs when a new player command is issued
- The one-time infantry target score
- Shooting a close visible enemy instead of standing and watching a threat sector

The log lines are `KKREAPER: Paused Reaper Improved AI` and `KKREAPER: Restored Reaper Improved AI`.
