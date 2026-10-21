# Crackdown Recompiled v0.5.0

Crackdown for Xbox 360 as a native PC recompilation, with enhanced graphics, high frame rates, modern display support, keyboard and mouse support, a full launcher, offline achievements and language selection. It runs natively on Windows, and experimentally on Linux and Steam Deck as an AppImage.

v0.5.0 is a major update. It adds true direct mouse look, fully remappable keyboard controls and modern button prompts. It fixes the explosion and ragdoll crash, along with a long list of physics problems above 30 FPS. The launcher now updates itself.

Thanks to @GaetanVigner and @DyllanM for their first contributions to the project.

## What's New

### Input & Controls
- **True direct mouse look**: the mouse turns the camera directly instead of emulating a controller stick, so there is no stick deadzone or speed curve in the way. Mouse sensitivity now ranges from 0.10x to 10.00x.
- **Fully remappable keyboard controls**: every keyboard control can be rebound on the launcher's **Controls** page. Click a control, then press the new key. **Reset to defaults** restores the original layout.
- **Modern button prompts and sharper HUD text**: the old Xbox 360 button glyphs are replaced by sharp modern ones, and HUD text is redrawn at twice the original resolution from the game's own typeface. With keyboard and mouse in use, prompts show your bound keys, and they switch back to controller glyphs as soon as a pad is used.
- **Better switching between mouse and controller**: the cursor follows the last device you used. A click in gameplay captures the mouse, and a controller, an overlay, menu keys or switching away from the window release it.
- **Any connected controller can now press Start**, including when several pads are or were connected (#10, by @GaetanVigner).

### Stability & Crash Fixes
- **Fixed: the explosion and ragdoll crash** ("Unhandled guest access violation ... 0x72D32F98", issues #8, #9 and #11). Above 30 FPS, the game's frame timer could report 0 ms frames, and a camera change during a knockdown or ragdoll then divided by zero. That could turn the world black and leave you without control, and the crash followed. The root cause is fixed (#12, by @DyllanM), and guards in the camera, aim, look-at and audio listener code stop invalid values from spreading or crashing the 3D sound pass (#10, by @GaetanVigner).
- **Fixed: ragdolls restarting through the floor** and related camera instability when a character is knocked down again while already ragdolled (#10, by @GaetanVigner).
- **Fixed: a crash from removed game objects** that stayed in the game's object list.
- **Fixed: audio voices stalling for good** after a long hitch on the PC.
- **Hang reports** (Windows): if the game freezes, the call stack of every thread is written to `hang_<n>.txt` in the logs folder, so freezes can be diagnosed from a report (#10, by @GaetanVigner).

### High Frame Rate & Gameplay
- **Fixed: launches on slopes and off ledges, and speed build-up while running down stairs and slopes above 30 FPS** (#10, by @GaetanVigner; #12, by @DyllanM).
- **Fixed: rockets and grenade rounds sometimes disappearing without exploding at high frame rates** (#10, by @GaetanVigner).
- **Fixed: characters getting stuck in walls or the floor** after landings, get-ups and steep faces at high frame rates, with gravity and other character timing kept at their 30 FPS behavior (#10, by @GaetanVigner).
- **Improved: Xbox 360-accurate vector math**: multiply-adds and dot products now round once, as on the console. With double rounding, a unit length could come out above 1.0, which added to the downhill speed build-up (#12, by @DyllanM). This adds no new CPU requirement: processors with FMA3 use it, and others get identical results through an exact fallback.
- **Improved: smoother game steps at Unlimited and during frame-rate dips**: when the PC cannot hold the selected rate, the game no longer alternates between very short and very long steps (#10, by @GaetanVigner).
- **Fixed: crowd animation and a cosmetic timer running at the wrong speed** above 30 FPS.
- 30 FPS behavior is unchanged by these fixes. Only the vector rounding applies at every rate, because it now matches the hardware.

### Launcher & Quality of Life
- **Automatic launcher updates**: the launcher checks the project's GitHub releases once per start. When a newer release exists, an update banner appears, and nothing is installed until you choose **UPDATE NOW** and then **RESTART TO UPDATE**. Only the port's own program files are replaced. Game files, DLC, saves, settings, logs and caches are kept. This works for the Windows ZIP and the AppImage. v0.4.0 and earlier have no updater, so install v0.5.0 manually; later releases can then be installed from the launcher.
- **Secure updates**: updates are downloaded only over HTTPS from the project's GitHub releases, every redirect is checked, and each package is verified against its published SHA-256 checksum before it is installed.
- **New launcher header artwork**.
- **Native main-menu Quit**: the main menu's Quit option now asks for confirmation with the game's own dialog and closes the game cleanly.

### Runtime & Compatibility
- Runtime fixes in the ReXGlue SDK layer: a dangling string when launching a title, wrong string comparisons, a negative scissor treated as "no scissor", and a broad warning cleanup with no behavior change (#10, by @GaetanVigner).
- XMA audio decoding keeps draining a partly delivered frame and no longer invalidates output early, so voices recover after a long stall.
- Unified session logging: on Windows, a crash adds a `CRASH REPORT` section to the session log. The launcher's **About** page has a **Detailed logging** option and opens the logs folder.

Keyboard and mouse input, the XMA stall guards, and parts of the vector math and high frame rate timing work are adapted from [Unowa001/crackdown-recomp](https://github.com/Unowa001/crackdown-recomp) (BSD 3-Clause). Its notice is in the `licenses` folder of the ZIP and in the AppImage.

## Highlights
- **Native Windows PC recompilation** (Direct3D 12): the game's own code, not an emulator.
- **Native Linux / Steam Deck AppImage (Experimental)**: Vulkan, no Proton or Wine.
- **30 / 60 / 90 / 120 / 144 / 165 / 240 FPS and Unlimited**, with game speed and physics that hold up at every rate.
- **Render Resolution up to 4x**, including 4K-class output depending on the selected aspect ratio and display setup.
- **Native Aspect Ratio**: 16:9, 16:10, **21:9 Ultrawide**, **32:9 Super Ultrawide**, 4:3 and 5:4.
- **Keyboard and mouse** with true direct mouse look and remappable controls, together with any Xbox controller.
- **Achievements** with no Xbox Live connection, **Cheats**, a **Language selector** and **Skip intro**.

## Graphics & Display
- **Native Aspect Ratio**: 16:9 Original, 16:10, 21:9, 32:9, 4:3 and 5:4. The game renders in the chosen format, so wider formats show more of the scene to the sides. Menus and videos made for 16:9 are not redesigned for every format.
- **Render Resolution** (1x to 4x) and **Internal render resolution** (100% Native, 75% Performance, 50% Low-end), both separate from the window size.
- **Presentation**: Letterbox or Stretch.
- **Graphics card selection** on Windows: Automatic or a specific GPU.
- **Windowed or Borderless Fullscreen**, **F11** to switch while playing, monitor selection, and window sizes that follow the aspect ratio.
- **Corrected exposure and brightness**.
- Anti-aliasing (FXAA), Multisampling and Texture filtering options.
- **Performance**: low CPU overhead across the graphics command processor and Direct3D 12 submission on its own thread (Windows). At 30 and 60 FPS, missed frames are caught up so game time keeps its normal speed. 90 FPS and higher are paced for even frame delivery.

## Quality of Life
- **Launcher** with an installer for your own disc image or extracted files, and display, graphics, controls and gameplay options.
- **Achievements**: the launcher page, or the **F7** overlay in game. Progress is saved locally.
- **Cheats**: 7 gameplay cheats, set on the launcher page or changed in game with **F10**.
- **Language selector**: English, French, German, Italian, Spanish (Spain), Spanish (Latin America), Czech, Polish, Hungarian and Japanese.
- **Skip intro**: starts directly at the title screen.
- An FPS counter (**F2**), plus frame rate and render resolution in the window title.
- A portable `user_data` folder for saves, settings and caches.

## Linux / Steam Deck (Experimental)
Linux and Steam Deck support is **Experimental**; the release itself is not. It is a native x86_64 build using Vulkan, shipped as an AppImage, and does not use Proton or Wine. It has the same launcher and platform-neutral features as Windows, including keyboard and mouse, remapping and the launcher updates. Hang reports are Windows only. Keyboard and mouse input has been tested manually on Windows only. If the system cannot mount the AppImage with FUSE, it unpacks itself and runs from there. The AppImage is built and checked by automated scripts and tests, but it has not yet been fully validated on real Linux PCs or Steam Deck hardware. Reports are welcome. See [docs/linux.md](https://github.com/saulob/crackdown-xbox360-recompiled/blob/main/docs/linux.md).

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
**Windows**: **Crackdown-v0.5.0-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **crackdown.exe**.
3. On the launcher's **Play** page click **Install from disc image...** and pick your own Crackdown `.iso`, or **Install from folder...** for extracted disc files.
4. Press **PLAY**.

**Linux and Steam Deck (Experimental)**: **CrackdownRecompiled-v0.5.0-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own disc image from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

## Reporting Problems
If the game does not start, crashes or freezes, please open an [issue](https://github.com/saulob/crackdown-xbox360-recompiled/issues). Attach the newest `crackdown_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), and after a freeze any `hang_<n>.txt` file, with a line about what you were doing. The launcher's **About** page opens that folder. For Linux reports, please also say your distribution and graphics card, and whether you start the AppImage from Steam.

## Checksums
SHA-256 files are published with the release for both downloads: `Crackdown-v0.5.0-windows-x64.sha256` and `CrackdownRecompiled-v0.5.0-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `Crackdown-v0.5.0-windows-x64.zip` | `6d7dcab1b33acfc0bcb1308683d1792b103636c382e99266b8f4a8327e2e10fd` |
| `CrackdownRecompiled-v0.5.0-x86_64.AppImage` | `d8f26b77e2d03a378417523f388cdb759467558c18a0a1b954f12e4d761a89b1` |
