"""Offline vpkd3d128 float16 semantics check.

Host arrays follow the codegen convention: host u32[k] holds guest element 3-k.
Emulates the old in-place emission, the new snapshot emission, and ReXGlue's
rex::float_to_xenos_half reference (no denormals, truncation, saturation).
"""
import math
import random
import struct

M32 = 0xFFFFFFFF


def f2b(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def b2f(b):
    return struct.unpack("<f", struct.pack("<I", b & M32))[0]


def ref_half(bits):
    """rex::float_to_xenos_half(value, preserve_denormal=false, round_to_nearest_even=false)."""
    a = bits & 0x7FFFFFFF
    if a >= 0x47FFE000:
        r = 0x7FFF
    else:
        if a < 0x38800000:
            r = 0
        else:
            r = (a + 0xC8000000) & M32
        r = (r >> 13) & 0x7FFF
    return (r | ((bits & 0x80000000) >> 16)) & 0xFFFF


def new_lane(src_bits, src_snapshot_bits=None):
    """Emitted expression from emit_float16_lane, written with the same integer operations."""
    t = src_bits & 0x7FFFFFFF
    body = 0x7FFF if t >= 0x47FFE000 else (0 if t < 0x38800000 else (((t + 0xC8000000) & M32) >> 13) & 0x7FFF)
    return (body | ((src_bits & 0x80000000) >> 16)) & 0xFFFF


def old_lane(arr, src_idx, dst_idx):
    """Old generated sequence for one lane, reading and writing the live host array arr."""
    t = arr[src_idx] & 0x7FFFFFFF
    f = b2f(t)
    if math.isnan(f) or f > 65504.0:
        exp = 0xFF
    else:
        exp = (arr[src_idx] & 0x7F800000) >> 23
    tmp16 = 0 if exp == 0xFF else (arr[src_idx] & 0x7FE000) >> 13
    if exp == 0xFF:
        val = 0x7FFF
    elif exp > 0x70:
        val = ((exp - 0x70) << 10) + tmp16
    elif 0x71 - exp > 31:
        val = 0
    else:
        val = (0x400 + tmp16) >> (0x71 - exp)
    set_u16(arr, dst_idx, val & 0xFFFF)
    set_u16(arr, dst_idx, get_u16(arr, dst_idx) | ((arr[src_idx] & 0x80000000) >> 16))


def get_u16(arr, k):
    return (arr[k // 2] >> (16 * (k % 2))) & 0xFFFF


def set_u16(arr, k, v):
    w = k // 2
    shift = 16 * (k % 2)
    arr[w] = (arr[w] & ~(0xFFFF << shift) & M32) | ((v & 0xFFFF) << shift)


def old_lane_split(src, dst, src_idx, dst_idx):
    """Old sequence with the destination separate from the source."""
    t = src[src_idx] & 0x7FFFFFFF
    f = b2f(t)
    if math.isnan(f) or f > 65504.0:
        exp = 0xFF
    else:
        exp = (src[src_idx] & 0x7F800000) >> 23
    tmp16 = 0 if exp == 0xFF else (src[src_idx] & 0x7FE000) >> 13
    if exp == 0xFF:
        val = 0x7FFF
    elif exp > 0x70:
        val = ((exp - 0x70) << 10) + tmp16
    elif 0x71 - exp > 31:
        val = 0
    else:
        val = (0x400 + tmp16) >> (0x71 - exp)
    set_u16(dst, dst_idx, val & 0xFFFF)
    set_u16(dst, dst_idx, get_u16(dst, dst_idx) | ((src[src_idx] & 0x80000000) >> 16))


def pack_new(vec, count, shift_field):
    """New generated code: vSrc snapshot, then per-lane emission."""
    snap = list(vec)
    dst = [0xCDCDCDCD] * 4
    for i in range(count):
        src_idx = 3 - i
        dst_idx = ((1 - i) if count == 2 else (3 - i)) + 2 * shift_field
        set_u16(dst, dst_idx, new_lane(snap[src_idx]))
    return dst


def pack_new_inplace(vec, count, shift_field):
    """New generated code with vD == vB: the snapshot isolates the reads from the writes."""
    snap = list(vec)
    arr = list(vec)
    for i in range(count):
        src_idx = 3 - i
        dst_idx = ((1 - i) if count == 2 else (3 - i)) + 2 * shift_field
        set_u16(arr, dst_idx, new_lane(snap[src_idx]))
    return arr


def guest_words(host):
    return [host[3 - w] for w in range(4)]


def guest_to_host(words):
    return [words[3 - g] for g in range(4)]


FAILS = []


def check(name, cond, detail=""):
    if not cond:
        FAILS.append(f"{name} {detail}")


def vector_tests():
    # Official vectors from tests/ppc/asm/instr_vpkd3d128.s (guest order, IN -> OUT words).
    cases = [
        ("float16_2_0", 3, 0, [0x3F000000, 0xBF000000, 0, 0], [0xCDCDCDCD] * 3 + [0x3800B800]),
        ("float16_2_invalid_0", 3, 0, [0x3FC00000, 0xBFC00000, 0x42A23EC8, 0x403DB757],
         [0xCDCDCDCD] * 3 + [0x3E00BE00]),
        ("float16_4_0", 5, 0, [0x3F000000, 0xBF000000, 0x3F800000, 0xBF800000],
         [0xCDCDCDCD] * 2 + [0x3800B800, 0x3C00BC00]),
        ("float16_4_invalid_0", 5, 0, [0x3FC00000, 0xBFC00000, 0x3FC00000, 0xBFC00000],
         [0xCDCDCDCD] * 2 + [0x3E00BE00, 0x3E00BE00]),
    ]
    for name, typ, shift, words_in, words_out in cases:
        vec = guest_to_host(words_in)
        if typ == 3:
            got_new = guest_words(pack_new(vec, 2, shift))
        else:
            got_new = guest_words(pack_new(vec, 4, shift))
        # The reference words only assign the halves the instruction writes; compare those lanes.
        for w in range(4):
            if words_out[w] != 0xCDCDCDCD:
                check(f"vector {name} word{w}", got_new[w] == words_out[w],
                      f"got {got_new[w]:08X} want {words_out[w]:08X}")
    print("vector tests:", len(cases), "cases checked")


def random_tests(n, seed):
    rng = random.Random(seed)
    alias_new_bad = 0
    alias_old_bad = 0
    nonalias_old_bad = 0
    total = 0
    samples = [0, 0x80000000, 0x3F000000, 0xBF000000, 0x3F800000, 0xBF800000, 0x3C000000,
               0x7F800000, 0xFF800000, 0x7FC00000, 0x477FE000, 0x47FFE000, 0x38800000,
               0x33800000, 0x00000001, 0x40400000, 0xC2F60000, 0x4B3BDF83]
    for _ in range(n):
        if rng.random() < 0.3:
            bits = rng.choice(samples)
        else:
            bits = rng.getrandbits(32)
        vec = [bits, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)]
        vec = [vec[0], vec[1], vec[2], vec[3]]
        for count, typ_shift in ((2, 0), (4, 0), (2, 1), (4, 2)):
            total += 1
            ref = [0xCDCDCDCD] * 4
            for i in range(count):
                dst_idx = ((1 - i) if count == 2 else (3 - i)) + 2 * typ_shift
                set_u16(ref, dst_idx, ref_half(vec[3 - i]))
            got_alias = pack_new_inplace(vec, count, typ_shift)
            got_nonalias = pack_new(vec, count, typ_shift)
            written = [((1 - i) if count == 2 else (3 - i)) + 2 * typ_shift for i in range(count)]
            if any(get_u16(got_alias, k) != get_u16(ref, k) for k in written):
                alias_new_bad += 1
            if got_nonalias != ref:
                alias_new_bad += 1
            # Old in-place sequence compared on the written halves only.
            old_alias = list(vec)
            for i in range(count):
                old_lane(old_alias, 3 - i, ((1 - i) if count == 2 else (3 - i)) + 2 * typ_shift)
            written = [((1 - i) if count == 2 else (3 - i)) + 2 * typ_shift for i in range(count)]
            if any(get_u16(old_alias, k) != get_u16(ref, k) for k in written):
                alias_old_bad += 1
            old_nonalias = [0xCDCDCDCD] * 4
            for i in range(count):
                old_lane_split(vec, old_nonalias, 3 - i,
                               ((1 - i) if count == 2 else (3 - i)) + 2 * typ_shift)
            if any(get_u16(old_nonalias, k) != get_u16(ref, k) for k in written):
                nonalias_old_bad += 1
    print(f"random: {total} packs (count x shift combos) over {n} vectors")
    check("new alias == reference", alias_new_bad == 0, f"bad={alias_new_bad}")
    print(f"  new lane (snapshot, alias and non-alias) mismatches vs reference: {alias_new_bad}")
    print(f"  old in-place mismatches vs reference: {alias_old_bad}")
    print(f"  old non-alias mismatches vs reference: {nonalias_old_bad}")


def truncation_test(n, seed):
    """Independent check of the normal range: truncated mantissa, exponent rebiased by 15 - 127."""
    rng = random.Random(seed)
    bad = 0
    for _ in range(n):
        x = b2f(f2b(rng.uniform(-60000.0, 60000.0) * 10 ** rng.uniform(-4, 0)))
        if abs(x) < 2.0 ** -14 or abs(x) >= 65504.0:
            continue
        bits = f2b(x)
        e = math.frexp(abs(x))[1] - 1  # |x| = 1.m * 2^e
        mant = int((abs(x) / 2.0 ** e - 1.0) * 1024.0)  # truncation
        half = (((e + 15) << 10) | mant) | (0x8000 if x < 0 else 0)
        if half != ref_half(bits):
            bad += 1
    check("truncation normal range", bad == 0, f"bad={bad}")
    print(f"truncation (normal range) mismatches: {bad}")


def head_sign_example():
    # Head vertex z is -1.60 in model space; the old in-place write kept the magnitude and lost the sign.
    bits = f2b(-1.6)
    ref = ref_half(bits)
    e = (ref >> 10) & 0x1F
    m = ref & 0x3FF
    magnitude = (1 + m / 1024.0) * 2.0 ** (e - 15)
    print(f"-1.6 -> half {ref:04X} ({'neg' if ref & 0x8000 else 'pos'}), decoded magnitude {magnitude:.4f}")
    check("sign preserved for -1.6", ref & 0x8000 != 0)
    check("magnitude for -1.6", abs(magnitude - 1.6) < 1.0 / 512)


def main():
    vector_tests()
    random_tests(20000, 1234)
    truncation_test(200000, 99)
    head_sign_example()
    if FAILS:
        print("FAILED:")
        for f in FAILS:
            print("  ", f)
        raise SystemExit(1)
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
