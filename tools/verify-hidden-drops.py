"""Check the supported EXE's independent legendary path and all its callers."""
from pathlib import Path
import bisect
import hashlib
import json
import re
import struct
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

root = Path(__file__).resolve().parents[1]
game = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r'E:\SteamLibrary\steamapps\common\Crashlands\Crashlands.exe')
binary = game.read_bytes()
digest = hashlib.sha256(binary).hexdigest()
assert digest == 'a0fa1008f3f9903701765dc168c25c68bba95bda248899d60e325c6a1c9a4b4d'
pe = pefile.PE(data=binary)
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
header = (root / 'src/native/offsets.h').read_text()
offsets = {name: int(value, 16) for name, value in re.findall(r'constexpr uintptr_t (\w+)=(0x[\da-f]+);', header)}
functions = json.loads((root / 'artifacts/function-map.json').read_text())
variables = json.loads((root / 'artifacts/variable-map.json').read_text())
target = offsets['check_legendary_component_drop']
assert functions['gml_Script_check_legendary_component_drop']['rva'] == target == 0x2b6010
for name in ['ds_map_exists', 'ds_map_find_value', 'ds_map_set', 'LEGENDARY_TALLY', 'LEGENDARY_NODROP']:
    assert variables[hex(base + offsets['var_' + name])] == name

def double_at(rva):
    return struct.unpack_from('<d', binary, pe.get_offset_from_rva(rva))[0]

constants = {0x2038ea0: 1, 0x20390e0: 20, 0x2038b40: 0.0003, 0x23b66a0: -1}
for address, expected in constants.items():
    assert double_at(address) == expected
ret = offsets['legendary_chance_return']
off = pe.get_offset_from_rva(ret - 5)
roll = list(md.disasm(binary[off:off + 5], base + ret - 5))
assert len(roll) == 1 and roll[0].mnemonic == 'call' and roll[0].operands[0].imm == base + offsets['random_real']

ordered = sorted((entry['rva'], name) for name, entry in functions.items())
starts = [address for address, name in ordered]
ends = {entry.struct.BeginAddress: entry.struct.EndAddress for entry in pe.DIRECTORY_ENTRY_EXCEPTION}
expected = {
    'gml_Object_o_dusk_crystal_Other_11': 833,
    'gml_Object_o_fishinghole_bawg_Other_13': 830,
    'gml_Object_o_fishinghole_savanna_Other_13': 830,
    'gml_Object_o_fishinghole_tundra_Other_13': 830,
    'gml_Object_o_geysir_Other_11': 843,
    'gml_Object_o_jollychomp_Other_14': 832,
    'gml_Object_o_lyff_Other_12': 840,
    'gml_Object_o_sawgrass_Other_11': 831,
    'gml_Object_o_snorble_Other_12': 836,
    'gml_Object_o_spur_Other_11': 838,
    'gml_Object_o_tartil_Other_12': 109,
}
names = json.loads((game.parent / 'ui_text_en.json').read_text('utf-8'))['tags']['IN']['text']
zh_names = json.loads((game.parent / 'ui_text_zh-cn.json').read_text('utf-8'))['tags']['IN']['text']
sites = []
for section in pe.sections:
    if section.Name.rstrip(b'\0') != b'.text':
        continue
    raw = section.get_data()
    for match in re.finditer(b'\xe8', raw):
        pos = match.start()
        if pos + 5 > len(raw):
            continue
        ret = section.VirtualAddress + pos + 5
        if ret + struct.unpack_from('<i', raw, pos + 1)[0] != target:
            continue
        start, owner = ordered[bisect.bisect_right(starts, ret - 5) - 1]
        off = pe.get_offset_from_rva(start)
        instructions = list(md.disasm(binary[off:off + ends[start] - start], base + start))
        index = next(i for i, ins in enumerate(instructions) if ins.address == base + ret - 5)
        assert instructions[index].mnemonic == 'call' and instructions[index].operands[0].type == X86_OP_IMM
        # Each event passes a numeric item-ID constant through RV's constructor.
        constant = next(ins for ins in reversed(instructions[max(0, index - 30):index])
                        if ins.mnemonic == 'lea' and ins.op_str.startswith('rdx, [rip')
                        and ins.operands[1].type == X86_OP_MEM and ins.operands[1].mem.base == X86_REG_RIP)
        rva = constant.address + constant.size + constant.operands[1].mem.disp - base
        item = int(double_at(rva))
        assert expected[owner] == item
        sites.append({'event': owner, 'return_rva': hex(ret), 'item_id': item,
                      'name_en': names[str(item)], 'name_zh': zh_names[str(item)]})
assert len(sites) == len(expected) == 11
assert {site['event'] for site in sites} == set(expected)
assert len({site['item_id'] for site in sites}) == 9
report = {
    'game_sha256': digest, 'passed': True, 'native_function_rva': hex(target),
    'chance_return_rva': hex(offsets['legendary_chance_return']),
    'native_constants': {hex(address): value for address, value in constants.items()},
    'chance_formula': '(tally - 20) * 0.0003 * (1 + PLAYERSTATS[33])',
    'decompiled_source': 'artifacts/decompiled/gml_Script_check_legendary_component_drop.c',
    'sites': sites, 'unique_items': 9, 'in_game_test': False,
}
(root / 'artifacts/hidden-drop-evidence.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('PASS: legendary hook, RNG call, five variables, native constants and all 11 source calls / 9 items')
