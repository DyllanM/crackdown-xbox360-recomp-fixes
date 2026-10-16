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

Ideas from the reference ports that were not ported because Crackdown showed no evidence for them (GPU
render-target/EDRAM/resolve changes, fence wake, critical-section spin, bulk register and draw paths) need
gameplay profiling first.

The XAudio worker rendezvous in `sub_82A50000` has no kernel wait; on title termination a codegen
`[[midasm_hook]]` in `config/overrides.toml` lets it return (`crackdown_audio_barrier_escape`).
