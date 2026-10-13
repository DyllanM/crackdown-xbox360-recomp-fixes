"""Finds code reached only through data pointers, vtables or lis/addi pairs that
ReXGlue v0.10.0 did not register as functions, classifies each candidate and
writes the HIGH-confidence ones to config/discovered-functions.toml.

  python tools/find-missing-functions.py                      report only
  python tools/find-missing-functions.py --apply-high         also rewrite the TOML
  python tools/find-missing-functions.py --verify             check generated output
  python tools/find-missing-functions.py --explain ADDR ...   detail one address
  python tools/find-missing-functions.py --selftest

Needs a codegen run (generated/default) and game/extracted/default_image.bin.
Method and evidence rules follow tools/find-missing-functions.py in
hard-corps-uprising-recompiled. Section layout and paths are read from the image
and the manifest, not assumed.
"""
import bisect
import hashlib
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
MANIFEST = os.path.join(ROOT, 'crackdown_manifest.toml')
IMAGE_PATH = os.path.join(ROOT, 'game', 'extracted', 'default_image.bin')
IMAGE_SHA256 = 'e2e74dd9905dbbb73e9a6769b78726fba8e8abc508149d51a6c7c983f92575f5'
IMAGE_BASE = 0x82000000
IMAGE_SIZE = 0x1670000
OVERRIDES = os.path.join(ROOT, 'config', 'overrides.toml')
DISCOVERED = os.path.join(ROOT, 'config', 'discovered-functions.toml')
REPORT = os.path.join(ROOT, 'build', 'missing-functions-report.txt')
CXX_EH_MAGIC = 0x19930520
MAX_HIGH_BYTES = 0x400
IMAGE_CHARACTERISTIC_EXEC = 0x20000000

IMG = b''
table_sites = defaultdict(list)


@dataclass
class Section:
    name: str
    start: int
    end: int
    flags: int


@dataclass
class Layout:
    code: list
    data: list
    rdata: tuple
    pdata: tuple
    code_span: tuple


def u32(a):
    return struct.unpack_from('>I', IMG, a - IMAGE_BASE)[0]


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def parse_sections(img):
    e_lfanew = struct.unpack_from('<I', img, 0x3C)[0]
    if img[e_lfanew:e_lfanew + 4] != b'PE\x00\x00':
        sys.exit('image has no PE signature')
    nsec, opt_size = struct.unpack_from('<HH', img, e_lfanew + 6)[0], struct.unpack_from('<H', img, e_lfanew + 20)[0]
    table = e_lfanew + 24 + opt_size
    sections = []
    for i in range(nsec):
        off = table + 40 * i
        name = img[off:off + 8].rstrip(b'\x00').decode('latin-1')
        vsize, va = struct.unpack_from('<II', img, off + 8)
        flags = struct.unpack_from('<I', img, off + 36)[0]
        sections.append(Section(name, IMAGE_BASE + va, IMAGE_BASE + va + vsize, flags))
    return sections


def build_layout(sections):
    code = sorted((s.start, s.end) for s in sections if s.flags & IMAGE_CHARACTERISTIC_EXEC)
    data = [(s.name, s.start, s.end) for s in sorted(sections, key=lambda s: s.start)
            if not s.flags & IMAGE_CHARACTERISTIC_EXEC and s.name not in ('.pdata', '.reloc') and s.end > s.start]
    by_name = {s.name: s for s in sections}
    rdata = (by_name['.rdata'].start, by_name['.rdata'].end)
    pdata = (by_name['.pdata'].start, by_name['.pdata'].end)
    return Layout(code, data, rdata, pdata, (code[0][0], code[-1][1]))


LAYOUT = None
CODE_STARTS = []


def in_code(a):
    if a & 3:
        return False
    i = bisect.bisect_right(CODE_STARTS, a) - 1
    return i >= 0 and LAYOUT.code[i][0] <= a < LAYOUT.code[i][1]


def in_data(a):
    return any(s <= a < e for _, s, e in LAYOUT.data)


