<p align="center">
  <img src="docs/images/wonderboy-logo.png" alt="Wonder Boy: The Dragon's Trap" width="540">
</p>

<p align="center">
  <strong>Unofficial PlayStation Vita port</strong><br>
</p>

Wonder Boy: The Dragon's Trap on PS Vita, with physical controls, an on-device APK/OBB importer, and support for switching between modern and retro graphics and audio.

## Requirements

- [kubridge](https://github.com/bythos14/kubridge/releases) installed and loaded. Reboot after installing it.
- `libshacccg.suprx` in `ur0:data/` or `ur0:data/external/`. See [ShaRKF00D](https://github.com/OsirizX/ShaRKF00D) for extraction instructions. This file does **not** go in your taiHEN plugin configuration.
- Your own supported Android APK and OBB, listed below.
- About **3 GB free on `ux0:` before copying the game files**.

## Supported game files

The APK contains the Android application; the OBB contains its larger game assets. Neither is included with the port.

| File | Supported version |
| --- | --- |
| APK | Android **1.1.0**, version code **105**, **ARMv7 / 32-bit** |
| OBB | **main.103** for `com.dotemu.wonderboy.dragonstrap` |

The APK's `105` and the OBB's `103` are the intended pairing. Their numbers do not need to match.

Use files from your own supported Android installation or backup. The game has an [official Google Play page](https://play.google.com/store/apps/details?id=com.dotemu.wonderboy.dragonstrap), but compatibility with the version currently delivered by the store has **not** been verified. PC, console, iOS, and ARM64-only files are not supported substitutes.

<details>
<summary>Reference file checksums</summary>

These identify the reference files used during development. The installer checks the required APK contents and the OBB itself; matching a filename alone is not enough.

**APK**: 21,709,600 bytes

```text
3f2a9187b7c4af60fcd8d085d39fbabd8ee00c5d7a9dfc1ce0be123fdc3c6729
```

**OBB**: 785,907,776 bytes

```text
dba60c0bf7ca52cf781810dec9dde225b01740cb2f4a3da352d3abdb155226e2
```

</details>

## Installation

1. Copy `wonderboy.vpk` to your Vita and install it through VitaShell. Accept the extended-permissions prompt.
2. Put **exactly one `.apk` and one `.obb`** directly inside `ux0:data/wonderboy/import/`:

   ```text
   ux0:data/wonderboy/import/
   ├── game.apk
   └── game.obb
   ```

   **Any filenames work**, including the original names. The `.apk` and `.obb` extensions are required; uppercase extensions work too. Do not place the files in subfolders. Extract APK/OBB files from any XAPK, APKS, or ZIP container first (renaming the container does not convert it)

3. Leave VitaShell, open **Wonder Boy**, and press **START** to prepare the files. No PC conversion is needed. Keep the Vita powered while preparation runs.
4. When **Ready to play** appears, press **START**. Later launches use the prepared data without repeating the import.

**Using USB on Windows?** If your Vita appears as `D:`, the import folder is `D:\data\wonderboy\import\`. Do not create an extra `ux0` folder.

**Need to pause preparation?** Press **Circle** and wait for the setup screen before closing the app. Press **START** next time to resume; completed files are checked and reused.

**Want to reclaim space?** On the ready screen, press **Triangle**, then **Triangle** again to confirm removal of the imported APK and OBB. Prepared data is checked first. **Circle** cancels. Deletion is optional and never automatic; keep your originals elsewhere for reinstalls.

## Controls

| Button | Action |
| --- | --- |
| D-pad / left stick | Move, crouch, climb |
| Up | Open doors and enter buildings |
| Cross (X) | Jump / swim / fly; confirm |
| Square | Attack |
| Circle | Use selected item; back |
| Triangle | Open inventory |
| L / R | Select previous / next item |
| START | Pause |
| SELECT + L | Switch modern / retro audio |
| SELECT + R | Switch modern / retro visuals |

Both touch panels are disabled. Press **X or START** to skip publisher intros and the opening story; loading cannot be skipped.

The FPS counter is **off on each launch**. Enable it through **Settings → Toggle FPS counter** to display it in the bottom-left corner.

## Saves and updates

Saves are stored in `ux0:data/wonderboy/saves/`.

The original checkpoint rules apply. Quitting during the opening castle can restart the prologue; reach the village after the first dragon before checking Continue.

To update, close the game and install the new VPK over the existing app. **Keep `ux0:data/wonderboy/` intact** to preserve your saves, prepared assets, and caches. An ordinary update does not require another import unless its release notes say otherwise. This version has no automatic updater.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Missing or duplicate inputs | Keep exactly one APK and one OBB directly in the import folder. Check for hidden extensions such as `.apk.zip`. |
| Unsupported file | Verify the supported version and pairing above. Renaming a different version will not help. |
| Not enough space | Preparation needs about 1.3 GB beyond the copied inputs, plus room for the app and caches. Free space and resume. |
| Interrupted preparation | Keep the inputs, reopen the app, and press START. |
| App exits immediately | Check kubridge, the shader compiler path, and permission to run unsafe homebrew. Reboot after installing kubridge. |
| Stalls or frame drops | Shader caching can cause initial stalls. Frame-time spikes remain a known issue in this candidate. |

For a bug report, include the port version, location, and steps to reproduce. After closing the game, copy `ux0:data/wonderboy/run.log` **before launching again**, because the next launch replaces it. Include any error code and the FPS reading for performance reports.

## Building

The source includes a native build script and dependency fingerprints in `dependencies.json`. With a matching VitaSDK and libraries:

```sh
python build.py --sdk /path/to/vitasdk --check
python build.py --sdk /path/to/vitasdk --jobs 4
```

The script builds the native executable and application metadata. It does not create a complete VPK; packaging artwork and Android game files are not included in this source repository.

## Credits

- **Lizardcube and Dotemu**: Wonder Boy: The Dragon's Trap.
- **Westone and the original Wonder Boy team**: the original game.
- **VitaSDK, VitaGL, soLoader, kubridge, SDL, and their contributors**: tools and libraries that make this port possible.
- Third-party licenses are retained alongside the bundled source and in `licenses/`.

The logo comes from the [official Lizardcube press kit](https://www.lizardcube.com/presskit/TheDragonsTrap/). Game content, artwork, and trademarks belong to their respective owners. This is an unofficial fan port, not affiliated with or endorsed by the game's creators or Sony.
