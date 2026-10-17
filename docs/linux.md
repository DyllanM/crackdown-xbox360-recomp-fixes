# Linux and Steam Deck

The Linux build is a native x86_64 program, not a Windows build under Proton or Wine. It uses the same code and the same
launcher as the Windows build: ReXGlue SDK 0.10.0's Linux runtime and the Xenos GPU plugin's Vulkan backend, both built
with the project's patches. It ships as one AppImage, which is also the Steam Deck build.

**Experimental.** The AppImage is built and checked by the project's scripts and the automated tests pass on Linux, but
this release has not yet been fully validated on real Linux PCs or Steam Deck hardware. Reports are welcome.

Every setting of the launcher works on Linux, apart from **Graphics card**: that list selects a Direct3D 12 adapter, so it
exists on Windows only. On Linux the runtime prefers the dedicated GPU, and the usual selection variables (`DRI_PRIME`,
`MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT`, `__NV_PRIME_RENDER_OFFLOAD`) take precedence when they are set.

## Requirements

- 64-bit x86 Linux with glibc 2.31 or newer, such as Ubuntu 20.04 or later (`ldd --version` shows it)
- A Vulkan driver (Mesa RADV on a Steam Deck and most AMD and Intel PCs)
- An X11 or Wayland desktop (the runtime loads `libX11`, `libX11-xcb`, `libxcb` and `libwayland-client`)
- `zenity` or `kdialog` for the launcher's file picker, and `xdg-open` for its folder buttons
- Your own Crackdown disc image or its extracted files (see the README for the exact version)

The AppImage carries the C++ runtime of its build machine (GCC 13 or newer). It is used only when the system's own copy
is older than GCC 13.

## Installing

1. Download **CrackdownRecompiled-v…-x86_64.AppImage** and put it in a folder you can write to.
2. Make it executable: right-click it > **Properties** > **Permissions** > **Is executable**, or
   `chmod +x CrackdownRecompiled-v*-x86_64.AppImage`.
3. Run it. The launcher opens.
4. On the **Play** page click **Install from disc image...** and pick your Crackdown `.iso` (or **Install from folder...**
   for extracted files). The game files, about 6 GB, are copied into a `game` folder next to the AppImage.
5. Press **PLAY**.

### Steam Deck

This is the intended flow. It has not been tried on a Steam Deck itself.

1. In **Desktop Mode**, do steps 1 to 5 above. Copy your disc image to the Deck first (USB stick, SD card or network).
2. In Steam: **Games** > **Add a Non-Steam Game to My Library**, and pick the AppImage.
3. Return to **Game Mode** and start it from your library.

Use the touchscreen or the trackpads in the launcher. A controller's **Start** button presses Play, and the D-pad, left
stick, A and B move through the launcher.

### If the AppImage does not start

An AppImage mounts itself with FUSE before any of its own code runs. Where that mount is not possible, for example when
Steam starts a non-Steam AppImage in an environment without a working FUSE helper, the AppImage unpacks itself into a
private folder under `/tmp` (about 70 MB), runs the game from there with the same arguments, returns the game's exit
status and removes the folder afterwards. Nothing has to be set in Steam. A restart of the game from the launcher starts
the original AppImage again and goes through the same steps. Where FUSE works, the AppImage is mounted and nothing is
unpacked. Only a failed mount triggers this: a damaged AppImage is still reported as damaged. `game`, `logs` and
`user_data` stay next to the AppImage in both cases.

In that case a line such as `Error: $FUSERMOUNT_PROG not set` can still appear in the terminal or Steam's log; it is
expected and harmless. To force the unpacking, set the game's Steam **Launch Options** to

```
APPIMAGE_EXTRACT_AND_RUN=1 %command%
```

(`%command% --appimage-extract-and-run` does the same). `VERBOSE=1` makes the AppImage print which path it took.

### If Vulkan is unavailable

When the Vulkan graphics system cannot start, the program opens a small launcher window drawn without the GPU. It shows
the game files and graphics status; **Play** explains that a working Vulkan driver is needed and points at the `logs`
folder next to the AppImage, and **Quit** closes it. It does not need zenity or any other helper.

## Where things are

| What | Where |
| --- | --- |
| Game files | `game` next to the AppImage |
| Logs | `logs` next to the AppImage (`user_data/logs` if that folder is not writable) |
| Settings, saves, shader cache | `user_data` next to the AppImage |

A build run outside an AppImage keeps `game`, `logs` and `user_data` next to the program instead. `--game_data_root` and
`--user_data_root` override the folders, as on Windows.

- **Window icon.** The program sets its window icon from the AppImage's own icon file, so it does not depend on the
  AppImage being integrated into the desktop.
- **Application ID.** The program tells the desktop it is `io.github.saulob.crackdown` (the Wayland app_id and the X11
  window class). The `.desktop` file in `packaging/linux` has the same name and sets it as `StartupWMClass`, and
  `tools/package-linux.sh` stops if they differ.

## Known limits