# ---------------------------------------------------------------------------
# Minimal PowerPC decoder: enough to classify control flow and reject data.
# ---------------------------------------------------------------------------
XO19 = {0, 16, 18, 33, 50, 129, 150, 193, 225, 257, 274, 289, 417, 449, 528}
XO31 = {0, 4, 8, 9, 10, 11, 19, 20, 21, 23, 24, 26, 27, 28, 32, 40, 53, 54, 55, 58, 60, 68, 73, 75,
        83, 84, 86, 87, 104, 119, 124, 136, 138, 144, 146, 149, 150, 151, 178, 181, 183, 200, 202,
        210, 214, 215, 232, 233, 234, 235, 246, 247, 266, 278, 279, 284, 306, 310, 311, 316, 339,
        341, 343, 370, 371, 373, 375, 407, 412, 434, 438, 439, 444, 457, 459, 467, 470, 476, 489,
        491, 498, 512, 533, 534, 535, 536, 539, 566, 567, 595, 597, 598, 599, 631, 659, 661, 662,
        663, 695, 725, 727, 759, 790, 792, 794, 824, 826, 827, 854, 918, 922, 954, 982, 983, 986,
        1014,
        6, 7, 38, 39, 71, 103, 135, 167, 199, 231, 342, 359, 374, 487, 519, 551, 647, 679, 775,
        807, 822, 903, 935}
XO59 = {18, 20, 21, 22, 24, 25, 26, 28, 29, 30, 31}
XO63_A = {18, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 31}
XO63_X = {0, 12, 14, 15, 32, 38, 40, 64, 70, 72, 136, 264, 583, 711, 814, 815, 846, 974}
PRIMARY_SIMPLE = {2, 3, 7, 8, 10, 11, 12, 13, 14, 15, 17, 20, 21, 23, 24, 25, 26, 27, 28, 29, 32,
                  33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
                  53, 54, 55}
TERMINATORS = {'b', 'blr', 'bctr'}
BCTR = 0x4E800420
BLR = 0x4E800020


@dataclass
class Insn:
    addr: int
    word: int
    kind: str
    target: int = 0


def decode(a):
    return decode_word_at(a, u32(a))


def decode_word_at(a, w):
    if w == 0:
        return Insn(a, w, 'null')
    op = w >> 26
    if op == 18:
        target = sext(w & 0x03FFFFFC, 26)
        target = target if w & 2 else (a + target) & 0xFFFFFFFF
        return Insn(a, w, 'bl' if w & 1 else 'b', target)
    if op == 16:
        bo = (w >> 21) & 31
        target = sext(w & 0xFFFC, 16)
        target = target if w & 2 else (a + target) & 0xFFFFFFFF
        cond = (bo & 0x14) != 0x14
        if w & 1:
            return Insn(a, w, 'bcl', target)
        return Insn(a, w, 'bc' if cond else 'b', target)
    if op == 19:
        xo = (w >> 1) & 0x3FF
        if xo not in XO19:
            return Insn(a, w, 'unknown')
        bo = (w >> 21) & 31
        cond = (bo & 0x14) != 0x14
        if xo == 16:
            if w & 1:
                return Insn(a, w, 'call')
            return Insn(a, w, 'bclr' if cond else 'blr')
        if xo == 528:
            if w & 1:
                return Insn(a, w, 'bctrl')
            return Insn(a, w, 'bcctr' if cond else 'bctr')
        return Insn(a, w, 'other')
    if op in PRIMARY_SIMPLE or op in (4, 5, 6):
        return Insn(a, w, 'other')
    if op == 30:
        return Insn(a, w, 'other' if ((w >> 2) & 7) <= 4 or ((w >> 1) & 15) in (8, 9) else 'unknown')
    if op == 31:
        return Insn(a, w, 'other' if ((w >> 1) & 0x3FF) in XO31 else 'unknown')
    if op == 58:
        return Insn(a, w, 'other' if (w & 3) <= 2 else 'unknown')
    if op == 59:
        return Insn(a, w, 'other' if ((w >> 1) & 0x1F) in XO59 else 'unknown')
    if op == 62:
        return Insn(a, w, 'other' if (w & 3) <= 1 else 'unknown')
    if op == 63:
        xo5 = (w >> 1) & 0x1F
        xo10 = (w >> 1) & 0x3FF
        return Insn(a, w, 'other' if xo5 in XO63_A or xo10 in XO63_X else 'unknown')
    return Insn(a, w, 'unknown')


def is_terminator_word(a):
    return decode(a).kind in TERMINATORS


def is_mtctr(w):
    return (w & 0xFC1FFFFF) == 0x7C0903A6


