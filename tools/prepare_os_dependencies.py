#!/usr/bin/env python3
"""Resolve the verified NFS release without consuming another task's uncommitted library edits."""
import io,os,shutil,subprocess,tarfile,tempfile,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
NFS_REVISION='3ed1433f4b6b5852e54350b1b51c5dc0b02bbc7a' # ESP32NFS v1.3.0

def prepare_nfs():
    explicit=os.environ.get('ESP32_NFS_LIBRARY')
    if explicit:return Path(explicit).resolve()
    git='/Library/Developer/CommandLineTools/usr/bin/git'
    if not Path(git).exists():git=shutil.which('git') or 'git'
    repo=ROOT/'libraries/ESP32NFS'
    if not repo.exists():
        cache=ROOT/'build/source-cache/ESP32NFS';cache.parent.mkdir(parents=True,exist_ok=True)
        if not cache.exists():subprocess.run([git,'clone','--filter=blob:none','--no-checkout','https://github.com/cccat6/ESP32NFS.git',str(cache)],check=True)
        repo=cache
    actual=subprocess.check_output([git,'-C',str(repo),'rev-parse',NFS_REVISION+'^{commit}'],text=True).strip()
    if actual!=NFS_REVISION:raise RuntimeError('Pinned ESP32NFS commit unavailable')
    parent=ROOT/'build/dependencies';parent.mkdir(parents=True,exist_ok=True)
    target=parent/('ESP32NFS-'+actual[:12]);marker=target/'.source_revision'
    if marker.exists() and marker.read_text().strip()==actual:return target
    data=subprocess.check_output([git,'-C',str(repo),'archive','--format=tar','--prefix=ESP32NFS/',actual])
    with tempfile.TemporaryDirectory(prefix='nfs-release-',dir=parent) as tmp:
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            archive.extractall(tmp,filter='data')
        candidate=Path(tmp)/'ESP32NFS';(candidate/'.source_revision').write_text(actual+'\n')
        try:candidate.rename(target)
        except OSError:
            if not marker.exists() or marker.read_text().strip()!=actual:raise
    return target
if __name__=='__main__':print(prepare_nfs())
