#!/usr/bin/env python3
"""Real wrapper selection, coverage feedback, safety flags and split link tests."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, AFL_PATH=str(ROOT), AFL_QUIET='1')
    for key in ('AFL_CC', 'AFL_CXX', 'AFL_USE_ASAN', 'AFL_USE_MSAN',
                'AFL_HARDEN', 'AFL_DONT_OPTIMIZE', 'AFL_INST_RATIO',
                'TRACE_CUSTOM_RECEIVE', 'TRACE_CUSTOM_SEND'):
        env.pop(key, None)
    commands, results = [], []

    def run(name, argv, local=None, data=None, success=True):
        commands.append({'name': name, 'argv': list(map(str, argv)),
                         'environment': local or {}})
        active = dict(env, **(local or {}))
        completed = subprocess.run(list(map(str, argv)), env=active, cwd=out,
                                   input=data, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=30)
        (out / (name + '.log')).write_bytes(completed.stdout)
        assert (completed.returncode == 0) == success, (name, completed.returncode,
                                                        completed.stdout.decode(errors='replace'))
        results.append({'name': name, 'returncode': completed.returncode, 'passed': True})
        return completed.stdout.decode(errors='replace')

    cc, cxx = ROOT / 'afl-clang-fast', ROOT / 'afl-clang-fast++'
    versions = run('default-c-version', [cc, '--version'])
    assert 'clang version 18.' in versions
    assert 'clang version 18.' in run('default-cpp-version', [cxx, '--version'])
    # Resolve the real compilers before adding probes to PATH. A hard-coded
    # compiler pathname would bypass these probes even if its version matched.
    compilers = {name: shutil.which(name, path=env.get('PATH'))
                 for name in ('clang', 'clang++')}
    assert all(compilers.values()), compilers
    probe_bin = out / 'path-compilers'
    probe_bin.mkdir(exist_ok=True)
    probe_log = out / 'path-compiler-calls.jsonl'
    probe_log.write_text('')
    for name, real_compiler in compilers.items():
        probe = probe_bin / name
        probe.write_text(
            '#!/usr/bin/env python3\nimport json,os,sys\n'
            f'with open({str(probe_log)!r}, "a") as log:\n'
            f'    print(json.dumps({{"compiler": {name!r}, "argv": sys.argv[1:]}}), file=log)\n'
            f'os.execv({real_compiler!r}, [{real_compiler!r}, *sys.argv[1:]])\n')
        probe.chmod(0o755)
    probe_env = {'PATH': str(probe_bin) + os.pathsep + env.get('PATH', os.defpath)}
    for name, wrapper, suffix in (('clang', cc, '.c'), ('clang++', cxx, '.cpp')):
        assert 'clang version 18.' in run('path-' + name + '-version',
                                           [wrapper, '--version'], probe_env)
        source = out / ('path-probe' + suffix)
        source.write_text('int path_probe(void) { return 0; }\n')
        run('path-' + name + '-compile',
            [wrapper, '-O0', '-g0', '-c', source,
             '-o', out / ('path-probe-' + name + '.o')], probe_env)
    probe_calls = [json.loads(line) for line in probe_log.read_text().splitlines()]
    assert [entry['compiler'] for entry in probe_calls] == [
        'clang', 'clang', 'clang++', 'clang++'], probe_calls
    for index, entry in enumerate(probe_calls):
        if index % 2 == 0:
            assert '--version' in entry['argv'], entry
        else:
            assert '-c' in entry['argv'] and any(
                flag.startswith('-fpass-plugin=') for flag in entry['argv']), entry
    run('plugin-export', ['nm', '-D', ROOT / 'afl-llvm-pass.so'])
    assert 'llvmGetPassPluginInfo' in (out / 'plugin-export.log').read_text()
    driver = run('driver', [cc, '-###', '-O0', ROOT / 'test-instr.c', '-o', out / 'driver'])
    assert '-fpass-plugin=' in driver and '"-O0"' in driver and '"-O3"' not in driver
    assert 'afl-llvm-rt-state-tracer.o' in driver
    spy = out / 'compiler-spy.py'
    spy.write_text('#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]))\n')
    spy.chmod(0o755)
    for wrapper, variable in ((cc, 'AFL_CC'), (cxx, 'AFL_CXX')):
        recorded = run(variable.lower() + '-override', [wrapper, '-c', '-O1', 'input.c'],
                       {variable: str(spy)})
        argv = json.loads(recorded)
        assert '-O1' in argv and '-O3' not in argv
        assert any(x.startswith('-fpass-plugin=') for x in argv)
        assert not any(x.endswith('.o') for x in argv)
    for mode, variables, required in (
            ('harden', {'AFL_HARDEN': '1'}, '-fstack-protector-all'),
            ('asan', {'AFL_USE_ASAN': '1'}, '-fsanitize=address'),
            ('msan', {'AFL_USE_MSAN': '1'}, '-fsanitize=memory'),
            ('builtin', {'AFL_NO_BUILTIN': '1'}, '-fno-builtin-strcmp')):
        captured = run(mode + '-flags', [cc, '-c', 'input.c'], dict(variables, AFL_CC=str(spy)))
        assert required in json.loads(captured)
    run('asan-msan-conflict', [cc, '-c', 'input.c'],
        {'AFL_USE_ASAN': '1', 'AFL_USE_MSAN': '1'}, success=False)
    run('harden-asan-conflict', [cc, '-c', 'input.c'],
        {'AFL_USE_ASAN': '1', 'AFL_HARDEN': '1'}, success=False)
    run('instrument-coverage', [cc, '-O0', ROOT / 'test-instr.c', '-o', out / 'coverage'])
    for name, data in (('path-empty', b''), ('path-one', b'1\n')):
        run(name, [ROOT / 'afl-showmap', '-m', 'none', '-q', '-o', out / (name + '.map'),
                   out / 'coverage'], data=data)
    first, second = [(out / (name + '.map')).read_bytes() for name in ('path-empty', 'path-one')]
    assert first and second and first != second
    for ratio in ('0', '101', 'invalid'):
        run('ratio-' + ratio, [cc, '-O1', '-c', ROOT / 'test-instr.c', '-o', out / 'bad.o'],
            {'AFL_INST_RATIO': ratio}, success=False)
    for ratio in ('1', '100'):
        run('ratio-' + ratio, [cc, '-O1', '-c', ROOT / 'test-instr.c', '-o', out / ('ratio' + ratio + '.o')],
            {'AFL_INST_RATIO': ratio})
    source = out / 'hello.c'
    source.write_text('#include <stdlib.h>\nint main(void) { char *p=malloc(96); if(!p)return 1; p[0]=7; free(p); return 0; }\n')
    cpp = out / 'hello.cpp'
    cpp.write_text('extern void escape(void*);\nint main(){ int *p=new int(7); int x=*p; delete p; return x!=7; }\n')
    for name, wrapper, src in (('c', cc, source), ('cpp', cxx, cpp)):
        run(name + '-object', [wrapper, '-O0', '-c', src, '-o', out / (name + '.o')])
        run(name + '-link', [wrapper, out / (name + '.o'), '-o', out / name])
        run(name + '-execute', [out / name])
        for mode, setting in (('harden', 'AFL_HARDEN'), ('asan', 'AFL_USE_ASAN')):
            binary = out / (name + '-' + mode)
            run(name + '-' + mode + '-build', [wrapper, '-O1', src, '-o', binary], {setting: '1'})
            run(name + '-' + mode + '-execute', [binary],
                {'ASAN_OPTIONS': 'detect_leaks=0:abort_on_error=1'})
    # MSAN needs PIE; execute if the runtime can map its shadow in this process.
    binary = out / 'c-msan'
    run('msan-build', [cc, '-O1', '-fPIE', '-pie', source, '-o', binary], {'AFL_USE_MSAN': '1'})
    run('msan-execute', [binary], {'MSAN_OPTIONS': 'exit_code=86', 'ASAN_OPTIONS': 'detect_leaks=0'})
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (out / 'result.json').write_text(json.dumps({'passed': True, 'cases': results,
        'coverage_tuple_counts': [len(x.splitlines()) for x in (first, second)],
        'path_compiler_resolutions': compilers, 'path_compiler_probes': probe_calls}, indent=2) + '\n')
    print(f'{len(results)} checks passed; coverage tuple counts: {[len(x.splitlines()) for x in (first, second)]}')


if __name__ == '__main__':
    main()
