# Pokémon Stadium 3DS

A native Nintendo 3DS port of Pokémon Stadium (N64, US 1.0), built from the
[pret/pokestadium](https://github.com/pret/pokestadium) decompilation. It runs on an Old 3DS.

**Website: [pokemon-stadium-3ds.netlify.app](https://pokemon-stadium-3ds.netlify.app/)**: what the port is,
what you need and how to install it.

This repository contains **no game data**. You need your own copy of the game.

## Play it

You need a 3DS with custom firmware and your own Pokémon Stadium cartridge (US 1.0) dumped to a `.z64` file.

1. Download the latest patch (`pokestadium_v_<version>_cia.bps`) from
   [Releases](https://github.com/ChonkHouseDev/pokestadium-3ds/releases/latest).
2. Apply it to your `.z64` with [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/): pick your ROM,
   pick the patch, apply. Everything happens in your browser: your ROM never leaves your computer.
3. Rename the result to `pokestadium.cia` and install it with FBI. Your ROM is inside the `.cia`, so nothing
   has to be copied to the SD.

Only the US 1.0 ROM works: the patch checks it and refuses any other.

Notes:

- Sound needs `sdmc:/3ds/dspfirm.cdc` (dump it once with DSP1).
- Game Boy saves (Transfer Pak): put your `.sav` files in `sdmc:/3ds/pokestadium/saves/` and pick them on the
  bottom screen. Pokémon Red, Blue and Yellow installed from the Virtual Console are found automatically.
- Where the map had GB Tower there is an ONLINE sign. Online play is not available yet.
- The game writes its log to `sdmc:/3ds/pokestadium/log.txt`.

## Build from source

The build runs on Linux or WSL. It produces `pokestadium.3dsx` and `pokestadium.elf`.

You need:

- Your own ROM, and a checkout of [pret/pokestadium](https://github.com/pret/pokestadium) already extracted
  and built with it (the build reads its `asm/us/`, `build/pokestadium-us.map` and `build/pokestadium-us.elf`).
- devkitPro with devkitARM, libctru and citro3d.
- A system `arm-none-eabi-gcc` (the game block is compiled big-endian, BE8) and Python 3.
- A checkout of [EstebanPdN/mario-kart-64-3ds](https://github.com/EstebanPdN/mario-kart-64-3ds) with
  `third_party/SpaghettiKart` populated, and a build of it with the target `mk64-3ds-interpreter-probe`.
  This port links against its Fast3D interpreter and citro3d backend and patches them at build time.

```bash
DEVKITPRO=/opt/devkitpro \
MK64_3DS=/path/to/mario-kart-64-3ds \
MK64_3DS_BUILD=/path/to/mario-kart-64-3ds-build \
platform/3ds/build_3ds.sh /path/to/pret-pokestadium /path/to/output
```

The PC-side asset path (Torch, `.o2r`) uses libultraship at `60c9bb71` and Torch at `2d474dd`, the same
commits as the Mario Kart 64 3DS port, plus the patches in `lus-patches/` and `torch-patches/`.

## How it works

- [ARCHITECTURE.md](ARCHITECTURE.md): what each part is and how they talk to each other.
- [PORTING.md](PORTING.md): how to port another N64 decompilation with this method.
- [LESSONS.md](LESSONS.md): what cost us time, so it does not cost you the same.
- [docs/DEV_NOTES.es.md](docs/DEV_NOTES.es.md): the development notes, in Spanish. Most code comments are in Spanish too.

## Credits

- Port by **Pokethrasher**.
- [pret/pokestadium](https://github.com/pret/pokestadium): the decompilation (`src/`, `include/`, `lib/`, `yamls/`, `linker_scripts/`).
- [EstebanPdN/mario-kart-64-3ds](https://github.com/EstebanPdN/mario-kart-64-3ds): the 3DS backend this port builds on.
- [libultraship](https://github.com/Kenix3/libultraship) and [Torch](https://github.com/HarbourMasters/Torch).
- [mupen64plus-rsp-hle](https://github.com/mupen64plus/mupen64plus-rsp-hle) (`third_party/`, GPLv2): audio and JPEG.
  Binaries that include it are under GPLv2.
- `stb_image.h` by Sean Barrett. The bottom-screen letters are drawn from the Poppins font (OFL).
- makerom (3DSGuy/Project_CTR), bannertool, Floating IPS and Rom Patcher JS for packaging and patching.

## AI use disclosure

AI-assisted development tools were used in this project for implementation, debugging, refactoring, testing
and documentation.

Changes made with AI assistance are reviewed by the maintainer and tested on a real Old 3DS before they are
accepted. The repository does not include prompts, chat transcripts or other tool state: those are
development artifacts, not project source.

Bug reports and contributions are judged on their technical quality, whether or not AI tools were used.

## Licence

The port's own code, tooling and documentation are under CC BY-NC-SA 4.0: you may copy, fork, modify and
share them with credit, not commercially, and under the same licence. The game, the decompilation and the
third-party components are not covered and keep their own terms. See [LICENSE.md](LICENSE.md).

Pokémon Stadium is © Nintendo / Creatures / GAME FREAK / The Pokémon Company.
This is a fan project, not affiliated with or endorsed by them.
