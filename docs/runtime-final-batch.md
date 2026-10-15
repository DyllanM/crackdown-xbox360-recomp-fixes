# Final runtime batch

**TARGETED FINAL TRACE READY.** Existing HEAD repair is preserved. AUDIO and NPC INDICATORS remain **UNRESOLVED**; this pass prepares compiled, bounded evidence and does not claim functional fixes for those blockers.

## Audio

`82A4BF44` is the return address of `XAudioSubmitRenderDriverFrame`, at `generated/default/crackdown_recomp.169.cpp:30397`. Its containing function is `sub_82A4BEF8`, starting at line 30353. It obtains a descriptor using `82A4FB48`, calls driver vtable+32, and submits descriptor+8 using the client at driver+24. Vtable `8205593C` resolves +32 to `82A4BD50` (`crackdown_recomp.122.cpp:30872`), which conditionally copies 6144 bytes to a guest capture/writeback pointer. Neither function performs sample arithmetic.

The retained runtime callback is `82A5B578 -> 82A51150`; scheduling uses `82A50738` and virtual task methods at +68. The full dynamic task-to-render-driver chain remains unobserved. Instrumented processing includes `82A4F410 -> 82A4DCF0`, routing at `82A4B7C8`, PCM resampling/conversion, coefficient initialization, and scheduler waits.

The first retained corruption is six planes of 512 `FFC00000` NaNs at approximately 13:31:10.407, with zero decoder errors, stalls and empty decode results. A shared producer is plausible; the shared sample/coefficient/state and first invalid operation are not established.

The AMD64 conversion explains persistent `BF800000`: `_mm_max_ps(NaN, -1.0f)` selects its second operand; `_mm_min_ps(-1.0f, 1.0f)` leaves -1.0. This does not establish a new clamp defect or justify replacing every NaN with zero. The excluded-channel patch is retained.

Callbacks continuing after the last submission prove queue drainage, not which producer transition caused it. The retained sampled pump history cannot distinguish a blocked guest call/barrier, stopped work list, producer exit or credit loss. No device/semaphore reset or speculative starvation fix was added.

`prepare-final-audio-trace.ps1` derives temporary hooks from 99 existing DSP, routing, resampling and scheduler functions during CMake configuration. It does not invoke ReXGlue codegen or change generated originals/codegen inputs. Statement comparison verifies preservation of every original executable statement. Aliased arithmetic inputs are copied before register writes. Events record guest function/PC, caller/parent, thread, arguments, DSP/state words, source/destination where known, scalar/vector input bits and output float32 bits. Routing entry inspects source PCM before mixing. Integer exponent checks keep finite operations out of the logger; logging preserves MXCSR.

The audio budget is 128 events: 96 deduplicated invalid sites and 32 reserved producer-stop records. After 500 ms without a submission, at most four snapshots report outstanding instrumented calls, engine worker flags, barriers and lock words. These are explicitly approximate concurrent observations. No events enter RuntimeHistory. Producers outside the selected functions are not guaranteed to be captured.

## NPC INDICATORS

Texture association is proven: `EnforcerRatingIcons.png`, texture `412D3DBC`, descriptor `3AF177E78C7B7728`. The exact visible blue cell/style remains unproven. No separate loose atlas or existing Crackdown atlas extractor was found in the inspected project/reference tools; no new extraction system was created.

`823FC530/550/570` return from three `823D0FD0` calls inside `823FC500` (`crackdown_recomp.90.cpp:9310`). Images are instance+752, +396 and +40: three layers of one candidate. The retained log shows the first layer emitting no geometry and the others one quad each. Historical unique image addresses and interval draws are not simultaneous logical owner counts. The retained candidate dump contains 73 style-1 records; it does not prove exactly 20 NPCs or incorrect population.

`823FC580` constructs 400 reusable records at manager+80 with 1152-byte stride. `823FED80` installs the manager. `823FD138` gathers through `823FC758`, `823FCA20`, and `823FCE30`, stores count at manager+0x70850, and disables records beyond count. Persistent record addresses therefore do not prove stale registration.

The first gather walks Interesting Objects. It requires object+3176==0, successful virtual+196 predicates, flag 0x10 clear at (object+292-32)+5457, an allowed classification from (object+276)+100, and conditional local-object exclusion. Classifications 1/4/5/6 map to styles 1/2/3/4; 2/3 are rejected. These raw predicates are not proven to mean alive, team, line of sight or mission state.

