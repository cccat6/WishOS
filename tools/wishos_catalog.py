#!/usr/bin/env python3
"""One dependency graph for target selection, local builds and CI."""
import argparse
import json
import os
import subprocess
from pathlib import Path
from prepare_board_package import ROOT, HARDWARE, load, driver_packages, runtime_package, asset_sources, platform_package
from prepare_arduino import lock as dependency_lock

def catalog():
    result={}
    for file in sorted((HARDWARE/'boards').glob('*/board.json')):
        directory,board,port_directory,port,_=load(file.parent)
        if board['id']!=file.parent.name or board['id'] in result:raise ValueError('catalog directory/id mismatch')
        drivers=driver_packages(board,port)
        runtime,_=runtime_package(directory,board)
        paths={directory,port_directory,platform_package(board)[0],runtime,*asset_sources(directory,board).values()}
        paths.update(path for path,_ in drivers.values())
        dependencies=set(port.get('dependencies',[]))
        for _,driver in drivers.values():
            dependencies.update(driver.get('dependencies',[]))
            paths.update((ROOT/file).resolve() for file in driver.get('build_inputs',[]))
        for name in dependencies:
            spec=dependency_lock()['libraries'].get(name,{})
            if spec.get('patch'):paths.add((ROOT/'tools/dependencies'/spec['patch']).resolve())
        result[board['id']]={
            'directory':directory,'board':board,'port':port,'drivers':drivers,
            'dependencies':sorted(dependencies),'paths':{path.resolve() for path in paths},
        }
    return result

def select(changed=None, board=None, entries=None, previous_lock=None):
    entries=catalog() if entries is None else entries
    if board:
        if board=='all':return sorted(entries)
        if board not in entries:raise ValueError('unknown board: '+board)
        return [board]
    if changed is None:return sorted(entries)
    common=('firmware/WishOS/','.github/workflows/')
    build_tools={'tools/build_wishos.py','tools/build_wishos.sh','tools/prepare_board_package.py',
                 'tools/wishos_catalog.py','tools/flash_layout.py','tools/prepare_filesystem.py','tools/dependencies/filesystem.json','tools/prepare_os_dependencies.py','tools/prepare_library_snapshot.py',
                 'tools/prepare_arduino.py','tools/release_wishos.py'}
    selected=set()
    for name in changed:
        name=Path(name).as_posix()
        if name.startswith(common) or name in build_tools:return sorted(entries)
        if name=='tools/dependencies/arduino.json':
            current=dependency_lock()
            if previous_lock is None or any(current.get(k)!=previous_lock.get(k) for k in ('format','cli_version')):return sorted(entries)
            changed_libs={key for key in set(current['libraries'])|set(previous_lock['libraries']) if current['libraries'].get(key)!=previous_lock['libraries'].get(key)}
            changed_chains={key for key in set(current['toolchains'])|set(previous_lock.get('toolchains',{})) if current['toolchains'].get(key)!=previous_lock.get('toolchains',{}).get(key)}
            selected.update(id for id,e in entries.items() if changed_libs.intersection(e['dependencies']) or e['port']['toolchain'] in changed_chains)
        path=(ROOT/name).resolve()
        for id,entry in entries.items():
            if any(path==dependency or path.is_relative_to(dependency) for dependency in entry['paths']):selected.add(id)
    return sorted(selected)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--board');parser.add_argument('--base');parser.add_argument('--head',default='HEAD')
    parser.add_argument('--changed',nargs='*');parser.add_argument('--github-output')
    args=parser.parse_args()
    changes=args.changed
    previous_lock=None
    if args.base:
        # --no-renames includes both the removed and added path, including moved shared assets.
        git='/Library/Developer/CommandLineTools/usr/bin/git' if Path('/Library/Developer/CommandLineTools/usr/bin/git').exists() else 'git'
        changes=subprocess.check_output([git,'diff','--name-only','--no-renames','-z',args.base,args.head],cwd=ROOT).decode().split('\0')
        changes=[p for p in changes if p]
        previous=subprocess.run([git,'show',args.base+':tools/dependencies/arduino.json'],cwd=ROOT,text=True,capture_output=True)
        if previous.returncode==0:previous_lock=json.loads(previous.stdout)
    boards=select(changes,args.board,previous_lock=previous_lock)
    matrix={'include':[{'board':id} for id in boards]}
    print(json.dumps({'boards':boards,'matrix':matrix},separators=(',',':')))
    if args.github_output:
        with open(args.github_output,'a') as stream:
            stream.write('matrix='+json.dumps(matrix,separators=(',',':'))+'\n')
            stream.write('has_targets='+str(bool(boards)).lower()+'\n')

if __name__=='__main__':main()