# ---------------------------------------------------------------------------
# Inputs from the previous codegen run and the config files
# ---------------------------------------------------------------------------
@dataclass
class Known:
    starts: list
    start_set: set
    imports: set
    labels: set
    pdata: dict
    pdata_begins: list
    config: dict
    branch_sites: dict
    names: dict


def read_manifest():
    text = open(MANIFEST, encoding='utf-8').read()
    out = re.search(r'out_directory_path\s*=\s*"([^"]+)"', text)
    if not out:
        sys.exit(f'{MANIFEST}: no out_directory_path')
    includes = re.search(r'includes\s*=\s*\[([^\]]*)\]', text)
    names = re.findall(r'"([^"]+)"', includes.group(1)) if includes else []
    return os.path.join(ROOT, *out.group(1).split('/')), names


def parse_config_functions(path):
    entries = {}
    if not os.path.exists(path):
        return entries
    for m in re.finditer(r'^\s*(0x[0-9A-Fa-f]{8})\s*=\s*\{([^}]*)\}', open(path, encoding='utf-8').read(), re.M):
        start = int(m.group(1), 16)
        body = m.group(2)
        end = re.search(r'end\s*=\s*(0x[0-9A-Fa-f]+)', body)
        size = re.search(r'size\s*=\s*(0x[0-9A-Fa-f]+|\d+)', body)
        if end:
            entries[start] = int(end.group(1), 16)
        elif size:
            entries[start] = start + int(size.group(1), 0)
        else:
            entries[start] = None
    return entries


def load_registered(gen):
    register_files = [f for f in sorted(os.listdir(gen)) if f.endswith('_register.cpp')]
    if len(register_files) != 1:
        sys.exit(f'{gen}: expected one *_register.cpp, found {len(register_files)}')
    register = open(os.path.join(gen, register_files[0]), encoding='utf-8').read()
    starts, imports, names = set(), set(), {}
    for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}), (\w+)\)', register):
        a = int(m.group(1), 16)
        if a in starts:
            sys.exit(f'duplicate SetFunction for {a:08X}')
        starts.add(a)
        names[a] = m.group(2)
        if m.group(2).startswith('__imp__'):
            imports.add(a)
    return starts, imports, names


def load_known(gen):
    starts, imports, names = load_registered(gen)
    partition = json.load(open(os.path.join(gen, 'codegen.partition.json')))['assignments']
    missing = {int(k, 16) for k in partition} - starts
    if missing:
        sys.exit(f'partition/register mismatch: {len(missing)} functions not registered')

    labels = set()
    for f in sorted(os.listdir(gen)):
        if re.search(r'_recomp\.\d+\.cpp$', f):
            labels.update(int(x, 16) for x in re.findall(r'^loc_([0-9A-F]{8}):',
                                                        open(os.path.join(gen, f), encoding='utf-8').read(), re.M))

    pdata = {}
    for a in range(LAYOUT.pdata[0], LAYOUT.pdata[1], 8):
        begin, data = u32(a), u32(a + 4)
        if begin == 0:
            continue
        length = (data >> 8) & 0x3FFFFF
        pdata[begin] = (begin + max(length, 1) * 4, bool(data >> 31))

    # Entries this tool wrote earlier are removed from the registered set, so the
    # scan does not depend on whether they were applied, and rediscovered each run.
    discovered = parse_config_functions(DISCOVERED)
    starts -= set(d for d in discovered if d in starts)
    labels = {l for l in labels if not any(s <= l < e for s, e in discovered.items() if e)}
    config = parse_config_functions(OVERRIDES)

    sites = defaultdict(list)
    for s, e in LAYOUT.code:
        for a in range(s, e, 4):
            insn = decode(a)
            if insn.kind in ('b', 'bl', 'bc', 'bcl'):
                sites[insn.target].append(a)

    return Known(sorted(starts), starts, imports, labels, pdata, sorted(pdata), config, sites, names)


