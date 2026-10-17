## Crackdown Recompiled v0.1.0

The first release of the Crackdown PC port: the Xbox 360 game, statically recompiled to native code with the ReXGlue SDK, for Windows and for Linux, including the Steam Deck. It comes with a launcher that installs the game from your own disc and offers modern PC options.

### Highlights
- **Windows and native Linux.** A native Windows build (Direct3D 12) and a native Linux build (Vulkan), shipped as one AppImage that also serves the **Steam Deck**.
- **Launcher** with an installer for your own Crackdown disc image or extracted files, and display, graphics and gameplay options. The same launcher runs on both platforms.
- **Frame rate**: 30 (Original), 60 (Recommended, the default), 90, 120, 144, 165, 240 or Unlimited. Game speed stays the same at every rate.
- **Native Aspect Ratio**: 16:9 Original, 16:10, **21:9 Ultrawide**, **32:9 Super Ultrawide**, 4:3 and 5:4, with **Letterbox** or **Stretch** presentation. The game renders in the chosen format, so wider formats show more of the scene to the sides.
- **Render resolution** (1x to 4x) and **Internal render resolution** (100% Native, 75% Performance, 50% Low-end), separate from the Window size.
- **Graphics card selection** on Windows (Automatic or a specific GPU).
- **Windowed and Borderless Fullscreen**, **F11** to switch while playing, monitor selection, and window sizes that follow the aspect ratio.
- **Skip intro**: starts directly at the title screen.
- **FPS and title display options**: frame rate and render resolution in the window title, and a frame counter in the corner (**F2**).
- Anti-aliasing (FXAA), Multisampling and Texture filtering options.
- Xbox controllers work without setup; an optional keyboard mode is available.
- Portable `user_data` folder for saves, settings and caches.

### Downloads
**Windows**: **Crackdown-v0.1.0-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **crackdown.exe**.
3. On the launcher's **Play** page click **Install from disc image...** and pick your own Crackdown `.iso`, or **Install from folder...** for extracted disc files.
4. Press **PLAY**.

**Linux and Steam Deck**: **CrackdownRecompiled-v0.1.0-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own disc image from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included.** You need your own copy of the game, this exact version:

| | |
| --- | --- |
| Game | Crackdown, Xbox 360 retail disc (2007), original release without a title update |
| Title ID | `4D5307DC` |
| `Default.xex` | 14,270,464 bytes, SHA-256 `7AAA7A8920D1C3C737E58CF583DEBAFE6013BE0A96BEC3EC136E5DE7E3377E5A` |
| Game files | 196 files, 6,555,220,594 bytes, copied from the disc |

The launcher checks the files when you install them and refuses any other version.

Requirements:
- **Windows**: Windows 10 or 11 (64-bit) and a DirectX 12 graphics card.
- **Linux**: 64-bit x86 Linux with glibc 2.31 or newer (such as Ubuntu 20.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

### Known issues
- Background music may occasionally stop during gameplay while sound effects and voices continue.
- Linux and Steam Deck support is new in this release. It has had little testing on real Linux PCs, and none on a Steam Deck yet. Rendering, audio, controllers and performance may still have problems there. See [docs/linux.md](https://github.com/saulob/crackdown-xbox360-recompiled/blob/main/docs/linux.md).
- The Graphics card selection exists on Windows only.

### Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/crackdown-xbox360-recompiled/issues) and attach the newest `crackdown_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card, and whether you start the AppImage from Steam.

### Checksums
SHA-256 files are published with the release for both downloads: `Crackdown-v0.1.0-windows-x64.sha256` and `CrackdownRecompiled-v0.1.0-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `Crackdown-v0.1.0-windows-x64.zip` | `pending` |
| `CrackdownRecompiled-v0.1.0-x86_64.AppImage` | `pending` |
