from pathlib import Path
import json, re, struct, sys
import pefile
from capstone import *
from capstone.x86 import *

GAME = Path(r'E:\SteamLibrary\steamapps\common\Crashlands')
b = (GAME/'Crashlands.exe').read_bytes()
pe = pefile.PE(data=b, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
base = pe.OPTIONAL_HEADER.ImageBase
strings = {}
for s in pe.sections:
    raw=s.PointerToRawData
    for m in re.finditer(rb'[\x20-\x7e]{4,}',b[raw:raw+s.SizeOfRawData]):
        strings[base+s.VirtualAddress+m.start()]=m.group().decode('ascii')
funcs = {}
variables = {}
for section in pe.sections:
    if section.Name.rstrip(b'\0') != b'.data': continue
    for o in range(section.PointerToRawData, section.PointerToRawData+section.SizeOfRawData-24, 8):
        s, f, z = struct.unpack_from('<QQQ',b,o)
        if s in strings and strings[s].startswith(('gml_Script_', 'gml_Object_', 'gml_GlobalScript_')) and base+0x1000 <= f < base+0x1d63000:
            funcs[strings[s]] = {'va':f, 'rva':f-base,'offset':pe.get_offset_from_rva(f-base), 'table':o}
        if s in strings and f in (0,0xffffffff):
            variables[base+pe.get_rva_from_offset(o)+8]=strings[s]
Path('artifacts/function-map.json').write_text(json.dumps(funcs,indent=2))
Path('artifacts/variable-map.json').write_text(json.dumps({hex(k):v for k,v in variables.items()},indent=2))
byva = {v['va']:n for n,v in funcs.items()}
ends = {base+x.struct.BeginAddress:base+x.struct.EndAddress for x in pe.DIRECTORY_ENTRY_EXCEPTION}
md = Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True

def label(va):
    if va in byva: return byva[va]
    if va in strings: return repr(strings[va])
    if va in variables: return 'var_'+variables[va]
    try:
        o=pe.get_offset_from_rva(va-base)
        val=struct.unpack_from('<d',b,o)[0]
        q=struct.unpack_from('<Q',b,o)[0]
        if q in strings:return '&'+repr(strings[q])
        if q in byva:return '&'+byva[q]
        return f'double={val:.8g} qword={q:#x}'
    except:return ''

def dump(name):
    item=funcs[name]; start=item['va']; end=ends.get(start,start+0x10000)
    lines=[f'{name} {start:#x} size={end-start:#x}']
    for i in md.disasm(b[item['offset']:item['offset']+end-start],start):
        notes=[]
        for op in i.operands:
            if op.type==X86_OP_IMM and op.imm>=base: notes.append(label(op.imm))
            if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
                va=i.address+i.size+op.mem.disp
                notes.append(f'{va:#x} '+label(va))
        lines.append(f'{i.address-base:08x} {i.mnemonic:8} {i.op_str:55} '+ ' ; '.join(notes))
    out=Path('artifacts/disasm');out.mkdir(exist_ok=True)
    (out/(name+'.asm')).write_text('\n'.join(lines))
    print(lines[0])

if __name__=='__main__':
    if len(sys.argv)>1:
        for term in sys.argv[1:]:
            for name in funcs:
                if term in name: dump(name)
    else: print('Mapped',len(funcs),'functions')