# ---------------------------------------------------------------------------
# Exception-handling tables in .rdata (the SDK registers their funclets itself)
# ---------------------------------------------------------------------------
def eh_table_words(known):
    words = set()
    rlo, rhi = LAYOUT.rdata

    def mark(start, size):
        words.update(range(start, start + size, 4))

    def in_rdata(a, size=4):
        return rlo <= a and a + size <= rhi

    for begin, (_, has_eh) in known.pdata.items():
        if not has_eh or begin - 8 < LAYOUT.code_span[0]:
            continue
        table = u32(begin - 4)
        if not in_rdata(table, 28):
            continue
        if u32(table) == CXX_EH_MAGIC:
            max_state, unwind, n_try, try_map, n_ip, ip_map = (u32(table + o) for o in (4, 8, 12, 16, 20, 24))
            mark(table, 28)
            if unwind and in_rdata(unwind, max_state * 8):
                mark(unwind, max_state * 8)
            if try_map and in_rdata(try_map, n_try * 20):
                mark(try_map, n_try * 20)
                for i in range(n_try):
                    n_catch, handlers = u32(try_map + i * 20 + 12), u32(try_map + i * 20 + 16)
                    if handlers and n_catch <= 20 and in_rdata(handlers, n_catch * 16):
                        mark(handlers, n_catch * 16)
            if ip_map and in_rdata(ip_map, n_ip * 8):
                mark(ip_map, n_ip * 8)
        else:
            count = u32(table)
            if count <= 0x1000 and in_rdata(table, 4 + count * 16):
                mark(table, 4 + count * 16)
    return words


# ---------------------------------------------------------------------------
# References: data pointers and lis/addi pairs that name code addresses
# ---------------------------------------------------------------------------
@dataclass
class Ref:
    site: int
    kind: str
    section: str


def pointer_run(a):
    lo = a
    while in_code(u32(lo - 4)):
        lo -= 4
    hi = a
    while in_code(u32(hi + 4)):
        hi += 4
    return lo, hi


def collect_refs(known, eh_words):
    refs = defaultdict(list)
    for name, lo, hi in LAYOUT.data:
        for a in range(lo, hi, 4):
            t = u32(a)
            if not in_code(t):
                continue
            if a in eh_words:
                kind = 'eh'
            else:
                run_lo, run_hi = pointer_run(a)
                entries = [u32(x) for x in range(run_lo, run_hi + 4, 4)]
                if any(e in known.labels for e in entries):
                    kind = 'jumptable'
                elif run_hi > run_lo:
                    kind = 'vtable'
                else:
                    kind = 'dataptr'
            refs[t].append(Ref(a, kind, name))

    for lo, hi in LAYOUT.code:
        lis = [None] * 32
        for a in range(lo, hi, 4):
            w = u32(a)
            op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
            if op == 15 and ra == 0:
                lis[rd] = (w & 0xFFFF) << 16
                continue
            if op in (14, 24) and lis[ra] is not None:
                lo16 = w & 0xFFFF
                t = (lis[ra] + sext(lo16, 16)) & 0xFFFFFFFF if op == 14 else lis[ra] | lo16
                if in_code(t):
                    refs[t].append(Ref(a, 'lisaddi', '.text'))
                elif in_data(t):
                    table_sites[t].append(a)
            if is_terminator_word(a):
                lis = [None] * 32
    return refs


def switch_dispatch_follows(site):
    """True when the address built at site feeds an indexed load/add and a bctr soon after."""
    indexed = False
    for a in range(site + 4, site + 36, 4):
        w = u32(a)
        if (w >> 26) == 31 and ((w >> 1) & 0x3FF) in (23, 87, 279, 266):
            indexed = True
        if w == BCTR:
            return indexed
        if decode(a).kind in TERMINATORS:
            return False
    return False


def switch_table_site(t, refs, known):
    """Site in the preceding registered function that uses the referencing table as a switch table."""
    i = bisect.bisect_left(known.starts, t) - 1
    prev = known.starts[i] if i >= 0 else 0
    for r in refs:
        if r.kind not in ('vtable', 'dataptr'):
            continue
        lo, hi = pointer_run(r.site)
        for table in range(lo, hi + 4, 4):
            for site in table_sites.get(table, ()):
                if prev <= site < t and switch_dispatch_follows(site):
                    return site
    return 0


# ---------------------------------------------------------------------------
# Control-flow walk of one candidate function
# ---------------------------------------------------------------------------
@dataclass
class Walk:
    ok: bool = True
    reasons: list = field(default_factory=list)
    visited: set = field(default_factory=set)
    insns: dict = field(default_factory=dict)
    terminators: list = field(default_factory=list)
    has_call: bool = False

    @property
    def end(self):
        return max(self.visited) + 4 if self.visited else 0


