# Architecture

The port is two programs in one 3DS process: the game, compiled almost unchanged as a **big-endian** ARM
block, and a little-endian **host** that gives it a fake N64 to run on.

```
 3DS process (ARM11)
 +-------------------------------------------+-----------------------------------------------+
 | HOST  little-endian  (platform/3ds/host)  | GAME BLOCK  big-endian BE8, at 0x0A000000     |
 |                                           |                                               |
 |  loads the block, sets page permissions   |  src/         the decomp, in C                |
 |  Fast3D interpreter (from MK64 3DS)       |  port/os/     libultra threads, messages, VI  |
 |  citro3d backend    (from MK64 3DS)       |  port/        assets, fragments, RSP HLE,     |
 |  audio worker on core 1, NDSP output      |               MIPS stub interpreter           |
 |  pad, touch, saves, ROM reads from the SD |  platform/3ds/blob/  fibers, libc, the        |
 |  Game Boy cartridges, bottom screen       |               game side of the service table  |
 +---------------------+---------------------+-----------------------+-----------------------+
                       |   tramp.s: "setend be" / "setend le" on     |
                       +------------- every call across -------------+
```

## Where each folder comes from

| Folder | Origin | Role |
|---|---|---|
| `src/`, `include/`, `lib/`, `yamls/`, `linker_scripts/` | pret/pokestadium, modified here (changes are marked `PORT`) | The game |
| `port/os/` | This port | libultra's OS on fibers: threads, message queues, VI retrace, timers |
| `port/` | This port | Asset loading, fragments, replacements for functions still in assembly, the bridge to the RSP HLE |
| `platform/3ds/blob/` | This port | Game side of the border: service table, ARM context switch, minimal libc, linker script |
| `platform/3ds/host/` | This port | The host: everything the 3DS does for the game |
| `platform/3ds/patch_*.py` | This port | Patches applied at build time to the Mario Kart 64 3DS interpreter, backend and shader |
| `platform/3ds/cia/` | This port | Title settings, icon and banner for the installable `.cia` |
| `platform/3ds/test/` | This port | The same block under `qemu-armeb` with a minimal host, no console needed |
| `third_party/mupen64plus-rsp-hle/` | mupen64plus (GPLv2) | Audio (naudio) and JPEG microcode, emulated at high level |
| `tools/port/` | This port | Build scripts and generators |
| `tools/pc/` | This port | Windows script that builds the `.cia` |
| `tools/tracer/`, `tools/viewer/`, `port/host/` | This port | Test benches on PC |
| `patch/` | This port | The `.cia` and BPS patch kit |

The Mario Kart 64 3DS code is not copied here. `build_3ds.sh` reads its sources and its build, patches
copies of the interpreter and the backend in the output folder, and links against its libraries.

## The big-endian block

N64 code assumes big-endian memory everywhere: structs read from the ROM, display lists, audio banks,
save data. Instead of fixing byte order format by format, the game is compiled for ARM in big-endian mode
(BE8: instructions stay little-endian, data is big-endian) and the CPU is switched with `setend`.

- `tools/port/build_blob_3ds.sh` compiles `src/` and parts of ultralib with
  `-mbig-endian -march=armv6k+fp -mfloat-abi=hard`, assembles the data the decomp still has as assembly
  (`asm/us/data` from your own extracted decomp) and links everything with `ld -EB --be8` at the fixed
  address `0x0A000000` (`platform/3ds/blob/blob.ld`, 20 MB reserved).
- The host embeds the result (`blob.bin`), maps code pages read-execute and the rest read-write, and jumps
  in through `ps_call_blob` (`platform/3ds/host/tramp.s`).
- The game calls the host through a table of services (`PsBlobApi` in `platform/3ds/blob/blob_host.c`):
  print, clock, sleep, pump, pad, graphics task, ROM read, prepared asset, audio, save, Game Boy cartridge.
  Each entry is a trampoline that switches to little-endian, moves to the host's own stack, calls the C++
  function and switches back.

Rules for crossing the border (from `blob_host.c`):

- Only 32-bit arguments and return values in registers. No `u64`, no structs by value.
- Data the host writes into shared memory (pads, sizes) is written big-endian.
- The host builds the service table with the pointers already in big-endian.
- The exception handler also enters through assembly that forces little-endian first.

Because that block contains data assembled from your ROM's decomp, the `.elf` and the `.cia` cannot be
shared. That is why releases are BPS patches against the user's own ROM (see `patch/`).

### What the build fixes for you

