# Building Crackdown

Normal development, with guest code that is current:

    .\tools\build.ps1          # build
    .\tools\build.ps1 -Run     # build, then launch the game

When `build.ps1` prints `Guest code: stale`, regenerate the guest code once, then build:

    .\tools\codegen.ps1
    .\tools\build.ps1 -Run

- **Guest code** (`generated\default`) is regenerated only by `tools\codegen.ps1`. `build.ps1` never runs codegen. It
  compares content hashes of the XEX, `crackdown_manifest.toml` and every include with
  `generated\codegen-inputs.stamp`, and stops when they differ. A patch that touches `src/codegen/` changes the
  codegen tool too: `codegen.ps1` then builds `rexglue.exe` from the patches into `out\rexglue-codegen` and uses it,
  and the stamp records that tool, so adding or editing such a patch makes the guest code stale.
- **ReXGlue runtime and GPU plugin** (`tools\rexglue\win-amd64`) are checked by content hash on every build. The
  fingerprint covers the baseline in `patches\rexglue-sdk\baseline.env`, the build settings in
  `tools\rexruntime-patch.ps1`, the toolchain and every `*.patch` file. When it does not match, `build.ps1` rebuilds
  the runtime itself, or restores the stock files when there are no patches. Adding, editing, renaming or removing a
  patch is enough; nothing else needs to change.
- **DLLs next to `crackdown.exe`** are compared with the installed files by hash after every build and before `-Run`.
  `-Run` does not start a stale or mismatched build.

Manual, for diagnosis only:

    .\tools\build-patched-rexruntime.ps1 -Check     # report the installed runtime and GPU plugin
    .\tools\build-patched-rexruntime.ps1 -Restore   # put the stock release files back

Only the requested `-Config` is built and installed (default: `RelWithDebInfo`), for targets `rexruntime` and
`rexgpu-xenos`. Debug and Release keep separate runtime stamps; all configurations use the same patch set.
Pass the same `-Config` to manual `-Check` or `-Restore` commands.

Toolchain detection restores `.EXE` to `PATHEXT` when missing before invoking `vswhere.exe`. This is needed when
a Windows PowerShell process inherits an incomplete environment from WSL; builds still use native Windows paths
and Visual Studio's Clang, LLD, CMake, Ninja and x64 SDK environment.

## Tests, launcher and packaging

    .\tools\test.ps1                         # pure-logic tests (user data, settings file, window placement, installer, ...)
    .\tools\build.ps1 -Target <names>        # runtime-linked tests (allocator stress, title-thread shutdown, RenderDoc detect)
    .\tools\package-release.ps1              # local Release build and dist\Crackdown-v<version>-windows-x64.zip

`package-release.ps1` builds into `build-release\`, stages only the allow-listed files and refuses to continue if
the staging folder holds game files, saves, logs, settings or local paths. Nothing is published or uploaded.

The launcher is the TRG library in `third_party/trg-launcher` (git submodule, pinned commit). It has no upstream
license file; get one from its author before distributing a package.

Patches with a `.windows` or `.linux` suffix apply only on that platform; unsuffixed patches apply everywhere.
See `docs/rexglue-patches.md` for what each patch is for.

Linux: only the Linux-suffixed patches and a clean-apply check exist. There are no Linux build or AppImage scripts
yet, and nothing has been built or run on Linux.
