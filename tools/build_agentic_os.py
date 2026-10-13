#!/usr/bin/env python3
"""Build the portable OS with a selected board/port/assets package."""
import os,shutil,subprocess,sys,tempfile,hashlib
from pathlib import Path
from prepare_radio_sdk import prepare
from prepare_os_dependencies import prepare_nfs
from prepare_board_package import stage as stage_board, DEFAULT
ROOT=Path(__file__).resolve().parents[1]
cli=os.environ.get('ARDUINO_CLI','/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli')
stage=Path(os.environ.get('NOTES_OS_STAGE',ROOT/'build/agentic-os-sketch/AgenticOS'))
build=Path(os.environ.get('NOTES_OS_BUILD',ROOT/'build/agentic-os'))
config=['--config-file',os.environ['ARDUINO_CONFIG_FILE']] if os.environ.get('ARDUINO_CONFIG_FILE') else []
package=Path(os.environ.get('NOTES_OS_BOARD',str(DEFAULT)))
if not package.exists():package=ROOT/'hardware/agent-os/boards'/package
stage.mkdir(parents=True,exist_ok=True)
# Generated sketch only; copy exactly the public firmware.
if (stage/'src').exists():shutil.rmtree(stage/'src')
shutil.copytree(ROOT/'firmware/AgenticOS/src',stage/'src',copy_function=shutil.copy2)
shutil.copy2(ROOT/'firmware/AgenticOS/AgenticOS.ino',stage/'AgenticOS.ino')
board,port=stage_board(package,stage)
for relative in port.get('exclude_sources',[]):
    excluded=(stage/'src'/relative).resolve()
    if not excluded.is_relative_to((stage/'src').resolve()):raise ValueError('invalid excluded source')
    if excluded.is_dir():shutil.rmtree(excluded)
    elif excluded.exists():excluded.unlink()
flags=' '.join(port.get('flags',[]));fqbn=port['fqbn']
if not port.get('radio_mux') and (stage/'src/radio').exists():shutil.rmtree(stage/'src/radio')
opt='\n'.join(flags.split()+['-DLV_CONF_INCLUDE_SIMPLE',f'-I"{stage}/src"'])+'\n'
if not (stage/'build_opt.h').exists() or (stage/'build_opt.h').read_text()!=opt:(stage/'build_opt.h').write_text(opt)
common=[cli,'compile',*config,'--library',str(prepare_nfs()),'--fqbn',fqbn]
props=subprocess.check_output([*common,'--show-properties=expanded',str(stage)],text=True)
p=dict(line.split('=',1) for line in props.splitlines() if '=' in line)
radio=Path(tempfile.gettempdir())/('notes-radio-sdk-'+hashlib.sha256(str(ROOT).encode()).hexdigest()[:10])
if any(c.isspace() for c in str(radio)):raise RuntimeError('Set TMPDIR to a path without spaces for SDK response files')
response=prepare(Path(p['compiler.sdk.path']),Path(p['compiler.path']),radio) if port.get('radio_mux') else None
command=[*common,'--build-property','tools.ctags.pattern=/usr/bin/true',
 '--build-property',f'compiler.cpp.extra_flags={flags}',
 *(['--build-property',f'compiler.c.elf.libs=@{response}','--build-property',f'build.zigbee_libs=-lesp_zb_api.zczr -lzboss_stack.zczr {radio}/libzboss_port.native.a'] if response else []),
 '--build-property',f'upload.maximum_size={port["application_bytes"]}',
 '--warnings','none','--build-path',str(build),str(stage)]
subprocess.run(command,check=True)
