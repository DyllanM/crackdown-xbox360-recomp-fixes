# ReXGlue patches

Baseline: ReXGlue v0.10.0 (`patches/rexglue-sdk/baseline.env`). Only `rexruntime` and `rexgpu-xenos` are rebuilt.
Patches must not move virtual functions or data members of classes the generated game code compiles against.

| # | Platform | Purpose |
|---|----------|---------|
| 0001 | all | `XamReadTileToTexture` ABI fix (achievement icons) |
| 0002 | all | runtime health observer hooks (audit) |
| 0003 | all | physical heap release order |
| 0004 | all | excluded audio channels |
| 0005 | all | vpkd3d128 in-place source |
| 0006 | windows | D3D12 occlusion query results (NPC indicator rating) |
| 0007 | all | XMA context sync fixes |
| 0008 | all | stop guest threads on title termination, with timeout |
| 0009 | all | RenderDoc: detect only when already loaded |
| 0010 | windows | D3D12 adapter preference (high-performance GPU) |
| 0011 | all | Vulkan device preference |
| 0012 | all | log host MSAA 2x support |
| 0013 | linux | SPIR-V texture exponent adjust |
| 0014 | linux | cache Vulkan swapchain paint pipeline |
| 0015 | all | timer queue waits instead of spinning (measured: 19.6% -> 0.5% of a core) |
| 0016 | all | batch performance counter reads |
| 0017 | all | vblank thread sleeps until due |
| 0018 | windows | vblank thread priority |
| 0019 | all | command processor waits instead of polling |
| 0020 | all | WAIT_REG_MEM woken by vblank |
| 0021 | windows | high-resolution sleep |
| 0022 | all | log guest MSAA usage |
| 0023 | all | UI repaint once per guest frame (was 3 per frame at 30 fps) |
| 0024 | all | GPU register writes: block copy for render-state, constant and fetch-constant runs (Geometry Wars 0004) |
| 0025 | all | draw path: primitive cache threshold and overlap fix, reused request allocation, Xenia defaults for `clear_memory_page_state` and `execute_unclipped_draw_vs_on_cpu` (Geometry Wars 0006) |
| 0026 | all | guest critical sections poll briefly before a kernel wait (Geometry Wars 0008) |
| 0027 | linux | idle command thread waits without the alertable flag (Geometry Wars 0020) |
| 0028 | linux | `WaitMultiple` backs off on lock contention instead of spinning (Geometry Wars 0021) |
| 0029 | all | DEBUG_SAULO frame event counters (vblank, command processor swap, guest output, host present); remove with `src/debug_saulo_frame30.cpp` |
| 0030 | all | render target transfers cut around rectangles a draw provably overwrites; stencil bits tracked per depth target (Hard Corps 0005) |
| 0031 | all | per-tile stencil bits and a conservative first row for ownership claims (Hard Corps 0007) |
| 0032 | all | resolve dumps only EDRAM tiles drawn since the last dump (Hard Corps 0008) |
| 0033 | all | command processor guest memory writes wake `WaitOnAddress`/futex waiters; `src/gpu_fence_wait.cpp` is the waiter (Hard Corps 0009) |
| 0034 | windows | early submission only while fewer than three are in flight, `PollBackgroundWork` finishes pending occlusion queries, no `direct_host_resolve` preflight (Hard Corps 0011 without its occlusion rewrite, which 0006 replaces) |
| 0035-0037 | windows | D3D12 halves of 0030-0032 (Hard Corps 0012-0014) |
| 0038-0040 | linux | Vulkan halves of 0030-0032, submissions and occlusion queries (Hard Corps 0016-0018) |
| 0041-0043 | all, windows, linux | resolves copied straight into the textures that sample them (`resolve_copy_into_textures`, Hard Corps 0022-0024) |
| 0044-0046 | all, windows, linux | above Original resolution, a resolve kept only in its texture is written back to memory when needed (`resolve_defer_writeback`, Hard Corps 0030-0032) |
| 0047-0049 | all, windows, linux | `internal_render_scale` 100/75/50: host render targets below the render resolution (Hard Corps 0036-0038, cvar renamed) |
| 0052 | all | codegen: bit-exact `vrsqrtefp` estimate (Xenia's table) instead of an exact 1/sqrt, which let the 3D sound arccos at `0x8225F910` return NaN (Crackdown Recomp SDK 0002); the helper is emitted into the generated `crackdown_pch.h` because the game compiles against the stock SDK headers; regenerate with `tools\codegen.ps1` |

ThinLTO (`CMakeLists.txt`) covers Release and RelWithDebInfo; the LTO cache is `<build dir>/lto-cache`.

Not ported: the Geometry Wars frame-limiter hook (`src/frame_pacing.cpp` paces frames itself).

The XAudio worker rendezvous in `sub_82A50000` has no kernel wait; on title termination a codegen
`[[midasm_hook]]` in `config/overrides.toml` lets it return (`crackdown_audio_barrier_escape`). The 13
`crackdown_variable_step_frame_ms` hooks there give the title's inline 16 ms frame-time sites the host-paced frame
time above 60 FPS. `crackdown_audio_wave_timeout_tick` at `0x8225B254` advances a wave's safety timeout at 30 Hz at any
frame rate, so speech is not stopped early above 30 FPS.
