"""Report imported stubs and every direct caller of the known null crash. No codegen or fixes."""
from collections import Counter
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'out/rexglue-sdk-src'
OUTPUT = ROOT / 'out/runtime-audit'


def classify(name):
    groups = [
        ('G', r'^NetDll_|^XNet|^WSA'),
        ('D', r'Floating|VectorUnit|Context|Unwind'),
        ('E', r'XMA|Audio|Voice'),
        ('B', r'Stfs|File|Device|Volume|Directory'),
        ('C', r'Thread|Wait|Event|Semaphore|SpinLock|Critical|Timer|Irql'),
        ('F', r'^Vd|Gpu|Video'),
        ('G', r'Net|Socket|WSA'),
        ('H', r'Content|Profile|User|License|Signin|Achievement'),
        ('A', r'Dbg|Debug|Perf|Devkit|Dm'),
    ]
    return next((category for category, pattern in groups if re.search(pattern, name)), 'I')


def inventory():
    imports = set(re.findall(r'\b__imp__(\w+)', (ROOT / 'generated/default/crackdown_init.cpp').read_text()))
    definitions = {}
    for path in sorted((SDK / 'src/kernel').rglob('*.cpp')):
        text = path.read_text()
        for match in re.finditer(r'REX_EXPORT_STUB(?:_RETURN)?\(\s*__imp__(\w+)', text):
            definitions[match[1]] = {'kind': 'macro_stub', 'source': str(path.relative_to(SDK)),
                                     'line': text.count('\n', 0, match.start()) + 1}
        for match in re.finditer(r'REX_EXPORT\(\s*__imp__(\w+),\s*[\w:]+', text):
            definitions.setdefault(match[1], {'kind': 'implemented_or_manual_stub',
                                               'source': str(path.relative_to(SDK))})
        for match in re.finditer(r'REX_EXPORT\(\s*__imp__(\w+),\s*(?:\w+::)*(\w+)', text):
            body = re.search(r'\b' + re.escape(match[2]) + r'\([^;]*?\)\s*\{(.*?)\n\}', text, re.S)
            if body and re.search(r'REX\w+_(?:WARN|ERROR)\([^;]*\bstub\b', body[1], re.I | re.S):
                definitions[match[1]] = {'kind': 'manual_stub_logged', 'source': str(path.relative_to(SDK))}
    rows = []
    for name in sorted(imports):
        row = {'name': name, 'category': classify(name), **definitions.get(name, {'kind': 'external_or_unknown'})}
        if name in ('StfsCreateDevice', 'StfsControlDevice'):
            row['kind'] = 'manual_success_stub'
        if row['kind'] == 'macro_stub':
            row['behavior'] = 'r3 unchanged unless explicitly STUB_RETURN; runtime observes actual value'
        rows.append(row)
    return rows


def callers():
    rows = []
    imports = Counter()
    for path in sorted((ROOT / 'generated/default').glob('crackdown_recomp.*.cpp')):
        text = path.read_text()
        imports.update(re.findall(r'\b(__imp__\w+)\(ctx, base\)', text))
        for fn in re.finditer(r'DEFINE_REX_FUNC\(([^)]+)\).*?(?=\nDEFINE_REX_FUNC|\Z)', text, re.S):
            body = fn[0]
            for call in re.finditer(r'\bsub_82A49908\(ctx, base\)', body):
                before = body[max(0, call.start() - 1800):call.start()]
                pcs = re.findall(r'ctx.lr = (0x[0-9A-F]+);', before)
                rows.append({'function': fn[1], 'return_pc': pcs[-1] if pcs else '?',
                             'source': path.name, 'line': text.count('\n', 0, fn.start() + call.start()) + 1,
                             'preceding_code': before})
    return rows, imports


def main():
    rows = inventory()
    sites, imports = callers()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    report = {'imports': rows, 'null_crash_direct_callers': sites,
              'import_static_calls': dict(sorted(imports.items()))}
    (OUTPUT / 'static-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    stubs = [row for row in rows if 'stub' in row['kind'] and row['kind'] != 'implemented_or_manual_stub']
    lines = ['# Runtime static audit', '', f'Imports: {len(rows)}; imported stubs: {len(stubs)}; '
             f'null-crash direct sites: {len(sites)}.', '',
             '| Stub | Class | Behavior | Source |', '|---|---|---|---|']
    lines.extend(f"| {row['name']} | {row['category']} | {row['kind']} | {row.get('source', '?')} |" for row in stubs)
    lines += ['', 'Classes: A debug/devkit; B storage; C synchronization; D context/FP; E audio; '
              'F graphics; G networking; H profile/content; I unknown.', '',
              'Counts here are static sites. Executed counts and first callers come from StubAudit.', '',
              '| Caller | Return PC | Source line |', '|---|---|---|']
    lines.extend(f"| {site['function']} | {site['return_pc']} | {site['source']}:{site['line']} |" for site in sites)
    (OUTPUT / 'static-audit.md').write_text('\n'.join(lines) + '\n')
    print(f'imports={len(rows)} imported_stubs={len(stubs)} classes={dict(Counter(row["category"] for row in stubs))} '
          f'null_call_sites={len(sites)} report={OUTPUT.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
