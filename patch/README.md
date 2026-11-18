# Patch kit

Windows scripts for maintainers: they turn a build of the port into the installable `.cia` and into the BPS
patch that is published on Releases. Nothing here contains game data: your ROM, your builds and the patches
themselves are ignored by git (see `.gitignore`).

To play, you do not need this folder: follow "Play it" in the main README.

## Scripts

All of these need `tools\flips.exe` (Floating IPS) and your ROM in `rom\baserom.z64`.

- **`public.bat`**: the public release. Type the version (for example `0.1.0`). It builds the `.cia` from
  `build\normal\pokestadium.elf` with the defaults of `cia_config\`, makes
  `patches\pokestadium_v_<version>_cia.bps` in delta mode, checks it carries no ROM data
  (`tools\check_patch.py`) and applies it to your ROM to verify the result is identical
  (`tools\verify_patch.py`). Upload that `.bps` to a GitHub Release; it is not committed.
- **`make_patch.bat [build file] [rom]`**: only makes and checks a patch for any build file.
- **`verify_patch.bat`**: applies the newest patch in `patches\` to your ROM and compares.

If `check_patch.py` fails, the patch contains data that is also in the ROM and must not be published.
