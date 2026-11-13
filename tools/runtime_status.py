import ctypes,struct,json,sys
from pathlib import Path
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenFileMappingW.restype=ctypes.c_void_p
k.MapViewOfFile.restype=ctypes.c_void_p
k.MapViewOfFile.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint,ctypes.c_size_t]
k.CloseHandle.argtypes=[ctypes.c_void_p];k.UnmapViewOfFile.argtypes=[ctypes.c_void_p]
pid=int(sys.argv[1]);h=k.OpenFileMappingW(6,False,'Local\\CrashlandsTrainer_v2_'+str(pid))
if not h:h=k.OpenFileMappingW(6,False,'Local\\CrashlandsTrainer_v1_'+str(pid))
if not h:raise OSError(ctypes.get_last_error(),'No trainer session')
p=k.MapViewOfFile(h,6,0,0,4096)
if len(sys.argv)>2:ctypes.c_int.from_address(p+int(sys.argv[2])).value=int(sys.argv[3])
b=ctypes.string_at(p,640)
out=dict(zip(['magic','version','flags','status','heartbeat','hooks'],struct.unpack_from('<6I',b)))
count=7 if out['version']>=3 else 6 if out['version']==2 else 5
out['hits']=struct.unpack_from('<'+str(count)+'q',b,24)
offset=24+8*count
out.update(dict(zip(['item','amount','quality','affixes','speed'],struct.unpack_from('<qdiid',b,offset))))
out['test_status']=struct.unpack_from('<i',b,offset+36)[0]
out['detail']=b[offset+40:].split(b'\0')[0].decode('utf8',errors='replace')
print(json.dumps(out,indent=2))
k.UnmapViewOfFile(p);k.CloseHandle(h)