The second gather uses bytes 1765/1763/1764 and bit 0 of byte 187, squared distance, projection and overlap checks. The third additionally checks byte 3056, virtual+1276, a related object's virtual+196 predicate, local-object exclusion and classification; allowed classifications map to styles 8–11.

Records store world position at +1120, style at +1136, rating/state at +1140, owner at +1144 and flags at +1148/+1149. `823FBC38` applies projected-depth, rating/proximity, style-7 and overlap checks, alpha and color before setting instance+12. Rendering also requires an enabled image and valid texture.

Styles 5–11 read owner+320 and add a world Y offset. Styles 1–4 call virtual+12 on owner+32, then add the offset. `823E20A8` applies camera matrix rows based at `82C433A0`, divides by transformed Z beyond an epsilon, and applies screen scale/offset. There is no direct HEAD read in the inspected projection path; the first path's indirect position getter remains unresolved, so an indirect HEAD interaction cannot be excluded. No compensating offset was added.

At most 128 state-change events record instance, owner/vtable, first-path input object/vtable, raw eligibility fields, classification/style, rating, world/root bits, projection, screen coordinates, and all three layers' UVs/colors. Movement alone does not produce another event. Summaries distinguish simultaneous owner pointers, instances, geometry draws, post-candidate rejections, candidate removals and invalid positions. `stale_removals=unproven` avoids misclassifying ordinary candidate removal as a stale-lifetime repair.

## Cleanup and validation

HEAD translation repair is unchanged. Pose samples fell from 20 to one per tracked character, resource/variation logs to eight, and automatic character snapshots from twelve to two. Detailed character row dumps and the dependency-update hook were removed from the active trace path. Audio health, decoder errors, queue, underrun, starvation, submit-age and DC saturation totals remain.

Focused tests pass for finite-output silence, first-site deduplication under 10,000 repetitions, `FFC00000` reporting, dot-product propagation across lanes, concurrent budget enforcement, producer reserve and MXCSR preservation. Compiled hook statement comparison passes for all 99 functions. The isolated runtime-health test supplies mocks for the two new trace notifications and passes using the patched runtime source headers. Its first attempt used the installed baseline conversion header and failed the excluded-channel check; selecting the actual patched header resolved that test mismatch without editing the installed SDK.

One `tools/build.ps1` invocation succeeded in RelWithDebInfo with zero new project warnings. Installed hashes match the runtime stamp; staged runtime, GPU plugin and Tracy DLL hashes match installed files. Patches 0001–0004 are unchanged; no runtime rebuild was needed. Fingerprint:

`A7615B5AD50DB7A4DB1CAF8043EC8967DCAD93331BFDCB316B697404AA291DB0`

| File | Installed and staged SHA-256 |
| --- | --- |
| rexruntimerd.dll | 1310A5D48CC66D69146A6489C9D11145ED1700A69B4E9B16579E7143AA9ED06F |
| rexgpu-xenosrd.dll | 4E379B5726A14D7D1ED5DB97127DC3F7842906DF61B38B4673660E63F680814A |
| TracyClientrd.dll | 6E5AFCA2B13EE999FE21C33E049576D8DE951B4D0E5054AAFEB7C3A906227FE8 |

`git diff --check` passes. **GAME NOT RUN. CODEGEN NOT RUN.**

Changed this pass: `CMakeLists.txt`; `src/debug_saulo_diagnostics.cpp/.h`; `src/debug_saulo_final_trace.cpp/.h`; `src/runtime_health.cpp`; `tools/prepare-final-audio-trace.ps1`; `tests/final_audio_trace_test.cpp`; `tests/runtime_health_test.cpp`; `docs/runtime-batch-fix.md`; this report. Earlier working-tree changes are preserved.

## One combined validation

Run `.\tools\build.ps1 -Run`. Enter the Agency, inspect several NPC heads and blue NPC indicators, move around, kill one NPC and briefly carry the corpse. Shoot repeatedly for 2–3 minutes, checking that audio neither glitches nor disappears and indicators appear only where expected. Open/close one normal menu, continue briefly, exit normally and send one log with screenshots. No debug key.

If all three validate: **FINAL CLEANUP / CODE REVIEW**. Otherwise: **ONE FINAL BATCH CORRECTION PASS**, using the instruction and ownership evidence captured together.
