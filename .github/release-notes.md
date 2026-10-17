# Crackdown Recompiled v0.1.0

Crackdown for Xbox 360 as a native PC recompilation, with enhanced graphics, high frame rates, modern display support, a full launcher, offline achievements and language selection. It runs natively on Windows, and experimentally on Linux and Steam Deck as an AppImage.

## Highlights
- **Native Windows PC recompilation** (Direct3D 12): the game's own code, not an emulator.
- **Native Linux / Steam Deck AppImage (Experimental)**: Vulkan, no Proton or Wine.
- **Render Resolution up to 4x**, including 4K-class output depending on the selected aspect ratio and display setup.
- **30 / 60 / 90 / 120 / 144 / 165 / 240 FPS and Unlimited**, with improved frame pacing.
- **Native Aspect Ratio**: 16:9, 16:10, **21:9 Ultrawide**, **32:9 Super Ultrawide**, 4:3 and 5:4.
- **Internal render resolution**: 100% Native, 75% Performance or 50% Low-end.
- **Offline achievements** with the original icons and Gamerscore (**F7**), with no Xbox Live connection.
- **Language selector**: English, French, German, Italian, Spanish (Spain), Spanish (Latin America), Czech, Polish, Hungarian and Japanese.
- **Skip intro**, **Graphics card selection** on Windows, **Windowed / Borderless Fullscreen** with **F11**, and a full launcher.

## Graphics & Display
- **Native Aspect Ratio**: 16:9 Original, 16:10, 21:9, 32:9, 4:3 and 5:4. The game renders in the chosen format, so wider formats show more of the scene to the sides. Menus and videos made for 16:9 are not redesigned for every format.
- **Render Resolution** (1x to 4x) and **Internal render resolution** (100% / 75% / 50%), both separate from the Window size.
- **Presentation**: Letterbox or Stretch.
- **Graphics card selection** on Windows: Automatic or a specific GPU.
- **Windowed or Borderless Fullscreen**, **F11** to switch while playing, monitor selection, and window sizes that follow the aspect ratio.
- **Window title options**: frame rate and render resolution.
- Anti-aliasing (FXAA), Multisampling and Texture filtering options.

## Performance
- **Frame rate**: 30 (Original), 60 (Recommended, the default), 90, 120, 144, 165, 240 or Unlimited. Game speed stays the same at every rate.
- Improved frame pacing, and runtime optimizations that cut CPU waiting on the GPU on both platforms.
- Compatibility fixes for the recompiled game's graphics and audio.

## Quality of Life
- **Skip intro**: starts directly at the title screen.
- **Language selector** in the launcher for the game's text and interface.
- **Offline achievements**: press **F7** in game for the list. Unlock progress is saved locally.
- **Launcher** with an installer for your own disc image or extracted files, and display, graphics and gameplay options.
- **F11** display toggle, and an FPS counter (**F2**) plus frame rate and render resolution in the window title.
- Xbox controllers work without setup; an optional keyboard mode is available.
- Portable `user_data` folder for saves, settings and caches.

## Language
Select the game's text and interface language from the launcher: English, French, German, Italian, Spanish (Spain), Spanish (Latin America), Czech, Polish, Hungarian or Japanese. The available resources come from your own game files, and the language is applied when the game starts.

## Offline Achievements
Press **F7** while playing to open the achievements overlay, with the original icons, descriptions, Gamerscore, unlock status and progress. Progress is saved locally on your PC. There is no Xbox Live connection or synchronization.

## Linux / Steam Deck (Experimental)
Linux and Steam Deck support is **Experimental**; the release itself is not. It is a native x86_64 build using Vulkan, shipped as an AppImage, and does not use Proton or Wine. It has the same launcher and platform-neutral features as Windows. If the system cannot mount the AppImage with FUSE, it unpacks itself and runs from there. The AppImage is built and checked by automated scripts and tests, but it has not yet been fully validated on real Linux PCs or Steam Deck hardware. Reports are welcome. See [docs/linux.md](https://github.com/saulob/crackdown-xbox360-recompiled/blob/main/docs/linux.md).

## Known Issues
- Background music may occasionally stop during gameplay while sound effects and voices continue.
- The Graphics card selection exists on Windows only.

## Installation
You need your own supported Crackdown game files. **No copyrighted game data is included.** The launcher installs the game from your disc image or extracted files and checks that it is this exact version:

| | |
| --- | --- |
| Game | Crackdown, Xbox 360 retail disc (2007), original release without a title update |
| Title ID | `4D5307DC` |
| `Default.xex` | 14,270,464 bytes, SHA-256 `7AAA7A8920D1C3C737E58CF583DEBAFE6013BE0A96BEC3EC136E5DE7E3377E5A` |
| Game files | 196 files, 6,555,220,594 bytes, copied from the disc |

Requirements:
- **Windows**: Windows 10 or 11 (64-bit) and a DirectX 12 graphics card.
- **Linux**: 64-bit x86 Linux with glibc 2.31 or newer (such as Ubuntu 20.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

## Downloads
**Windows**: **Crackdown-v0.1.0-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **crackdown.exe**.
3. On the launcher's **Play** page click **Install from disc image...** and pick your own Crackdown `.iso`, or **Install from folder...** for extracted disc files.
4. Press **PLAY**.

**Linux and Steam Deck (Experimental)**: **CrackdownRecompiled-v0.1.0-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own disc image from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

## Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/crackdown-xbox360-recompiled/issues) and attach the newest `crackdown_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card, and whether you start the AppImage from Steam.

## Checksums
SHA-256 files are published with the release for both downloads: `Crackdown-v0.1.0-windows-x64.sha256` and `CrackdownRecompiled-v0.1.0-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `Crackdown-v0.1.0-windows-x64.zip` | `490f9d2cfb75b8c95b0f6b91853869f47e4f94853907711d2ee9a07e5333a6c5` |
| `CrackdownRecompiled-v0.1.0-x86_64.AppImage` | `bce086c2d4fa73401ac0ade13b69f4e898227db5f509b01a7b0088eecc4c1c25` |
