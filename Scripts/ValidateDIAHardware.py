import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

from ValidateDIA import PROJECT, ROOT, environment, fingerprint

NS = 'http://schemas.microsoft.com/developer/msbuild/2003'


def project(source, harness):
    tree = ET.ElementTree(ET.fromstring(source.lstrip('\ufeff').replace('Tests', 'HardwareRTLifecycle')))
    for node in tree.getroot().iter():
        for child in list(node):
            if child.tag in ('{' + NS + '}ClCompile', '{' + NS + '}ClInclude') and 'Include' in child.attrib:
                node.remove(child)
    for node in tree.getroot().iter('{' + NS + '}ProjectGuid'):
        node.text = '{A3345A06-7E33-4A31-B1F4-6AE75E233671}'
    group = ET.SubElement(tree.getroot(), '{' + NS + '}ItemGroup')
    ET.SubElement(group, '{' + NS + '}ClCompile', {'Include': str(harness)})
    ET.indent(tree, space='  ')
    return tree


def self_check():
    source = f'<Project xmlns="{NS}"><PropertyGroup><RootNamespace>Tests</RootNamespace><ProjectGuid>old</ProjectGuid></PropertyGroup><ItemGroup><ClCompile Include="old.cpp"/><ClInclude Include="old.h"/></ItemGroup><ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp20</LanguageStandard></ClCompile></ItemDefinitionGroup><ItemGroup><ProjectReference Include="Engine.vcxproj"/></ItemGroup></Project>'
    tree = project(source, Path('HardwareRTLifecycle.cpp'))
    files = [n.attrib['Include'] for n in tree.getroot().iter('{' + NS + '}ClCompile') if 'Include' in n.attrib]
    assert files == ['HardwareRTLifecycle.cpp']
    assert next(tree.getroot().iter('{' + NS + '}LanguageStandard')).text == 'stdcpp20'
    assert next(tree.getroot().iter('{' + NS + '}ProjectReference')).attrib['Include'] == 'Engine.vcxproj'
    assert next(tree.getroot().iter('{' + NS + '}RootNamespace')).text == 'HardwareRTLifecycle'


def msbuild():
    cache = ROOT / 'Intermediate/HardwareRT/SDL-build/CMakeCache.txt'
    if cache.exists():
        match = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache.read_text(), re.MULTILINE)
        if match and Path(match[1].strip()).is_file():
            return Path(match[1].strip())
    locator = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if locator.exists():
        found = subprocess.check_output([str(locator), '-latest', '-prerelease', '-requires', 'Microsoft.Component.MSBuild', '-find', 'MSBuild/**/Bin/MSBuild.exe'], text=True).splitlines()
        if found:
            return Path(found[0])
    raise RuntimeError('MSBuild was not found')


def main():
    parser = argparse.ArgumentParser(description='Opt-in Vulkan hardware RT resource lifecycle test. Requires the patched SDL SDK and a generated Release engine project.')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--lifecycle', action='store_true', help='Build and run the real GPU cancellation, submission, descriptor lifetime and timestamp checks.')
    mode.add_argument('--self-check', action='store_true', help='Check the project generator without building or accessing the GPU.')
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print('PASS hardware lifecycle runner self-check')
        return
    tests = PROJECT / 'Build/Tests.vcxproj'
    if not tests.exists():
        parser.error('Generate the Windows projects before running the hardware lifecycle test')
    output = ROOT / 'Intermediate/DIAEvidence' / (time.strftime('%Y%m%d-%H%M%S') + '-hardware-lifecycle')
    output.mkdir(parents=True)
    report = {'status': 'RUNNING', 'scope': 'Opt-in real Vulkan AS build, cancellation, cross-command reuse, active-pass retention and timestamp lifecycle. Lighting image comparisons are separate.', 'environment': environment(), 'validation_requested': True}
    path = output / 'results.json'
    print('Evidence:', output, flush=True)
    try:
        ET.register_namespace('', NS)
        generated = PROJECT / 'Build/HardwareRTLifecycle.vcxproj'
        project(tests.read_text(encoding='utf-8-sig'), ROOT / 'Scripts/HardwareRTLifecycle.cpp').write(generated, encoding='utf-8', xml_declaration=True)
        command = [str(msbuild()), str(generated), '/m:1', '/p:Configuration=Release', '/p:Platform=x64', '/verbosity:minimal', '/nologo']
        with (output / 'build.log').open('w') as stream:
            built = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=1800)
        report['build_exit_code'] = built.returncode
        assert built.returncode == 0, 'Hardware lifecycle build failed'
        executable = PROJECT / 'Binaries/Release/HardwareRTLifecycle.exe'
        report['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
        report['source_hashes'] = fingerprint()
        env = os.environ.copy()
        env['FP_DIA_RAY_BACKEND'] = 'hardware'
        env['FP_DIA_GPU_TIMING'] = '1'
        env['FP_GPU_VALIDATION'] = '1'
        env['VK_LAYER_VALIDATE_SYNC'] = '1'
        env.pop('VK_LAYER_ENABLES', None)
        with (output / 'runtime.log').open('w') as stream:
            run = subprocess.run([str(executable)], cwd=PROJECT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=240)
        text = (output / 'runtime.log').read_text()
        report['runtime_exit_code'] = run.returncode
        report['checks'] = [line for line in text.splitlines() if line.startswith('PASS ')]
        assert run.returncode == 0 and 'PASS lifecycle,' in text, 'Hardware lifecycle execution failed'
        assert not re.search(r'VUID-|Validation Error|SYNC-HAZARD|\[ERROR\]', text, re.IGNORECASE), 'Vulkan validation reported an error'
        assert fingerprint() == report['source_hashes'], 'Build inputs changed during the lifecycle test'
        report['status'] = 'PASS'
    except Exception as error:
        report['status'] = 'FAIL'
        report['failure'] = repr(error)
        raise
    finally:
        path.write_text(json.dumps(report, indent=2) + '\n')
        print(report['status'], 'Evidence:', output, flush=True)


if __name__ == '__main__':
    main()
