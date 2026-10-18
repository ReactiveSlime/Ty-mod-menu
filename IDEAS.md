# TyMod feature backlog / ideas

Parking lot for future mod menu items, grouped by effort.

## Known addresses (this build)
- Module base is ASLR'd per launch; use **module-relative offsets**.
- Player position: `TY.exe + 0x270B78` (X, Y, Z floats, Y-up; mirror copy at `+0x10`).
- Ty hit counter (max 4, int32): `TY.exe + 0x2737CC`. Infinite Health writes `4` every frame.
- Tool used to find addresses: the in-menu **Health** exact-value scanner
  (`health.cpp`); scan for a value, get it to change, filter for drops, pick.

## Shipped
- [x] No-Clip (camera-relative flight)
- [x] No-Clip chase camera (keeps Ty centred; camera jitter fixed)
- [x] Infinite Health
- [x] Speed slider
- [x] Health exact-value scanner (fallback if a patch moves the address)

## Cheap (addresses/patterns already known or one scan away)
- [x] **Teleport bookmarks**: save/recall player spots. Store a few float[3]
      copies of `posAddr`; a menu button per slot to save/recall. Pairs perfectly
      with No-Clip.
- [x] **Infinite Lives**: the life/respawn counter is likely right next to the
      hit counter (`0x2737CC`), probably an int. Use the scanner: die once, filter
      for the value that dropped, freeze it.
- [ ] **Super Speed (on foot)**: walk-speed multiplier while No-Clip is OFF.
      Find the player speed/velocity field (near posAddr) or scale the per-frame
      move. Toggle + slider.
- [ ] **Game speed slider**: scale the engine's time delta (slow-mo / fast
      forward). Might be a float global near the update code.

## Medium
- [ ] **Unlock All Moves / Abilities**: unlock bite, tail whip, etc.
- [x] **Infinite Breath** (underwater): freeze the oxygen/breath value.
- [ ] **Freeze timers**: mission / race countdowns.
- [ ] **Super Jump / No Fall Damage**: poke vertical velocity or the fall-damage
      check.
- [x] **FOV slider**: find the projection/FOV value.

## Larger
- [x] **Collectible ESP / waypoints**: draw markers to collectibles (needs a
      list/struct scan).
- [ ] **One-hit kill enemies** / **disable enemy AI**.

## Quality of life
- [x] Configurable menu hotkey (currently INSERT).
- [x] Persist settings (enabled features, speed) to a config file next to the DLL.
- [x] Per-feature hotkeys.

## Open issue (not a feature)
- **Infinite Health still fails in Outback Safari** (2026-10-18): health now keys
  off the `IsBull` bool (`+0x27E544`) or level id 10 -> `0x273814` (MTP's
  `SetOutbackHealth`), but it doesn't hold there. The bull-ride health counter
  may live elsewhere or be a different type. Next step: use the in-menu