"""Real loopback TCP / NFSv3 exchange through the OS provider adapter, no device access."""
import socket, struct, subprocess, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def u(*values): return struct.pack('>'+'I'*len(values),*values)
def opaque(b): return u(len(b))+b+b'\0'*((-len(b))%4)
class SocketTest(unittest.TestCase):
 def test_mount_lookup_read_readonly(self):
  with socket.socket() as reserve:
   reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
  proc=subprocess.Popen([str(ROOT/'build/os-nfs-socket'),str(port)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   self.assertEqual(proc.stdout.readline().strip(),'ready')
   with socket.create_connection(('127.0.0.1',port),timeout=5) as conn:
    def exact(n):
     data=b''
     while len(data)<n:
      part=conn.recv(n-len(data));self.assertTrue(part);data+=part
     return data
    def rpc(xid,program,procedure,args):
     body=u(xid,0,2,program,3,procedure,0,0,0,0)+args
     framed=u(len(body)|0x80000000)+body
     # Exercise split record header and split RPC body over a real socket.
     conn.sendall(framed[:2]);conn.sendall(framed[2:13]);conn.sendall(framed[13:])
     size=struct.unpack('>I',exact(4))[0];self.assertTrue(size&0x80000000)
     reply=exact(size&0x7fffffff);self.assertEqual(reply[:24],u(xid,1,0,0,0,0));return reply[24:]
    r=rpc(1,100005,1,opaque(b'/sd'));self.assertEqual(r[:4],u(0));n=struct.unpack('>I',r[4:8])[0];root=r[8:8+n]
    r=rpc(2,100003,3,opaque(root)+opaque(b'hello.txt'));self.assertEqual(r[:4],u(0));n=struct.unpack('>I',r[4:8])[0];handle=r[8:8+n]
    r=rpc(3,100003,6,opaque(handle)+struct.pack('>Q',0)+u(4096));self.assertEqual(r[:8],u(0,1));count,eof,n=struct.unpack('>III',r[92:104]);self.assertEqual(r[104:104+n],b'Notes OS NFS bridge');self.assertEqual((count,eof),(n,1))
    r=rpc(4,100003,7,opaque(handle)+struct.pack('>Q',0)+u(1,2)+opaque(b'X'));self.assertEqual(r[:4],u(30))
  finally:
   proc.terminate();proc.communicate(timeout=5)
if __name__=='__main__':unittest.main()