def walk(start, bound, known, extra_starts=frozenset(), own_labels=False):
    """Linear-sweep CFG walk bounded by [start, bound). A registered function's own
    loc_ labels are extra block starts; for a candidate every label is foreign."""
    callable_ = lambda t: t in known.start_set or t in known.imports or t in extra_starts
    w = Walk()
    worklist, queued = [start], {start}
    if own_labels:
        for l in known.labels:
            if start < l < bound:
                queued.add(l)
                worklist.append(l)

    def fail(reason):
        w.ok = False
        if reason not in w.reasons:
            w.reasons.append(reason)

    def push(t):
        if t not in w.visited and t not in queued:
            queued.add(t)
            worklist.append(t)

    while worklist:
        a = worklist.pop()
        while True:
            if a >= bound:
                fail('falls into next function')
                break
            if a in w.visited:
                break
            if a != start and a in known.labels and not own_labels:
                fail(f'reaches label {a:08X} of another function')
                break
            insn = decode(a)
            w.visited.add(a)
            w.insns[a] = insn
            k = insn.kind
            if k == 'null':
                fail(f'null word at {a:08X}')
                break
            if k == 'unknown':
                fail(f'undecodable word {insn.word:08X} at {a:08X}')
                break
            if k == 'b':
                if start <= insn.target < bound:
                    push(insn.target)
                elif callable_(insn.target):
                    w.terminators.append(insn)
                else:
                    fail(f'b to unknown {insn.target:08X}')
                break
            if k in ('bl', 'bcl'):
                w.has_call = True
                if insn.target != start and not callable_(insn.target):
                    fail(f'bl to unknown {insn.target:08X}')
                    break
            elif k == 'bc':
                if start <= insn.target < bound:
                    push(insn.target)
                elif not callable_(insn.target):
                    fail(f'bc to unknown {insn.target:08X}')
                    break
            elif k in ('blr', 'bctr'):
                w.terminators.append(insn)
                break
            elif k in ('bctrl', 'call'):
                w.has_call = True
            a += 4
    if not w.terminators and w.ok:
        fail('no terminator')
    if w.ok and w.visited and (max(w.visited) + 4 > bound):
        fail('falls into next function')
    return w


# ---------------------------------------------------------------------------
# Candidate classification
# ---------------------------------------------------------------------------
@dataclass
class Candidate:
    start: int
    end: int = 0
    confidence: str = 'LOW'
    ref: int = 0
    ref_kind: str = ''
    prev: int = 0
    prev_term: str = ''
    final_term: str = ''
    shape: str = ''
    reasons: list = field(default_factory=list)


def shape_of(w):
    insns = [w.insns[a] for a in sorted(w.insns)]
    n = len(insns)
    terms = {t.kind for t in w.terminators}
    words = [i.word for i in insns]
    if n == 2 and (words[0] >> 16) == 0x3863 and insns[1].kind == 'b':
        return 'this-adjust-thunk'
    if terms == {'b'} and not w.has_call and n <= 8:
        return 'tailcall-thunk'
    if 'bctr' in terms and not w.has_call and any(is_mtctr(x) for x in words) and n <= 12:
        return 'guarded-vcall-thunk' if any(i.kind == 'bc' for i in insns) else 'vcall-thunk'
    if 'bctr' in terms and not w.has_call:
        return 'indirect-tail'
    if not w.has_call and terms == {'blr'}:
        return 'leaf-small' if n <= 12 else 'leaf'
    if not w.has_call:
        return 'leaf-tail'
    return 'normal'


def previous_entity(t, known, accepted):
    """Return (start, kind, end) of the function or accepted candidate before t."""
    i = bisect.bisect_left(known.starts, t) - 1
    prev_known = known.starts[i] if i >= 0 else 0
    prev_acc = max((s for s in accepted if s < t), default=0)
    if prev_acc > prev_known:
        return prev_acc, 'candidate', accepted[prev_acc]
    if prev_known in known.config and known.config[prev_known]:
        return prev_known, 'candidate', known.config[prev_known]
    if prev_known in known.pdata:
        return prev_known, 'pdata', known.pdata[prev_known][0]
    return prev_known, 'known', 0


def pdata_extent_containing(t, known):
    i = bisect.bisect_right(known.pdata_begins, t) - 1
    if i >= 0:
        begin = known.pdata_begins[i]
        if begin < t < known.pdata[begin][0]:
            return begin
    return 0


