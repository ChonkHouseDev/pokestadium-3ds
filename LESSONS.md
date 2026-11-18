# Lessons

Things that cost time. Each one names the file where the fix lives.

## The installable title (.cia)

- **`MaxCpu: 0` in the `.rsf`.** With `0x9E` the title's share of core 1 is fixed at 30% and the sound
  ends up on the main core. With `0` the game sets its own share. (`platform/3ds/cia/pokestadium.rsf`)
- **`Logo: Homebrew`, not `None`.** With `None` the console refuses to open the title and reports that
  the SD card was removed. Tried twice. (same file)
- **Put the ROM in the RomFS.** The `.cia` carries the user's ROM, so nothing is copied to the SD, and the
  BPS patch can copy those bytes from the user's own ROM instead of carrying them.

## Toolchain

- **GCC 14** turns four old-C habits into errors, and the decomp has all of them. The block is compiled with
  `-Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion
  -Wno-error=incompatible-pointer-types`. (`tools/port/build_blob_3ds.sh`)
- **Declare the FPU in hand-written assembly.** `tramp.s` saves the VFP registers and needs `.fpu vfpv2`.
- **Float to unsigned is not the same on ARM.** MIPS wraps a negative float converted to `u8`/`u16`/`u32`;
  ARM saturates to 0. (`tools/port/fix_vcvt.py`)
- **The game depends on variable layout.** Zero-initialized variables are initial data, not `.bss`, and
  some original code writes past the end of an array counting on what the N64 had next.
  `PS_BOUNDS=1` logs every out-of-bounds index. (`tools/port/build_blob_3ds.sh`)

## The patch

- **Make it in delta mode** (`flips --create --bps-delta`), so the ROM inside the `.cia` becomes copies
  from the source ROM. The public patch for a `.cia` with a 32 MB ROM inside is under 3 MB.
- **Check every patch before publishing.** `check_patch.py` fails if any run of 24 bytes written by the
  patch also exists in the ROM. `verify_patch.py` applies it the way a browser patcher does and compares.

## The decomp is not the game

- **`NON_MATCHING` C can be wrong.** Several C versions had real bugs (a background, list clipping,
  voices, team selection). `tools/port/verif/` finds them by running the original and the C in a MIPS
  emulator and comparing results.
- **One freeze was an uninitialized index** in a `NON_MATCHING` function. GCC's `-Wmaybe-uninitialized`
  points at it; the block is normally compiled with `-w`, so run that warning once over all of `src/`.

## Timing

- **Instant hardware breaks code that assumed slow hardware.** A thread posted its "read done" message
  without waiting; on the N64 every read takes time and the queue is never full, here it was, and the
  message was lost. (`src/rsp.c`, marked `PORT`)
- **Limit catch-up.** When the console falls behind, recover at most 3 frames. With 12 the game ran
  visibly fast after every hitch. (`port/os/os_events.c`)
- **Do not discard audio that is already mixed.** Fill what the output is missing to keep a reserve, as
  the N64 does with `AI_LEN`. (`src/3D140.c`)

## Graphics on the PICA200

- **One constant per TEV stage.** A combiner like `(SHADE-ENV)*PRIM` needs two constants in one stage and
  came out white. (`platform/3ds/patch_citro3d.py`)
- **More GPU batches can be slower.** Drawing triangles natively with one batch per bone matrix doubled
  the GPU submissions and was slower on the console (3.4 s against 2.6 s of drawing per 60 frames).
  (`platform/3ds/build_3ds.sh`)
- **The host needs its own stack.** The game's fiber stacks are 64 KB; the interpreter and citro3d do not
  fit. Every trampoline switches stacks. (`platform/3ds/host/tramp.s`)

## Memory

- **Linear memory is the limit on an Old 3DS.** Keeping all mini sprites and two saved layers for the
  bottom screen (1.1 MB) left too little for the retained models, which then failed to allocate.
  The board now keeps one layer and only the minis of the current battle. (`platform/3ds/host/gb_saves.inc`)

## Testing

- **The emulator does not have your bugs.** A debug leftover wrote a 14 MB frame dump to the SD on each of
  the first touches of the bottom screen, freezing the game about 2.8 s each time. The test emulator had
  no touch screen, so it never showed up there. The console's `log.txt` found it.
- **Make the game write a summary.** Counters for stalls, audio cuts and draw cost in `log.txt` turned
  "it feels slow" into numbers that could be compared between builds.