- Little testing on real hardware: rendering, audio, controllers and performance are largely unconfirmed on Linux.
- Holding **Shift** to bring the launcher back works only if the window gets keyboard focus while Shift is still held,
  because SDL cannot read the keyboard before the window has focus. Keep Shift held until the launcher appears.
- The VSync setting maps to Vulkan's present mode: **Off** allows immediate presentation (tearing), **On** uses mailbox or
  FIFO.
- On a Wayland desktop with display scaling above 100%, window sizes can come out larger than chosen. ReXGlue sizes its
  window this way on Linux.
- The window title shows the frame rate and render resolution where the window system displays titles.

## Building the AppImage

Run on Linux or WSL, from a clone with the TRG launcher submodule, after `tools/codegen.ps1` has generated
`generated/default` on Windows and with your own `game/assets/Default.xex` in place (it is only read to check that the
generated code is current):

```sh
tools/package-linux.sh            # incremental Release build, then package
tools/package-linux.sh --clean    # clean Release build first
```

It needs `clang-20`, `clang++-20`, `lld-20` (the game links with ThinLTO), CMake 3.25+, Ninja, rsync, git, wget or curl,
unzip or python3, binutils, the GCC 13 or newer C++ runtime and headers, and the X11/Wayland client libraries.

The AppImage needs the glibc of the machine that built it, so releases are built on Ubuntu 20.04 (glibc 2.31). The script
warns when the build machine's glibc is newer and stops if any packaged library needs more than 2.31.
`tools/setup-linux-build-env.sh` prepares a clean Ubuntu 20.04 machine for it, as root: clang 20 from
[apt.llvm.org](https://apt.llvm.org), the GCC 13 runtime from the `ubuntu-toolchain-r/test` PPA, CMake 3.31.8 and the
SDK's build dependencies. libdecor 0.2.2 and PipeWire 0.3.48 are not packaged for 20.04, so the script builds them from
their pinned commits into `/usr/local`; SDL loads both at run time and needs only their headers to build the Wayland
decoration and PipeWire audio backends, so the AppImage does not carry them. To make the environment in WSL:

```sh
wsl --import CrackdownFocalBuild <folder> ubuntu-base-20.04.5-base-amd64.tar.gz
wsl -d CrackdownFocalBuild -u root -- bash tools/setup-linux-build-env.sh
```

The base image is `ubuntu-base-20.04.5-base-amd64.tar.gz` from cdimage.ubuntu.com (SHA-256
`60e216b60947653dc8989be3821380268315b99b2959c29882781541bfe5a426`); a Docker `ubuntu:20.04` container works the same way.

The script copies the sources to `~/crackdown-linux-build` (set `CRACKDOWN_WORK` to change it), downloads the pinned
ReXGlue SDK and appimagetool with their SHA-256 checks, builds the project's patched ReXGlue runtime and Vulkan GPU plugin
from source the first time and whenever the patches change (network access and the packages above), builds the AppImage
runtime (below), builds the `linux-amd64-release` preset, checks that the AppDir holds no game files, saves, logs or local
paths, and writes `dist/CrackdownRecompiled-v<version>-x86_64.AppImage` with its `.sha256` (`--out-dir DIR` writes them to
DIR instead of `dist`). It publishes nothing. It prints the commit it was run on and says when the working tree has
uncommitted changes.

### The AppImage runtime

The runtime is the small program at the front of every AppImage that mounts it and starts AppRun. The project builds its
own from the upstream [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime) source, because the released
runtime stops when FUSE cannot mount the AppImage:

- `patches/appimage-runtime/baseline.env` pins the upstream commit (`dd6cebe`, tag `20251108`), the Alpine 3.21.0 root file
  system the build runs in, and the SHA-256 of the reviewed result.
- `patches/appimage-runtime/0001-extract-and-run-when-fuse-fails.patch` is the whole change: when the FUSE mount fails the
  runtime uses its own extract-and-run code instead (the one `APPIMAGE_EXTRACT_AND_RUN` selects); each run gets a private
  extraction folder, termination signals reach the game, and the folder is really removed afterwards.
- `tools/build-appimage-runtime.sh` fetches that commit, checks it, applies the patch and runs upstream's own chroot build
  (needs root or sudo, git, tar and network access). The result is checked (type 2 magic, static, the version string) and
  compared with the pinned SHA-256; if the checksum differs, packaging stops: review the new build and update
  `RUNTIME_SHA256`. `--check FILE` verifies a runtime or a finished AppImage.

A build prints, and saves next to the runtime as `<runtime>.manifest`, what produced it. The runtime's static components and
their licenses are listed in `THIRD-PARTY-LICENSES-LINUX.txt` inside the AppImage. The runtime is rebuilt only when the
pins or the patch change.

For a plain build without packaging, with the SDK extracted into `tools/rexglue/linux-amd64`, `tools/build.sh`
(`release`, `relwithdebinfo` or `debug`) builds the patched ReXGlue libraries when they are missing or out of date and then
the `linux-amd64-<config>` preset into `out/build/<preset>`. `tools/test.sh` builds and runs the tests that need no game
files.
