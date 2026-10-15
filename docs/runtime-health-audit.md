> Superseded by [runtime-batch-fix.md](runtime-batch-fix.md) for the prompt3 batch findings and current validation. This document records the earlier general audit state.

> Superseded by [runtime-batch-fix.md](runtime-batch-fix.md) for the prompt3 batch findings and current validation. This document records the earlier general audit state.

# General runtime health audit

Status: **MULTIPLE FIXES IMPLEMENTED — GENERAL AUDIT READY**. Ready for one comprehensive user test.

Windows ReXGlue and the Crackdown incremental build succeeded. The game and codegen were not run.
The remaining attribution limits below do not require another diagnostic-only gameplay iteration.

## Windows toolchain failure

The failing detection command was:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Llvm.Clang `
  -property installationPath
```

The Windows PowerShell process inherited `PATHEXT=.CPL`. Its direct executable invocation produced no captured
stdout and no `LASTEXITCODE`, so `Get-BuildToolchain` received an empty installation path and threw
`Visual Studio with the C++ x64 tools and C++ Clang tools is required.` Running the same query with
`Start-Process -Wait` returned exit 0 and the installed Visual Studio path. Restoring `.EXE` to `PATHEXT`
made the original detection command work. `tools/build-common.ps1` now performs that minimal repair before
calling native tools. No SDK relocation, compiler replacement or WSL path conversion was needed.

The earlier missing-Windows-SDK conclusion was incorrect: the registered SDK is under
`E:\Windows Kits\10`, with Include version `10.0.26100.0`. The environment actually selected Visual Studio
18.10.12210.168, Clang/LLD **22.1.3**, MSVC **14.51.36231**, and Visual Studio's CMake/Ninja. LLVM 20.1.8
from the prompt was not the selected compiler. The native build used `S:\VisualStudio\repos\crackdown-xbox360-recompiled`.

Hard Corps and Geometry Wars build/patch workflows were compared locally. Their relevant detection also invokes
vswhere and imports VsDevCmd; copying a different build system would not fix this inherited PATHEXT defect.
The reproducible environment probe is in ignored `out/probe-windows-toolchain.ps1`.

## ReXGlue patch review and build

Pinned source: **v0.10.0, f5337cdc947ff6d4c4196737e2c807a48f2a1fc2**.

| Patch/change | Proof and purpose | Disposition |
|---|---|---|
| 0001 xam-tile-to-texture | Existing project patch | Preserved unchanged; applied and compiled |
| 0002 observer/telemetry | Host callback API; audio/XMA, VFS final native-open status, stub, invalid-function, GPU resolve and draw-rejection observations | Kept and extended; diagnostic, except the two SDL fixes below; compiled |
| 0002 SDL producer credit | Baseline pops and recycles a queued frame on failed SDL submission but does not release its producer semaphore credit. Success returns one credit per dequeued frame. Returning that same credit on discard preserves the producer/consumer invariant and avoids eventual credit exhaustion | Kept; generic behavioral fix, no device reset or forced silence |
| 0002 SDL queue access | Baseline submission debug output reads `frames_queued_.size()` without the queue mutex and increments a shared debug counter. Producer and callback mutate this queue. Removed the unlocked debug read/counter; all remaining queue-size observations hold `frames_mutex_` | Kept; generic race fix |
| 0003 physical release order | The pinned baseline frees parent physical pages before `TriggerCallbacks` temporarily drops the global lock. Another mapping may reuse those pages while the old view/metadata still exists. Release the view before the parent pages | Kept; generic behavioral fix, identical to both reference-project patches |

The physical defect was checked in this exact pinned source, including the callback lock release; upstream
v0.10.0 does not already contain the fix. Neither new patch contains Crackdown guest addresses or game symbols.
The SDL invariant was verified in local baseline code and AudioSystem's semaphore budgeting; no independent
SDL equivalent was found in the two reference patch sets. These fixes are justified independently of the
reported symptoms; they are not yet proven causes of the shooting/audio or null-pointer failure.

All three patches apply sequentially to a freshly materialized pinned baseline. Both runtime targets compiled
and linked through `tools/build-patched-rexruntime.ps1 -Config RelWithDebInfo -Jobs 12` (247 build actions).
Only **rexruntime** and **rexgpu-xenos**, configuration **RelWithDebInfo**, were requested. Required dependencies
were built automatically. Debug/Release were not rebuilt. The existing workflow now fingerprints each selected
configuration and gives Debug/Release separate stamps.

Fingerprint: `6E35DB0B82725ED2CCCD4B693ACBA975F55D83DEBEBF62CF9FF4AA8C4053E49D`.

