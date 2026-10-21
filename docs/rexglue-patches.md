# ReXGlue patches

Baseline: ReXGlue v0.10.0 (`patches/rexglue-sdk/baseline.env`). Only `rexruntime` and `rexgpu-xenos` are rebuilt, and the
result is installed over the stock files in `tools/rexglue`. The game compiles against the stock SDK headers, so a patch
must not move virtual functions or data members of classes the generated game code uses.

`NNNN-name.patch` applies on both platforms, `NNNN-name.windows.patch` only on Windows and `NNNN-name.linux.patch` only on
Linux. Numbers are unique and patches apply in name order; gaps are fine. 55 patches are common, 24 are Windows only and
13 are Linux only, so 79 apply on Windows and 68 apply on Linux. Patches 0005, 0052 and 0095 change the code generator and
are the reason `tools\codegen.ps1` builds its own `rexglue.exe`; the rest change the runtime and the GPU plugin.

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
| 0055 | all | mouse look sets the right stick from mouse speed (`mnk_mouse_scale`, `mnk_mouse_deadzone`), so slow movement still turns the camera; `mnk_mouse_stick` lets a title switch the mouse-derived stick off for its own mouse look |
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
| 0067 | linux | when SDL video cannot start, stderr also gets the SDL error and the graphical-session hint (DISPLAY for X11, WAYLAND_DISPLAY for Wayland) before the normal failure exit |
| 0068 | all | the guest time base is read without a lock: it runs at one ratio from the last ratio change, so reading it no longer takes the clock mutex on every `mftb` |
| 0069 | all | the command processor also wakes `WaitOnAddress`/futex waiters after its read pointer and scratch register writebacks (the Direct3D ring waits in `src/gpu_fence_wait.cpp`) |
| 0070 | all | on Windows a missing host file is not searched for again by listing its folder after the exact name was not found (names are case-insensitive); `NtQueryFullAttributesFile` translates its path once |
| 0073 | windows | an occlusion query index is reused only after the pending query that held it has its result |
| 0078 | all | a shared memory request whose pages are all valid and allocated returns without the lock (the invalidation callback clears valid bits with release stores); `gpu_invalidation_expand_pages` (16; 64 is the original whole block) limits the pages a CPU write drops to an aligned group |
| 0079 | all | the register block copy covers every range without write side effects, not only render state: everything but the shader constants, scratch registers, `COHER_STATUS_HOST` and the gamma ramp ports |
| 0080 | all | `gpu_draw_state_memo`: an update claiming the same render target ranges as the previous one, with the ownership unchanged since, skips the ownership walk |
| 0081 | windows | `d3d12_async_submission`: a "D3D12 Submission" thread replays, executes and signals the recorded submissions in order, and a swap waits for it to drain; `d3d12_async_submission_draws` (2000) also ends a submission between draws outside occlusion queries. The thread is joined in the command processor's destructor and a repeated start does not create a second worker |
| 0082 | windows | D3D12 half of 0080: draws without transfers or clears skip the transfer pass, and sampler parameters are recomputed only when the shader, a fetch constant or `anisotropic_override` changed |
| 0083 | windows | `gpu_host_threads_avoid_cpu0`: the command processor and submission threads run on the process's CPUs except 0 and 1 when it has at least 8 in one processor group |
| 0084 | all | the ImGui drawer applies the cursor shape ImGui asked for through `SDL_SetCursor`, which nothing did otherwise (`imgui_drawer.cpp`), so launcher hyperlinks show the hand cursor |
| 0085 | all | `DetachFileSink` removes and closes the rotating file sink without destroying loggers, so a controlled hard exit can delete the session log without freeing logger state other threads may still use |
| 0086 | all | `XamLoaderLaunchTitle` keeps the expanded launch path in an owning `std::string`; a `std::string_view` pointed into the temporary from `utf8_join_guest_paths()` for a relative XEX name (`-Wdangling-assignment-gsl`) |
| 0087 | all | `RtlCompareStringN` compares up to the shorter real length (0xFFFF = `strlen`) and then orders by length, as `RtlCompareString`; before, "abc" and "abcd" compared equal |
| 0088 | windows | D3D12 texture load: `host_slice_size_base` starts at 0 (a `-Wuninitialized` false positive; the conditional lvalue write is not tracked, and every read follows a write) |
| 0089 | all | draw extent estimate: a negative scissor edge no longer counts as the 8192 "no scissor" case (signed comparison against `int32_t(xenos::kTexture2DCubeMaxWidthHeight)`); non-negative scissors are unchanged |
| 0090 | windows | D3D12 render target cache: the direct resolve success/fallback counters that 0034 left unused are removed (the class is private to the GPU plugin) |
| 0091 | all | warning cleanup without behaviour change except `MakePacketType0`, which now sets its one-register bit (its only caller passes false): `void*` casts for the deliberate texture-key `memcpy`/`memset`, unused parameter names, initializer lists in declaration order, unsigned `-1` sentinels, no pessimizing `std::move`, range-loop references, `= {}` for `X_SLIST_HEADER`, and an unused lambda capture |
| 0092 | windows | Windows half of 0091 and 0093: full `D3D12_HEAP_PROPERTIES` initializers, a missing `override`, explicit `GetProcAddress` casts, an unused lambda capture and an unused local |
| 0093 | all | unused locals removed where their initializer has no side effect; guest-memory reads, FidelityFX-only locals and the only use of a parameter keep their evaluation with `[[maybe_unused]]` |
| 0094 | all | a passive input-activity callback reports each device's changed state from `InputSystem::GetState`; the cursor policy reads it without altering gamepad state, merging or device assignment |
| 0095 | all | codegen: `vmaddfp`/`vnmsubfp` (and the 128 forms) round once like the hardware, and `vmsum3fp128`/`vmsum4fp128` sum in double and round once (Xenia's order) instead of `dpps`; with two roundings a unit length could round above 1.0. The helpers are emitted into the generated `crackdown_pch.h`: with `-mfma` or on ARM64 they use the native fused multiply-add, otherwise x64 checks the CPU once and uses `vfmadd213ps` on FMA3 hosts and the exact round-to-odd emulation elsewhere, with the same bits, so FMA3 is not required |
| 0096 | all | XMA `Work()`: a partly delivered frame keeps draining after the input runs out, an input-starved context sets its output write offset to the read offset, and output is invalidated only when the ring is full, so voices no longer stall for good after a long host hitch |
| 0097 | windows | `rex::bit_scan_forward` is inline in `math.h` instead of defined in the runtime DLL: the GPU plugin calls it in its per-draw loops (constant maps, render target and texture bindings), and every call went through the import table. The runtime no longer exports it; nothing outside the SDK calls it. Portable, but Windows only until a Linux build checks it |
| 0098 | windows | the GPU plugin builds with ThinLTO in Release (`/opt:lldlto=3`), so the command processor's per-draw path (render target, texture and pipeline caches, shared memory, register file) inlines across its translation units |
| 0099 | windows | `gpu_skip_unchanged_constant_writes` (on): when the game rewrites float, bool, loop or fetch constants with the values they already have, the D3D12 command processor keeps the uploaded constant buffers, the texture bindings and the sampler memo instead of rebuilding them for the next draw. Vertex buffer residency is still reset on every fetch constant write, since the game may have rewritten the vertex data at the same address |
| 0100 | windows | the texture cache indexes its textures by base page, so `FindResolveCopyTargets` looks at the textures on the resolve's page instead of scanning all of them on every resolve. Portable, but Windows only until a Linux build checks it |
| 0101 | windows | D3D12 per-draw loops: vertex buffer residency is invalidated a 64-bit mask at a time instead of a bit at a time, the used float constants are copied a run of consecutive constants per `memcpy`, and the stencil tile bits are read and added in up to two contiguous runs that vectorize instead of a byte at a time with wrapping |

Patches 0054 to 0058, 0078 to 0083, 0095 and 0096 and the hooks in `src/audio_guest_sync.cpp` are adapted from
Unowa001/crackdown-recomp (BSD 3-Clause, notice in [THIRD-PARTY-LICENSES.txt](../packaging/licenses/THIRD-PARTY-LICENSES.txt)).

The AppImage runtime has its own pin and patch in `patches/appimage-runtime` (see [linux.md](linux.md)).

ThinLTO (`CMakeLists.txt`) covers Release and RelWithDebInfo; the LTO cache is `<build dir>/lto-cache`.

## Hooks in `config/overrides.toml`

The XAudio worker rendezvous in `sub_82A50000` has no kernel wait; on title termination a codegen `[[midasm_hook]]` lets it
return (`crackdown_audio_barrier_escape`). The `crackdown_variable_step_frame_ms` hooks give the title's inline 16 ms
frame-time sites the host-paced frame time above 60 FPS. `crackdown_audio_wave_timeout_tick` at `0x8225B254` advances a
wave's safety timeout at 30 Hz at any frame rate, so speech is not stopped early above 30 FPS.
`crackdown_audio_pcm_firewall` sanitizes the mixed audio frame before it is submitted.

The character hooks in `src/character_step_fixes.cpp` turn per-step terms the title tuned for 1/30 s into per-second ones
above 30 FPS, and change nothing at 30: the ledge and slope snap-down pull (`0x822B5634`, shorter steps only), the
vertical visual smoothing (`0x822A2DB8`, `0x822A2E28`), the first gravity increment after a grounded step (`0x822AA8F4`),
the hit-reaction and ragdoll tick windows (`0x822E191C`, `0x822BA48C`), the pose step of a restarted ragdoll
(`0x822BA2E8`) and the projectile impact ray (`0x823672B0`). `crackdown_crowd_anim_step` (`0x823308DC`) and
`crackdown_cosmetic_timer_step` (`0x823FF534`) scale two per-update constants, the crowd animation clock's 1/30 and a
0.033 s timer, to the update's length.

The title's frame timer (`sub_823263D8`, hooked in `src/frame_pacing.cpp`) keeps the fixed step's minimum frame, so with
the fixed step off it published 0 ms at `0x82D99128` on almost every other 60 FPS frame; a camera mode change on such a
frame divided by it, and the camera, the player and the audio listener went NaN. With the fixed step off the hook
publishes the port's own frame time instead, never 0.
