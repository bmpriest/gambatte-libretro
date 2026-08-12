# Dual-instance frontend contract

`NETPLAY_DUAL_INSTANCE=1` builds expose two linked Gambatte instances through
one libretro core. The ordinary libretro ABI remains the execution interface;
the small ABI in `libgambatte/libretro/gambatte_dual.h` covers the operations
that standard libretro cannot express.

Build with both features enabled:

```sh
make -f Makefile.libretro HAVE_NETWORK=1 NETPLAY_DUAL_INSTANCE=1
```

The core identifies itself as `Gambatte Dual v0.5.0-netdual7`. A frontend must
resolve `retro_dual_get_abi_version`, require ABI version 1, and inspect the
capability mask before using other extension entry points. The symbols are not
present in ordinary builds.

## Loading two cartridges

The core registers the `gblc` libretro subsystem with two required in-memory
content slots. Call:

```c
struct retro_game_info games[2] = { console_a, console_b };
retro_load_game_special(GAMBATTE_DUAL_SUBSYSTEM_ID, games, 2);
```

Slot zero is console A and input port zero. Slot one is console B and input port
one. `retro_load_game` remains supported as a same-ROM shorthand and loads its
single content object into both consoles.

## Presentation and persistence

Call `retro_dual_set_visible_console(A|B)` before loading content. The selected
console supplies the 160x144 video, audio, standard `retro_get_memory_*`
results, and memory map. `BOTH` retains the side-by-side diagnostic display and
uses console A for standard memory and audio. Presentation cannot change while
content is loaded because doing so would invalidate frontend geometry and save
ownership.

Use `retro_dual_get_memory_data/size(console, id)` to address each console's
SRAM, RTC, or system RAM explicitly. Netplay should initialize both consoles
through these pointers after loading, persist only the locally authoritative
console, and treat the mirrored console's SRAM and RTC as session-local.

## Inputs and reset

Libretro input port zero drives console A and port one drives console B. A
frontend should provide frame-tagged local and remote input to both ports before
each `retro_run` call.

`retro_reset` resets the selected visible console. For an explicit replicated
transaction, `retro_dual_reset_console(A|B)` identifies the logical console.
Reset is accepted only between frames while the local serial coordinator is
idle.

## Paired checkpoints

Ordinary `retro_serialize_size` remains zero so menus and generic frontends
cannot create incomplete one-console states. Netplay uses:

- `retro_dual_is_checkpoint_safe`
- `retro_dual_serialize_size`
- `retro_dual_serialize`
- `retro_dual_unserialize`

A checkpoint contains console A followed by console B in a versioned envelope.
It deliberately excludes presentation selection, input caches, diagnostics,
and an in-flight serial transaction. Serialization is therefore allowed only
between `retro_run` calls when the serial bus is idle. This makes the bytes
independent of which console a device presents and suitable for external state
hashing. Loading is transactional: if either console rejects its state, the
core restores the pre-load pair.

The frontend must treat the paired blob as opaque and require the same core
build and dual ABI on both devices. ABI versioning does not promise checkpoint
compatibility between different core commits.