- **Static symbols.** Game data stores N64 addresses (`0x8XXXXXXX`) that must be translated to native
  ones. Statics are not in the decomp's map, so the block is linked twice: once with an empty table of
  the same size, then with the real addresses (`tools/port/gen_static_syms.py`).
- **Unreferenced statics** are kept with `__attribute__((used))`, because data points at them by address.
- **Float to unsigned.** MIPS wraps negative values, ARM's `VCVT.U32` saturates to 0.
  `tools/port/fix_vcvt.py` rewrites those instructions in the linked block.
- **Fragment data.** On the N64 each load of an overlay brings its initial data back from the ROM. The
  linker script groups each fragment's `.data` and `.bss` so they can be restored on every "load".

## The fake N64

- **Threads** (`port/os/os_thread.c`): libultra threads and messages on a cooperative scheduler. The N64
  only switches thread when one blocks, yields or is woken by a higher priority event; fibers in one
  system thread reproduce that. On the 3DS the context switch is `platform/3ds/blob/fiber_arm.c`.
- **Hardware events** (`port/os/os_events.c`): clock, vertical retrace, timers and deferred messages
  (DMA, RSP). The game advances one logic frame every 1/30 s and skips drawing when the console is late.
- **Fragments** (`port/ps_frag.c`): the 77 code overlays are linked statically; "loading" one returns its
  native entry point. Data fragments (Pokémon models, animations) are loaded and relocated as on the N64.
- **MIPS stub interpreter** (`port/ps_mips_stub.c`): each data fragment still has a few MIPS instructions
  that return a pointer to its data, and each stadium has a small `main`. The game calls them as
  functions; jumping into that memory traps, and the port interprets the instructions.
- **Assets**: the game asks for ROM ranges. The host reads them from the user's ROM
  (`romfs:/baserom.z64` inside the `.cia`, or `sdmc:/3ds/pokestadium/baserom.z64`) and keeps a cache of
  prepared assets.
- **Functions still in assembly**: Yay0 and startup are rewritten in `port/ps_core.c`; the GSM voice
  decoder is translated to C instruction by instruction at build time (`tools/port/m2c.py`).

## Graphics

1. The game builds its display lists in big-endian memory.
2. `gfx_convert.cpp` converts them to native display lists for the Fast3D interpreter.
3. The interpreter is the Mario Kart 64 3DS one, recompiled for F3DEX2 and patched
   (`patch_interp_*.py`); the backend is its citro3d backend, patched (`patch_citro3d.py`,
   `patch_atlas_backend.py`, `patch_shader.py`).

What makes it run on an Old 3DS:

- **Model cache** (`model_cache.cpp`, "F1"): the converted result of each boned model batch is kept and
  invalidated only when the game reports that memory changed.
- **Resident models** (`ps_resident*.inc`, "F2"): vertices stay in GPU-readable memory and the PICA200
  does transform, lighting, texture coordinates and fog. Per frame the CPU only computes bone matrices.
- **HUD layer** (`ps_hud_interp.inc`): HUD pieces that did not change are copied from a saved layer
  instead of being executed again.
- **Software RDP** (`soft_rdp.cpp`): what the game draws off screen (render to texture for menus) is
  rasterized straight into the game's memory.

## Audio

The game's audio microcode (naudio) and the JPEG decoder run in
`third_party/mupen64plus-rsp-hle`, compiled big-endian inside the block (`port/ps_rsp_hle.c`). The host
runs audio tasks on a worker thread on core 1 and plays the result through NDSP.

## Controls, saves and the bottom screen

All in the host: `host_3ds.cpp` reads the pad and the touch screen and stores the game's flash save on
the SD. `gb_saves.inc` emulates the Game Boy cartridges of the Transfer Pak from `.sav` files or from the
Virtual Console titles, and draws the bottom screen. `gb_sel.inc` is the touch team selection, `gb_roam.inc`
the free screen outside battle. The bottom-screen graphics are generated by `tools/port/gen_ui3ds.py`
and do not come from the game. The touch battle board reads the battle state from the game's memory and
answers by pressing the buttons the original menu expects.

## Test benches

- `tools/port/build_headless.sh`: the game as a native 32-bit Linux program without a window.
- `tools/tracer/`: the game as MIPS big-endian under `qemu-mips`, with plugins to trace and profile.
- `platform/3ds/test/build_test.sh`: the same ARM BE8 block as the console, under `qemu-armeb`.
- `tools/viewer/`: shows the frames those benches dump.
- `tools/port/verif/`: runs original functions and their C versions in a MIPS emulator and compares them.
