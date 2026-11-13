"""Verify all runtime address assumptions against the supported original EXE."""
from pathlib import Path
import hashlib,json,struct,sys
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64

path=Path(sys.argv[1] if len(sys.argv)>1 else r'E:\SteamLibrary\steamapps\common\Crashlands\Crashlands.exe')
b=path.read_bytes();p=pefile.PE(data=b,fast_load=True);base=p.OPTIONAL_HEADER.ImageBase
assert hashlib.sha256(b).hexdigest()=='a0fa1008f3f9903701765dc168c25c68bba95bda248899d60e325c6a1c9a4b4d'
assert p.FILE_HEADER.TimeDateStamp==0x68f7b82a and p.OPTIONAL_HEADER.SizeOfImage==0x28a7000
md=Cs(CS_ARCH_X86,CS_MODE_64)
sites={0xebdab2:0x91570,0x2aec57:0x16b9a90,0x2b0913:0x16b9a90,0x2adbf8:0x16b9a90,0x3f655a:0x16b9a90,0x2b0af1:0x8b5f40,0x4930fa:0x1673bd0,0xba43e0:0x48ec10,0x2b6968:0x16b9a90}
checks=[]
for ret,target in sites.items():
    off=p.get_offset_from_rva(ret-5)
    ins=list(md.disasm(b[off:off+5],base+ret-5))
    assert len(ins)==1 and ins[0].mnemonic=='call' and int(ins[0].op_str,16)==base+target,hex(ret)
    checks.append({'return_rva':hex(ret),'call_target_rva':hex(target),'verified':True})
report={'game_sha256':hashlib.sha256(b).hexdigest(),'steam_build':'20483884','version':'1.5.143-rc.0 / Publish 100.0.165.0','sites':checks,'passed':True}
Path('artifacts/target-validation.json').write_text(json.dumps(report,indent=2))
print(f'PASS: game identity and all {len(sites)} context-specific native call sites')