| Artifact | Built = installed SHA-256 |
|---|---|
| rexruntimerd.dll | FE88CBF6DB9208D66C813665EDD586B62D3E9123CF9C0656445492872C755856 |
| rexruntimerd.lib | 30AFB32E0D68B9A7FF2270ED9FC74D94F3885EF88E0172A9BDC61D170B1B6CAC |
| rexgpu-xenosrd.dll | 3B16AE86D4BC859065696D251994FB22D055738C424747E3602935ADB6E9DDFE |

Installation and stamp update were performed by the project workflow; installed SDK source/headers were not
manually edited. Runtime/GPU DLLs and TracyClientrd.dll next to `build/crackdown.exe` match installed hashes.
The ReXGlue build emitted existing upstream warnings (unused parameters, initialization, member order,
nontrivial memcpy) under Clang 22; it is not a warning-free SDK build. No new project diagnostic warning was
emitted. The one subsequent Crackdown incremental build compiled main, diagnostics and RuntimeHealth, linked
successfully, and reported **zero warnings/errors**. Its captured log is `out/runtime-audit/project-build.log`.

## Automatic coverage

The collector keeps 512 host-only rolling events, timestamps/order, guest thread/caller, object, numeric payload
and bounded text. Its worker never dereferences guest memory. Guest snapshots use guarded reads in existing
hooks. Periodic summaries start after Character construction, run five seconds apart, and stop after 12;
shutdown and anomaly snapshots remain available. History dumps have a two-second cooldown and a 24-dump
budget; fatal evidence bypasses those limits. Frequent audio/XMA history is sampled while counters continue.
Logging rotates at 30 MB with two retained rotations. No debug key is required.

### Audio and XMA

Compiled telemetry includes XMA context create/release/enable/disable, title pool fill/release and occupancy,
packet/FFmpeg errors, empty decode results and stalled output progress. SDL summaries include submitted,
completed/discarded and queued buffers, callback age, underruns/starvation, mixer PCM, final PCM activity,
peak/RMS and run maxima, nonfinite PCM, mute/gain/pause, format/channel/buffer/device state and device errors.

Active output followed by over 500 ms of silence, underruns or stalled callbacks automatically generates an
AudioFault/history snapshot, including the state immediately preceding failure. A 500 ms resume grace avoids
classifying the pause-to-resume handoff as a stall. Intentional pause/mute/zero gain is gated. Finite peak
crossings above 2.0 are retained in history; this is a diagnostic threshold, not an audible-glitch detector.
The next log can distinguish continuing silent PCM, starvation/callback stoppage, decoder trouble and device
errors around the reported shooting -> glitch -> silence transition. It cannot prove audibility, associate a
particular gunshot voice or guarantee a causal diagnosis before gameplay evidence. SDL-completed means a
buffer was accepted and recycled, not that the speaker played it. Source lifecycle counts are APU clients.

### Character differential

Automatic per-run fields:

- `body_descriptor_nonzero_characters`
- `head_descriptor_nonzero_characters`
- `body_all_zero_characters`
- `head_all_zero_characters`

Separate unknown counts avoid treating unreadable/incomplete observations as all-zero. Nonzero means any
captured word in any of the five levels is nonzero. All-zero requires readable observations of all five levels
and all four words zero. Registries retain the latest observations per bounded unique Character address;
construction count is separate, and address reuse is not a proven lifetime identity. Up to four representative
Characters show all four BODY/HEAD words at each level in summaries. No word is called loaded/resolved.

82397C80 zeroes four output words and writes selected resource-record pointers; 823A3FF8 derives selectors
from `_lod`/shadow names. These observations do not prove later model loading/rendering. Existing evidence
remains 8 Characters, 40 BODY and 40 HEAD lookups, no variation calls and head word0 zero. No new gameplay
measurement or head-consumer hook system was introduced.

### HUD image groups

At 823D0FD0, groups use texture, a 64-bit hash of the complete eight-word image descriptor, and caller.
823D1E00 writes this 32-byte descriptor; the image stores its copy at +68. Texture/name lookup joins on the
same descriptor identity. `image_descriptor_id` is an observation fingerprint, not an invented literal image
index. Summaries show image name when joined, `hudtextures.tpk` as an atlas candidate, style vtable, total and
interval calls, geometry-producing calls, vertices/zero output, unique image objects, and duplicate same-image
calls within known 823FD260 passes. Top 24 groups are ranked by calls. Shared atlas textures no longer merge
all images into one caller/texture group. Hash collisions and dynamic descriptor changes remain possible.
The image objects are not proven world owners, geometry calls are not GPU submissions, and cTargetMarker is
not assumed to be the blue symbol.

### VFS and logical resource results

Summaries expose `probe_misses`, `fallback_successes`, `same_path_recoveries_approx`, `final_failures`, native
open failures and unrecovered native-open groups older than ten seconds. Top confirmed final-failure paths
are listed separately from top probe/native-open groups with `final_result=unproven`.