def next_boundary(t, known, all_candidates):
    i = bisect.bisect_right(known.starts, t)
    bound = known.starts[i] if i < len(known.starts) else LAYOUT.code_span[1]
    j = bisect.bisect_right(all_candidates, t)
    if j < len(all_candidates):
        bound = min(bound, all_candidates[j])
    for s in known.config:
        if s > t:
            bound = min(bound, s)
    return bound


def classify(t, refs, known, accepted, all_candidates):
    c = Candidate(t)
    kinds = {r.kind for r in refs}
    useful = [r for r in refs if r.kind in ('vtable', 'dataptr', 'lisaddi')]
    primary = useful[0] if useful else refs[0]
    c.ref, c.ref_kind = primary.site, primary.kind
    if len(refs) > 1:
        c.ref_kind += f'x{len(refs)}'

    prev_insn = decode(t - 4)
    c.prev_term = prev_insn.kind
    prev, prev_kind, prev_end = previous_entity(t, known, accepted)
    c.prev = prev

    bound = next_boundary(t, known, all_candidates)
    w = walk(t, bound, known, frozenset(accepted))
    c.end = w.end if w.visited else t + 4
    c.final_term = '/'.join(sorted({x.kind for x in w.terminators})) or '-'
    c.shape = shape_of(w) if w.ok else '-'

    hard, soft, jump = [], [], []
    if prev_insn.kind not in TERMINATORS:
        hard.append(f'previous word is {prev_insn.kind}')
    inside = pdata_extent_containing(t, known)
    if inside:
        hard.append(f'inside .pdata extent of {inside:08X}')
    if 'jumptable' in kinds:
        jump.append('referenced from a jump table')
    jt_site = switch_table_site(t, refs, known)
    if jt_site:
        jump.append(f'table used as switch table at {jt_site:08X}')
    if not w.ok:
        hard.extend(w.reasons)
    if not useful:
        hard.append('eh-only' if kinds == {'eh'} else 'no usable reference')

    if w.ok:
        for a in range(t + 4, c.end, 4):
            outside = [s for s in known.branch_sites.get(a, ()) if not t <= s < c.end]
            if outside:
                hard.append(f'branch from {outside[0]:08X} enters {a:08X}')
                break
        tail = []
        a = c.end
        while a < bound and u32(a) != 0:
            tail.append(decode(a).kind if u32(a) != BLR else 'blr')
            a += 4
        if tail and all(k in ('blr', 'nop') for k in tail) and len(tail) <= 2:
            c.end = a
        elif tail:
            soft.append(f'{len(tail)} unreached words after end')
        if c.end - t > MAX_HIGH_BYTES:
            soft.append('large body')

    if prev_kind == 'pdata':
        if prev_end > t:
            hard.append(f'inside .pdata extent of {prev:08X}')
        elif prev_end < t:
            soft.append(f'gap after .pdata end {prev_end:08X}')
    elif prev_kind == 'candidate':
        if prev_end != t:
            soft.append(f'gap after candidate {prev:08X}')
    else:
        pw = walk(prev, bound, known, frozenset(accepted), own_labels=True)
        if pw.visited and max(pw.visited) >= t:
            hard.append(f'inside walked body of {prev:08X}')
        elif not pw.ok or not pw.visited:
            soft.append(f'previous function {prev:08X} not walkable')
        elif pw.end != t:
            soft.append(f'gap after {prev:08X} end {pw.end:08X}')

    for s, e in known.config.items():
        if e and s < c.end and t < e:
            hard.append(f'overlaps configured {s:08X}')

    if jump:
        c.confidence = 'JT'
        c.reasons = jump + hard
    elif hard:
        c.confidence = 'EH' if 'eh-only' in hard else 'LOW'
        c.reasons = hard
    elif soft:
        c.confidence = 'MEDIUM'
        c.reasons = soft
    else:
        c.confidence = 'HIGH'
        c.reasons = ['clean']
    return c


