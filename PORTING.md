# Porting another N64 decompilation with this method

This is the order that worked for Pokémon Stadium. Read [ARCHITECTURE.md](ARCHITECTURE.md) first.

The idea in one line: do not convert the game to little-endian. Compile the decompiled C as a
big-endian ARM block, give it a small fake N64, and put all the 3DS work in a separate host.

## What you can reuse

| Piece | Reuse |
|---|---|
| `platform/3ds/host/tramp.s` and the service table in `platform/3ds/blob/blob_host.c` | As is. Add or remove services. |
| `platform/3ds/blob/` (fibers, libc, `blob.ld`) | As is. Change the reserved size if your game is bigger. |
| `port/os/` (threads, messages, VI, timers) | As is for any libultra game. |
| `tools/port/build_blob_3ds.sh` | The flags and the two-pass link, yes. The file lists are Stadium's. |
| `tools/port/gen_static_syms.py`, `fix_vcvt.py`, `m2c.py` | As is. |
| `port/ps_rsp_hle.c` and `third_party/mupen64plus-rsp-hle/` | If your game uses the naudio audio microcode. Others need other modules of that project. |
| `platform/3ds/host/gfx_convert.cpp`, `soft_rdp.cpp` | Mostly. They know F3DEX2 and Stadium's habits. |
| `model_cache.cpp`, `ps_resident*.inc`, `ps_hud_interp.inc` | The ideas, and much of the code if your models are boned display lists. |
| `patch/` | As is: shipping a port as a BPS patch works for any game. |
| `port/ps_frag.c`, `port/ps_mips_stub.c`, `gb_*.inc`, the bottom screen | Stadium only. |

## Steps

### 1. Start from a decomp that builds

You need the decomp building from your ROM, with its map and ELF. Compile with `NON_MATCHING` so every
function has a C version. Expect some of those C versions to be wrong (see [LESSONS.md](LESSONS.md)).

### 2. Find where the game touches the hardware

List the functions that load from the ROM, start RSP tasks, read controllers and save. In Stadium the
asset loading was concentrated in a handful of functions in one file, which made it easy to redirect.
Everything else can stay as it is.

### 3. Replace libultra's OS, keep the rest

Link the decomp's own C with `port/os/` instead of libultra's threads, interrupts and devices. Keep the
parts of ultralib that are plain C (math, audio, libc); `build_blob_3ds.sh` shows which ones.

### 4. Get it running on a PC first

Before touching the console, run the game where you can debug it. This repository keeps three benches:
a native headless build, a MIPS big-endian build under `qemu-mips` with trace plugins, and the real ARM
BE8 block under `qemu-armeb`. Dump frames to files and look at them with `tools/viewer/`.

### 5. Deal with overlays

If the game loads code overlays, link them all statically and make "load" return the native entry point.
Stored N64 addresses are translated with a table generated from the decomp's map
(`tools/port/gen_frag_table.py`, `gen_static_syms.py`).

### 6. Deal with what is still assembly

Three options, all used here: rewrite it in C (`port/ps_core.c`), translate it mechanically from the
decomp's ELF (`tools/port/m2c.py`), or interpret it at run time when it is tiny (`port/ps_mips_stub.c`).

### 7. Build the big-endian block

Use the flags in `build_blob_3ds.sh`. The ones that matter:

- `-mbig-endian` with `ld -EB --be8`, and a fixed base address.
- `-fno-zero-initialized-in-bss` and `-fno-toplevel-reorder`: the game depends on where its variables are.
- `-fwrapv -fno-strict-aliasing -fno-aggressive-loop-optimizations`: the original compiler did not exploit
  undefined behaviour, GCC does.
- `-ffreestanding -fno-builtin -nostdinc` with the small libc in `platform/3ds/blob/blob_libc.c`.

### 8. Write the host

Start from `host_3ds.cpp`. What changes per game is the list of symbols the host reads from the block
(`blob_info.h`, generated in `build_3ds.sh`) and anything that looks at game state, like the battle board.

### 9. Graphics

Check the microcode first. The Mario Kart 64 3DS interpreter is built for F3DEX; Stadium uses F3DEX2, so
the same source is recompiled with `F3DEX_GBI_2`. Then measure on the console and only then optimize:
the model cache and the resident models exist because the profiler said vertex work was the cost.

### 10. Package and distribute

Build the `.cia` with the user's ROM inside its RomFS and publish a BPS delta patch from the ROM to the
`.cia`. Always run `patch/tools/check_patch.py`: it fails if the patch carries data that is in the ROM.