All paths, including state/config/level/Streaming/cache namespaces, are aggregated without per-open log spam.
Canonical matching normalizes case/slashes and known game/relative prefixes; it never merges by basename or
strips cache namespaces. The native VFS reports the actual final OpenFile status on every return path. A
probe-found event cannot clear an outstanding native-open failure. Scoped HUD logical requests retain up to
eight source attempts and distinguish fallback after a miss from final texture failure after alternatives.
Unmatched/unreadable completions are not invented successes.

For Lua packages, archive members, resblocks and asynchronous streaming, exact logical completion/source
correlation is unavailable. Observed later success at the same normalized path is explicitly approximate:
it may belong to another request and does not prove a package fallback. A native open failure alone also does
not prove final logical failure. Paths remain visible as unproven; no ten-second heuristic is relabeled as a
confirmed missing game asset. This is the strongest implemented safe approximation for the comprehensive test.

### Stubs, GPU, faults and threads

Static inventory remains **248 imports, 33 macro/logged manual stubs**, classified by subsystem. Runtime records
unique stubs actually executed, count, first caller/r3 and observed return behavior; only the first occurrence
logs immediately. FP save/restore, STFS and synchronization/audio stubs remain instrumented without speculative
implementations. Arbitrary kernel warnings no longer inflate the stub inventory. Unlogged manual no-ops remain
a static-audit limitation. GPU counters cover invalid 4-sample-depth resolve selection, common draw rejection
and existing warning/error text groups for invalid fetch, resource creation, shader and pipeline failure.
The latter are named `*_failures_logged`: they count matching SDK log events, may overlap, and cannot detect
silent failure paths or replace successful resource/submission accounting. Resolve behavior is unchanged.

Fault summaries show event count, last fault/object/caller/thread and invalid-function count. Known null entry,
null-page guest faults and invalid indirect/Execute targets retain registers, guest stack return chain and
synchronous RuntimeHistory. Fault count is evidence-event count, not unique crashes. Audio wait/semaphore
failures and pause/resume observations also feed history. General guest-thread lifetime attribution remains
unproven; these limits do not require new hook families for this test.

## Static null caller batch

82A49908 dereferences r3 unconditionally through its vtable while holding a global critical section. The lock
does not validate the caller's earlier pointer load. All ten direct sites in seven functions were reviewed:

| Call instruction / return PC | Function | r3 supplier | Null check / inferable context |
|---|---|---|---|
| 82250EDC / 82250EE0 | 82250DE0 | r31+692 | No; count-bounded record iteration |
| 82250F00 / 82250F04 | 82250DE0 | r31+696 | No; previous slot output flag clear |
| 82250F24 / 82250F28 | 82250DE0 | r31+700 | No; previous slot output flag clear |
| 8225B1DC / 8225B1E0 | 8225B0A8 | r30+0 | No; state-machine branch |
| 82257074 / 82257078 | 82256FE0 | r31+0 | No; record byte +16 nonzero |
| 8225D5EC / 8225D5F0 | 8225D508 | r31+0 | No; state +12=2, field +40=0 |
| 826E7E44 / 826E7E48 | 826E7E00 | array at [r31+0], byte index r28 | No element check; global container count positive |
| 826E8898 / 826E889C | 826E8760 | r31+8 | Yes; completion/state polling |
| 826E88DC / 826E88E0 | 826E8760 | r31+8 | Yes; alternate completion/state branch |
| 82B6B7D4 / 82B6B7D8 | 82B6B750 | r30+0 (owner r31+124) | No; preceding operation nonnegative, polling loop |

Specific guest threads and lifetime ownership cannot be proven statically for these sites; runtime records
them if reached. No speculative null guards were added. Physical-release ordering is independently justified,
without claiming a causal link to this null argument.

## Validation and cleanup

- Sequential clean-baseline application of all three patches: PASS; new generic patches contain no title addresses.
- Patched Windows ReXGlue compile/link/install: PASS; fingerprint and built/installed/staged hashes: PASS.
- One Windows Crackdown incremental build: PASS, zero warnings/errors, guest code current, no codegen build rule.
- Existing portable collector test compiled with `-Wall -Wextra -Werror`: PASS. It covers pause/mute/resume,
  active-to-silent detection, concurrent stub first/count aggregation, BODY/HEAD, HUD descriptor grouping,
  VFS probe/fallback/final classification, logged GPU counters, 512-event rollover and shutdown.
- Static import/null caller audit: PASS; generated-function registration check: 347 entries, zero failures.
- Source-comment review: temporary DEBUG_SAULO aids remain labeled; template/tutorial comments were removed
  from CrackdownApp, and new comments are limited to safety/ABI/synchronization constraints.
- `git diff --check`: PASS. No staging, commits or remote mutations.

**GAME NOT RUN. CODEGEN NOT RUN.** Codegen inputs are unchanged.

Next step after the comprehensive user log/screenshots: **BATCH FIX IMPLEMENTATION / CLEANUP**.