def derive_config_ends(known):
    for s in sorted(known.config):
        if known.config[s] is None:
            i = bisect.bisect_right(known.starts, s)
            bound = known.starts[i] if i < len(known.starts) else LAYOUT.code_span[1]
            w = walk(s, bound, known, own_labels=True)
            known.config[s] = w.end if w.ok and w.visited else None


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def write_report(cands, calibration, explained, stats, reasons):
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    lines = ['# ' + ' '.join(f'{k}={v}' for k, v in stats.items())]
    lines.append('# reasons for non-HIGH candidates (count, first reason)')
    for reason, count in reasons.most_common():
        lines.append(f'#   {count:6d}  {reason}')
    lines.append('# START END CONFIDENCE REFERENCE REFTYPE PREV PREVTERM FINALTERM SHAPE REASON')
    for c in cands:
        if c.confidence == 'HIGH':
            lines.append(line_for(c))
    lines.append('# non-HIGH candidates')
    for c in cands:
        if c.confidence != 'HIGH':
            lines.append(line_for(c))
    lines.append('# calibration: configured [functions] entries re-classified as unregistered')
    for c, configured in calibration:
        lines.append(line_for(c) + f' config-end={configured:08X}' if configured else line_for(c))
    if explained:
        lines.append('# explained addresses')
        lines.extend(explained)
    open(REPORT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')


def line_for(c):
    return (f'{c.start:08X} {c.end:08X} {c.confidence} {c.ref:08X} {c.ref_kind} {c.prev:08X} '
            f'{c.prev_term} {c.final_term} {c.shape} {"; ".join(c.reasons)}')


def write_discovered(cands):
    lines = ['# Generated by tools/find-missing-functions.py --apply-high; do not edit by hand.',
             '[functions]']
    for c in cands:
        lines.append(f'0x{c.start:08X} = {{ end = 0x{c.end:08X} }}  # {c.shape}')
    open(DISCOVERED, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')


def explain(addr, refs, known, accepted, targets):
    out = [f'-- {addr:08X}']
    name = 'registered' if addr in known.start_set else 'unregistered'
    out.append(f'   state: {name}, loc_ label: {addr in known.labels}, config: {addr in known.config}')
    i = bisect.bisect_left(known.starts, addr) - 1
    if i >= 0:
        p = known.starts[i]
        out.append(f'   previous registered start: {p:08X} ({known.names.get(p, "?")}), ends {known.pdata[p][0]:08X} per pdata' if p in known.pdata else f'   previous registered start: {p:08X}')
        out.append(f'   previous word {addr - 4:08X}: {u32(addr - 4):08X} ({decode(addr - 4).kind})')
    j = bisect.bisect_right(known.starts, addr)
    if j < len(known.starts):
        out.append(f'   next registered start: {known.starts[j]:08X}')
    rs = refs.get(addr, [])
    out.append(f'   references: {len(rs)}')
    for r in rs[:12]:
        out.append(f'     {r.site:08X} {r.kind} in {r.section}')
    bound = next_boundary(addr, known, targets)
    w = walk(addr, bound, known, frozenset(accepted))
    out.append(f'   walk: ok={w.ok} end={w.end:08X} shape={shape_of(w) if w.ok else "-"} reasons={w.reasons}')
    for a in sorted(w.insns):
        out.append(f'     {a:08X} {w.insns[a].word:08X} {w.insns[a].kind}')
    pdata = pdata_extent_containing(addr, known)
    out.append(f'   inside pdata extent of: {pdata:08X}' if pdata else '   not inside any pdata extent')
    return out


def verify(known_gen, manifest_includes):
    entries = parse_config_functions(DISCOVERED)
    register_files = [f for f in os.listdir(known_gen) if f.endswith('_register.cpp')]
    register = open(os.path.join(known_gen, register_files[0]), encoding='utf-8').read()
    registered = defaultdict(int)
    for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}),', register):
        registered[int(m.group(1), 16)] += 1
    defined = defaultdict(int)
    for f in os.listdir(known_gen):
        if re.search(r'_recomp\.\d+\.cpp$', f):
            for m in re.finditer(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)',
                                 open(os.path.join(known_gen, f), encoding='utf-8').read(), re.M):
                defined[int(m.group(1), 16)] += 1
    failures = 0
    if os.path.basename(DISCOVERED) not in [os.path.basename(p) for p in manifest_includes]:
        print('FAIL manifest does not include config/discovered-functions.toml')
        failures += 1
    starts = sorted(registered)
    for s, e in sorted(entries.items()):
        problems = []
        if registered.get(s, 0) != 1:
            problems.append(f'registered {registered.get(s, 0)} times')
        if defined.get(s, 0) != 1:
            problems.append(f'defined {defined.get(s, 0)} times')
        i = bisect.bisect_right(starts, s)
        if e and i < len(starts) and starts[i] < e:
            problems.append(f'function {starts[i]:08X} starts inside')
        if problems:
            failures += 1
            print(f'FAIL 0x{s:08X}-0x{(e or 0):08X}: {"; ".join(problems)}')
    print(f'verify: {len(entries)} entries, {failures} failures, {len(registered)} registered functions')
    return failures == 0


