# ReXGlue patches

Baseline: ReXGlue v0.10.0 (`patches/rexglue-sdk/baseline.env`). Only `rexruntime` and `rexgpu-xenos` are rebuilt, and the
result is installed over the stock files in `tools/rexglue`. The game compiles against the stock SDK headers, so a patch
must not move virtual functions or data members of classes the generated game code uses.

`NNNN-name.patch` applies on both platforms, `NNNN-name.windows.patch` only on Windows and `NNNN-name.linux.patch` only on
Linux. Numbers are unique and patches apply in name order; gaps are fine. 39 patches are common, 12 are Windows only and
12 are Linux only, so 51 apply on each platform. Patches 0005 and 0052 change the code generator and are the reason
`tools\codegen.ps1` builds its own `rexglue.exe`; the rest change the runtime and the GPU plugin.

| # | Platform | Purpose |
|---|----------|---------|
| 0001 | all | `XamReadTileToTexture` ABI fix (achievement icons) |
| 0003 | all | physical heap release order |
| 0004 | all | excluded audio channels |
| 0005 | all | codegen: `vpkd3d128` reads its source in place |
| 0006 | windows | D3D12 occlusion query results written when the submission completes (Agency target indicators) |
| 0007 | all | XMA context synchronisation fixes |
| 0008 | all | stop guest threads on title termination, with a timeout |
| 0009 | all | RenderDoc: detect only when already loaded |
| 0010 | windows | D3D12 adapter preference (high-performance GPU) |
| 0011 | all | Vulkan device preference |
| 0012 | all | log host MSAA 2x support |
| 0013 | linux | SPIR-V texture exponent adjust |
| 0014 | linux | cache the Vulkan swapchain paint pipeline |
| 0015 | all | timer queue blocks instead of spinning |
| 0016 | all | batch performance counter reads |
| 0017 | all | vblank thread sleeps until due |
| 0018 | windows | vblank thread priority |
| 0019 | all | command processor waits instead of polling |
| 0020 | all | WAIT_REG_MEM woken by vblank |
| 0021 | windows | high-resolution sleep |
| 0023 | all | UI repaint once per guest frame |
| 0024 | all | GPU register writes: block copy for render-state, constant and fetch-constant runs |
| 0025 | all | draw path: primitive cache threshold and overlap fix, reused request allocation, Xenia defaults for `clear_memory_page_state` and `execute_unclipped_draw_vs_on_cpu` |
| 0026 | all | guest critical sections poll briefly before a kernel wait |
| 0027 | linux | idle command thread waits without the alertable flag |
| 0028 | linux | `WaitMultiple` backs off on lock contention instead of spinning |
| 0030 | all | render target transfers cut around rectangles a draw provably overwrites; stencil bits tracked per depth target |
| 0031 | all | per-tile stencil bits and a conservative first row for ownership claims |
| 0032 | all | resolve dumps only EDRAM tiles drawn since the last dump |
| 0033 | all | command processor guest memory writes wake `WaitOnAddress`/futex waiters; `src/gpu_fence_wait.cpp` is the waiter |
| 0034 | windows | early submission only while fewer than three are in flight, `PollBackgroundWork` finishes pending occlusion queries, no `direct_host_resolve` preflight |
| 0035-0037 | windows | D3D12 halves of 0030-0032 |
| 0038-0040 | linux | Vulkan halves of 0030-0032, submissions and occlusion queries |
| 0041-0043 | all, windows, linux | resolves copied straight into the textures that sample them (`resolve_copy_into_textures`) |
| 0044-0046 | all, windows, linux | above Original resolution, a resolve kept only in its texture is written back to memory when needed (`resolve_defer_writeback`) |
| 0047-0049 | all, windows, linux | `internal_render_scale` 100/75/50: host render targets below the render resolution |
| 0050 | all | XMA context loop layout acquire loads |
| 0051 | all | audio callbacks paced at the hardware's steady rate (`audio_callback_pacing`) |
| 0052 | all | codegen: bit-exact `vrsqrtefp` estimate instead of an exact 1/sqrt; the helper is emitted into the generated `crackdown_pch.h` because the game compiles against the stock SDK headers; regenerate with `tools\codegen.ps1` |
| 0053 | linux | Vulkan occlusion query results, the counterpart of 0006: the guest's begin and end blocks are separate, and the end block stays unfinished until the submission completes |
| 0054 | all | keyboard and mouse binds also reach `XInputGetKeystroke` (menus such as Press Start), with the SDL driver's repeat timing |
| 0055 | all | mouse look sets the right stick from mouse speed (`mnk_mouse_scale`, `mnk_mouse_deadzone`), so slow movement still turns the camera |
| 0056 | all | `readback_resolve_max_bytes`: with `readback_resolve` on, only resolves of at most this many bytes are copied back to guest memory; larger ones stay GPU-only and may defer their writes (`resolve_defer_writeback`) |
| 0057 | windows | D3D12 half of 0056 |
| 0058 | linux | Vulkan half of 0056 |
| 0059 | all | a read fault on an uncommitted page in 0x70000000-0x70FFFFFF (save-load) maps one zeroed page, at most 8 pages, each logged as `[SAVE_RECOVERY]`; writes and other ranges are not recovered |
| 0060 | all | `XamEnumerate` sets the item count to zero before any error return and no longer copies an uninitialised value (an unimplemented `XamCreateEnumeratorHandle` made the title walk off its stack) |
| 0061 | all | `KeSaveFloatingPointState` and `KeRestoreFloatingPointState` stubs warn on the first call only (the title calls them every frame) |
| 0062 | all | the "Incorrect resolve sample selected" warning is logged once per sample/depth/MSAA combination; the fallback is unchanged |
| 0063 | all | an unhandled guest fault also logs `[SAVE_CRASH]` with the page's allocation and its place relative to the faulting thread's stack (from the PCR): own stack, guard, the page above, or other memory |
| 0064 | all | the "has \"invalid\" type" texture fetch warning is logged once per distinct fetch constant, up to 16, then one overflow notice; handling is unchanged |
| 0065 | all | 0059 recovers only free or reserved pages away from the faulting thread's stack and its guard pages, never a page adjacent to one it already recovered, and nothing when the stack is unknown |
| 0066 | all | a read-only open that finds nothing (`NO_SUCH_FILE`, `OBJECT_NAME_NOT_FOUND`) and a path without a device (the kernel has no current directory) log at debug instead of warning/error; other `NtCreateFile` failures and unknown devices still warn |

Patches 0054 to 0058 are adapted from Unowa001/crackdown-recomp (BSD 3-Clause, notice in
[THIRD-PARTY-LICENSES.txt](../packaging/licenses/THIRD-PARTY-LICENSES.txt)).

The AppImage runtime has its own pin and patch in `patches/appimage-runtime` (see [linux.md](linux.md)).

ThinLTO (`CMakeLists.txt`) covers Release and RelWithDebInfo; the LTO cache is `<build dir>/lto-cache`.

## Hooks in `config/overrides.toml`

The XAudio worker rendezvous in `sub_82A50000` has no kernel wait; on title termination a codegen `[[midasm_hook]]` lets it
return (`crackdown_audio_barrier_escape`). The `crackdown_variable_step_frame_ms` hooks give the title's inline 16 ms
frame-time sites the host-paced frame time above 60 FPS. `crackdown_audio_wave_timeout_tick` at `0x8225B254` advances a
wave's safety timeout at 30 Hz at any frame rate, so speech is not stopped early above 30 FPS.
`crackdown_audio_pcm_firewall` sanitizes the mixed audio frame before it is submitted.
