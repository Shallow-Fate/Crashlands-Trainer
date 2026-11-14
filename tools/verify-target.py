"""Verify all runtime address assumptions against the supported original EXE."""
from pathlib import Path
import hashlib,json,re,struct,sys
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64

path=Path(sys.argv[1] if len(sys.argv)>1 else r'E:\SteamLibrary\steamapps\common\Crashlands\Crashlands.exe')
b=path.read_bytes();p=pefile.PE(data=b,fast_load=True);base=p.OPTIONAL_HEADER.ImageBase
assert hashlib.sha256(b).hexdigest()=='a0fa1008f3f9903701765dc168c25c68bba95bda248899d60e325c6a1c9a4b4d'
assert p.FILE_HEADER.TimeDateStamp==0x68f7b82a and p.OPTIONAL_HEADER.SizeOfImage==0x28a7000
md=Cs(CS_ARCH_X86,CS_MODE_64)
sites={0xebdab2:0x91570,0x2aec57:0x16b9a90,0x2b0913:0x16b9a90,0x2adbf8:0x16b9a90,0x3f655a:0x16b9a90,0x2b0af1:0x8b5f40,0x4930fa:0x1673bd0,0xba43e0:0x48ec10,0x9a42da:0x48ec10,0x2b6968:0x16b9a90}
checks=[]
for ret,target in sites.items():
    off=p.get_offset_from_rva(ret-5)
    ins=list(md.disasm(b[off:off+5],base+ret-5))
    assert len(ins)==1 and ins[0].mnemonic=='call' and int(ins[0].op_str,16)==base+target,hex(ret)
    checks.append({'return_rva':hex(ret),'call_target_rva':hex(target),'verified':True})
# Rerolls pay the normal cost, replace the existing equipment through the
# four-argument generator, then refresh the equipped stats and inventory.
header=Path('src/native/offsets.h').read_text()
def offset(name):
    return int(re.search(r'\b'+name+r'\s*=\s*(0x[0-9a-fA-F]+)',header).group(1),16)
assert offset('reforge_generate_return')==0x9a42da
assert offset('station_generate_return')==0xba43e0
assert offset('generate_randomized_equipment')==0x48ec10
start,end=0x9a3970,0x9a441f
flow=list(md.disasm(b[p.get_offset_from_rva(start):p.get_offset_from_rva(end)],base+start))
required=[('can_reroll',0x28b750),('reroll_juice_cost',0x9a4520),('lose_stackable_item',0x8dd8f0),('generate_randomized_equipment',0x48ec10),('update_character_equipment',0xc084c0),('reload_all_inventory_slot_item',0x98a940)]
calls=[i for i in flow if i.mnemonic=='call' and i.op_str.startswith('0x')]
positions=[next(j for j,i in enumerate(calls) if int(i.op_str,16)==base+target) for _,target in required]
assert positions==sorted(positions)
generator=next(j for j,i in enumerate(flow) if i.address==base+0x9a42d5)
assert any(i.mnemonic=='mov' and i.op_str=='r9d, 4' for i in flow[generator-6:generator])
reforge={'function_rva':hex(start),'generator_return_rva':hex(offset('reforge_generate_return')),'generator_argc':4,'native_call_order':[name for name,_ in required],'verified':True}
report={'game_sha256':hashlib.sha256(b).hexdigest(),'steam_build':'20483884','version':'1.5.143-rc.0 / Publish 100.0.165.0','sites':checks,'juiceforge_reroll':reforge,'passed':True}
Path('artifacts/target-validation.json').write_text(json.dumps(report,indent=2))
print(f'PASS: game identity, all {len(sites)} context-specific native call sites, and Juiceforge reroll flow')
