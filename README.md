# Koopky CQB Reaper Compatibility

Keeps a Clear, Garrison, or Take cover order in charge of movement when REAPER Improved AI is also loaded. Requires Koopky CQB and REAPER Improved AI.

Use this addon instead of Koopky CQB CRX Compatibility. The two AI mods are not meant to be loaded together, and each compatibility patch only wraps one of them.

## What it changes

While a Clear, a Garrison, or a Take cover is running, Reaper's combat changes are held off for that squad. They are put back when that order is finished or cancelled. A restart that keeps the same order does not put them back. During a Take cover push, or a walk back to the point, Reaper's combat move is cancelled as well. The bounding pair keeps Koopky's sprint. Reaper does not turn that run into a look at the target and a sideways step. A take cover jog still fires: that shot is Koopky's, and the rifle-up check lets it through. Once the squad is at the point and Take cover uses attack is on, that move is allowed again so normal attack can fight from there. Reaper's other changes stay off. Advance and Bound do not pause Reaper, because those orders hand the fight back to normal combat. A finished clear puts Reaper back, and a garrison or take cover that follows pauses it again when that order starts.

Held off for that order:

- The cover split that makes most of the squad stand, wait, and search cover where they already are
- Building-cover pulls, cover spacing, and the stance and run overrides on a combat move
- Reload-cover and the sidestep when a shot is blocked
- The door queue, and scheduling a door to shut behind them
- Grenade limits, medic hold-back, and smoke
- The investigation split
- Reaper's threatened recognition override. The order keeps Koopky's recognition, including Sharp combat
- The morale fire rate. It is applied again when the order ends
- A wedge formation. Single file is used for the order, and the wedge is restored afterward

An in-progress Reaper cover move is cancelled once when the order starts, so the squad is not left in that bound. Koopky still owns the route, the posts, and the doors. Soldiers still aim and fire. Cancelling the order puts the wedge and the morale fire rate back. Reaper then issues its own moves again.

These Reaper pieces stay on, because turning them off breaks boarding or there is nothing saved to put back:

- Boarding and dismount fixes, including the helicopter navmesh rebuild
- The stuck-door toggle, which only runs when a move is already stuck
- The reset that runs when a new player command is issued
- The one-time infantry target score
- Shooting a close visible enemy instead of standing and watching a threat sector

The log lines are `KKREAPER: Paused Reaper Improved AI` and `KKREAPER: Restored Reaper Improved AI`.
