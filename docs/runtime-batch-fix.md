# Runtime batch fix

Current status: **TARGETED FINAL TRACE READY**. See [the final batch report](runtime-final-batch.md). The notes below describe the preceding HEAD repair batch; AUDIO and NPC INDICATORS still require the bounded evidence prepared in the latest pass.

## One-run log evidence

The latest log records the first invalid mixer frame at about 13:31:10.407. All six channels contain 512 `FFC00000` NaNs while XMA reports zero errors, stalls, and zero-sample decode results. The tail shows the queue at zero, submit age around 24 seconds, roughly 4,595 underruns, and an open, unpaused, unmuted device with no device errors. XMA continues to make progress, so the source is upstream of SDL but still not identified.

## Logging and audio changes

`DumpRuntimeHistory` now receives an explicit fault flag; only observed faults bypass limits. Nonfatal reports use a 10-second global cooldown and 24-dump budget. Per-reason incidents count repeated callbacks without queuing or dumping every history; activation and resolution are transitions, and mixer/output non-finite state resolves after 32 clean frames. Saturation is tracked separately when at least 90% of samples clip and absolute mean is at least 0.99 for 32 buffers. Shutdown is idempotent and is invoked from existing close/thread-exit hooks, before the SDK close path hard-exits. These changes preserve totals in summaries. A bounded synthetic 40,000-event test verifies two activations, one resolution, bounded dumps, and under 2 MiB captured output; a real two-minute log size remains to be measured.

The retained project-owned ReXGlue patch still excludes zero-weight channels before conversion. It is retained as a generic correctness fix, but it did not remove this run's NaNs because the guest mixer frame was already poisoned before `SubmitFrame`. No downstream clamp or device reset was added.

## Character pose, HUD and population

`sub_8229FB68` creates separate BODY and HEAD models with different skeleton resources, links BODY+1776 to HEAD, and stores the same Character owner at model+1804. `sub_8239FE60` only clears dependency state; its surrounding calls do not write the active root translation. The original update therefore leaves a linked HEAD at zero while BODY has the Character/world translation. The hook now repairs only the two HEAD pose buffers when that exact owner/link/type invariant holds and the HEAD translation is zero, copying the valid BODY translation while preserving HEAD rotation and local animation.

The HUD family is three layers of one routine and resolves to `EnforcerRatingIcons.png` (texture `412D3DBC`, descriptor hash `3AF177E78C7B7728`, callers 823FC530/550/570). The retained snapshot shows 20+ image instances and 270 geometry draws, but the candidate path classifies them as Interesting Objects rather than Character owners. The atlas cell, eligibility, and lifetime defect are not proven; no cap, global hide, or reposition was added.

Eight registry entries represent Character profiles, not eight live NPCs. Candidate counts are not NPC ownership or population, and spawn/destruction totals are unavailable. Overpopulation and a shared head/HUD transform defect are therefore unproven. VFS logical requests all succeeded (2,184/2,184; zero final failures), so probe misses are not evidence for these symptoms.

## Reference checks and remaining signals

The frequently called FP save/restore pair wraps the guest timebase query at 826A8498; it does not bracket observed DSP/animation work. Current Xenia source does not provide those named implementations in its threading/misc files ([threading](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc), [misc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xboxkrnl/xboxkrnl_misc.cc)). ReXGlue keeps PPC FP/VMX state per context/thread. No FP/STFS stub implementation was justified. STFS/VFS counters show no proven resource failure. The repeated 4-sample-depth resolve selection warning matches the existing Xenia sanitize-to-zero behavior; draw rejects and other GPU failure counters are zero. It is not the leading head explanation. Fault and invalid-function counts are zero; no speculative guards were added.

## Changed files in this batch

Project files changed for this batch: `src/debug_saulo_diagnostics.cpp`, `docs/runtime-batch-fix.md`. Earlier project and ReXGlue changes remain in the working tree; no new ReXGlue patch was required for this batch.

## Validation

ReXGlue was already current (four patches, fingerprint `A7615B5AD50DB7A4DB1CAF8043EC8967DCAD93331BFDCB316B697404AA291DB0`). The incremental Crackdown build rebuilt the diagnostics object and linked successfully with no new project warnings or errors. `git diff --check` passes. **GAME NOT RUN. CODEGEN NOT RUN.**
