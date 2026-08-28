# Bloodborne readback optimizations (experimental)

This branch ports the readback optimization path from upstream shadPS4 pull request
[#3404](https://github.com/shadps4-emu/shadPS4/pull/3404) onto the newer video architecture used
by `DarkIzuku/shadp2p`.

The upstream pull request is unfinished and explicitly warns about possible freezes and rendering
bugs. This branch is therefore intended for controlled Bloodborne testing, not as a replacement for
the normal build yet.

## Scope

Included:

- CPU-fence detection for graphics and compute command buffers.
- Deferred GPU-written range protection until a likely CPU-visible fence.
- Preemptive GPU-to-CPU downloads for pages that have repeatedly required readback.
- The upstream PM4 address conversion cleanup required by the fence detector.
- A larger 128 MiB download staging buffer.
- A new `Optimized (Experimental)` readback mode.

Not included:

- The older upstream `DispatchDirect` to `DispatchIndirect` rewind patch. This fork already has a
  newer indirect-patch implementation, which is preserved.
- The upstream scheduler wait change. The equivalent fix is already present in this fork.
- The upstream configuration implementation, page-table simplification, unrelated assertion and
  buffer-cache refactors. The fork uses a newer settings and cache architecture, so only the
  readback-specific behavior was ported.
- Any FaceGen file removal, game-address hook, Bloodborne-specific memory address, or asset mod.
- Any multiplayer, Blood Messages, Wandering Ghosts, Chair Messenger, or seamless co-op change.

## Modes

- `Disabled`: unchanged; no GPU readbacks.
- `Relaxed`: keeps the current low-overhead behavior and now uses fence deferral plus preemptive
  downloads. **Try this first for Bloodborne.**
- `Precise`: unchanged accurate/heavier fallback. It deliberately bypasses the experimental
  optimizations.
- `Optimized (Experimental)`: enables fence deferral, preemptive downloads, and read protection.
  Try this only if `Relaxed` still shows vertex explosions.

The new value is appended after the existing three values so old per-game configuration files keep
their original meaning.

## Bloodborne test procedure

1. Do not install the Vertex Explosion Fix mod; it removes FaceGen data.
2. Keep a backup of the currently working emulator folder.
3. Extract the Windows artifact into a separate folder, or replace only the emulator executable used
   by BBLauncher after backing it up.
4. Open Bloodborne's per-game settings and select `Readbacks Mode: Relaxed`.
5. Load the same save and visit the same areas used for comparison. Check the customized face,
   armor changes, death/respawn, Hunter's Dream, and an area transition with many assets.
6. Record whether vertex explosions occur, whether area/asset loads stutter, and whether the game
   freezes or crashes.
7. If vertex explosions remain, repeat with `Optimized (Experimental)`. If it freezes or regresses,
   return to `Precise` or to the backed-up build.
8. Finally, verify co-op, Blood Messages, Wandering Ghosts, and Chair Messenger with the same server
   setup used by the normal build.

Successful compilation and automated tests do not prove the visual or frame-time improvement. The
final decision requires an A/B test on the same machine, game files, save, area route, and graphics
settings.