def selftest():
    checks = [
        (decode_word_at(0, 0x4E800420).kind, 'bctr'),
        (decode_word_at(0, 0x4E800020).kind, 'blr'),
        (decode_word_at(0, 0x48000010).kind, 'b'),
        (decode_word_at(0, 0x41980010).kind, 'bc'),
        (decode_word_at(0, 0x7D6903A6).kind, 'other'),
        (is_mtctr(0x7D6903A6), True),
        (is_mtctr(0x7C0903A6), True),
        (is_mtctr(0x7D6903A7), False),
    ]
    failures = [c for c in checks if c[0] != c[1]]
    print(f'selftest: {len(checks) - len(failures)}/{len(checks)} passed')
    return not failures


def main(argv):
    global IMG, LAYOUT, CODE_STARTS
    if '--selftest' in argv:
        return 0 if selftest() else 1
    apply_high = '--apply-high' in argv
    explain_addrs = [int(argv[i + 1], 16) for i, a in enumerate(argv) if a == '--explain']
    gen, includes = read_manifest()

    IMG = open(IMAGE_PATH, 'rb').read()
    if len(IMG) != IMAGE_SIZE:
        sys.exit(f'{IMAGE_PATH}: unexpected size 0x{len(IMG):X}')
    if hashlib.sha256(IMG).hexdigest() != IMAGE_SHA256:
        sys.exit(f'{IMAGE_PATH}: not the supported image dump')
    LAYOUT = build_layout(parse_sections(IMG))
    CODE_STARTS = [s for s, _ in LAYOUT.code]

    if '--verify' in argv:
        return 0 if verify(gen, includes) else 1

    known = load_known(gen)
    derive_config_ends(known)
    eh_words = eh_table_words(known)
    refs = collect_refs(known, eh_words)

    targets = sorted(t for t in refs if t not in known.start_set and t not in known.labels)
    accepted = {}
    cands = []
    for t in targets:
        c = classify(t, refs[t], known, accepted, targets)
        cands.append(c)
        if c.confidence == 'HIGH':
            accepted[t] = c.end

    calibration = []
    saved_config, saved_labels = dict(known.config), set(known.labels)
    for s in sorted(saved_config):
        known.config = {k: v for k, v in saved_config.items() if k != s}
        known.labels = {l for l in saved_labels if not (s <= l < (saved_config[s] or s + 4))}
        ref_list = refs.get(s) or [Ref(0, 'none', '-')]
        calibration.append((classify(s, ref_list, known, {}, targets), saved_config[s]))
    known.config, known.labels = saved_config, saved_labels

    explained = []
    for addr in explain_addrs:
        explained.extend(explain(addr, refs, known, accepted, targets))

    counts = Counter(c.confidence for c in cands)
    reasons = Counter(re.sub(r'\b[0-9A-F]{8}\b', '<addr>', r) for c in cands if c.confidence != 'HIGH'
                      for r in c.reasons[:1])
    stats = {'references': sum(len(v) for v in refs.values()),
             'unique_targets': len(refs),
             'registered': sum(1 for t in refs if t in known.start_set),
             'labels': sum(1 for t in refs if t in known.labels),
             'unregistered': len(targets),
             'HIGH': counts['HIGH'], 'MEDIUM': counts['MEDIUM'], 'LOW': counts['LOW'],
             'EH': counts['EH'], 'JT': counts['JT'], 'configured': len(saved_config)}
    write_report(cands, calibration, explained, stats, reasons)
    print(' '.join(f'{k}={v}' for k, v in stats.items()))
    print(f'report: {REPORT}')

    if apply_high:
        high = [c for c in cands if c.confidence == 'HIGH']
        write_discovered(high)
        print(f'wrote {len(high)} entries to {DISCOVERED}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
