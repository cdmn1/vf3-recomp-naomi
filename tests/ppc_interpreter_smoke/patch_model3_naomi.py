#!/usr/bin/env python3
"""Patch pinned upstream Model 3 ROM accesses for Naomi NetDIMM streaming."""
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_model3_naomi.py path/to/model3.cpp")

path = pathlib.Path(sys.argv[1])
source = path.read_text()

include_anchor = '#include "model3.h"'
include_patch = include_anchor + '\n#include "cart_vrom_bridge.h"'
if 'cart_vrom_bridge.h' not in source:
    if source.count(include_anchor) != 1:
        raise SystemExit("expected exactly one model3.h include")
    source = source.replace(include_anchor, include_patch, 1)

def replace_once(old, new, label):
    global source
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit("expected exactly one upstream " + label)
    source = source.replace(old, new, 1)

replace_once(
"""void Model3::r3d_texture_port(unsigned, uint32_t data)
{
	// Step 1.0: the data is a VROM word address; the words there go into the texture FIFO
	const uint32_t *v = reinterpret_cast<const uint32_t *>(vrom.data());
	uint32_t addr = data & 0xFFFFFF;
	uint32_t n = (2 + v[addr] / 2) / 4;
	for (uint32_t i = 0; i < n && tex_fifo.size() < 0x100000 / 4; i++)   // (the same words; a count of millions, a VROM word a program in a snapshot points at, no longer run through once the FIFO's full)
		tex_fifo.push_back(v[(addr + i) & 0xFFFFFF]);
}""",
"""void Model3::r3d_texture_port(unsigned, uint32_t data)
{
#ifdef VF3_NAOMI
	if (naomi_model3_vrom_port(data, &tex_fifo))
		return;
#endif
	const uint32_t *v = reinterpret_cast<const uint32_t *>(vrom.data());
	uint32_t addr = data & 0xFFFFFF;
	uint32_t n = (2 + v[addr] / 2) / 4;
	for (uint32_t i = 0; i < n && tex_fifo.size() < 0x100000 / 4; i++)
		tex_fifo.push_back(v[(addr + i) & 0xFFFFFF]);
}""", "r3d_texture_port")

replace_once(
"""void Model3::set_crom_bank(uint8_t v)
{
	crom_bank_reg = v;
	crom_bank = banked.data() + size_t((~v) & 0xF) * 0x800000;
}""",
"""void Model3::set_crom_bank(uint8_t v)
{
	crom_bank_reg = v;
#ifdef VF3_NAOMI
	if (naomi_model3_crom_select_bank(v))
		return;
#endif
	crom_bank = banked.data() + size_t((~v) & 0xF) * 0x800000;
}""", "set_crom_bank")

replace_once(
"""uint16_t Model3::peek16(uint32_t a) const
{
	if (a < 0x00800000) return *reinterpret_cast<const uint16_t *>(&ram[(a & 0x7FFFFE) ^ 2]);
	if ((a >> 24) == 0xFF) return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFE) ^ 2]);
	return 0;
}""",
"""uint16_t Model3::peek16(uint32_t a) const
{
	if (a < 0x00800000) return *reinterpret_cast<const uint16_t *>(&ram[(a & 0x7FFFFE) ^ 2]);
	if ((a >> 24) == 0xFF) {
#ifdef VF3_NAOMI
		if (a < 0xFF800000 && naomi_model3_crom_is_bound())
			return naomi_model3_crom_read16((a & 0x7FFFFE) ^ 2);
#endif
		return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFE) ^ 2]);
	}
	return 0;
}""", "peek16")

replace_once(
"	case 0xFF: return (a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 3];",
"""	case 0xFF:
#ifdef VF3_NAOMI
		if (a < 0xFF800000 && naomi_model3_crom_is_bound())
			return naomi_model3_crom_read8((a & 0x7FFFFF) ^ 3);
#endif
		return (a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 3];""", "read8 CROM")

replace_once(
"	case 0xFF: return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 2]);",
"""	case 0xFF:
#ifdef VF3_NAOMI
		if (a < 0xFF800000 && naomi_model3_crom_is_bound())
			return naomi_model3_crom_read16((a & 0x7FFFFF) ^ 2);
#endif
		return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 2]);""", "read16 CROM")

replace_once(
"	case 0xFF: return *reinterpret_cast<const uint32_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[a & 0x7FFFFF]);",
"""	case 0xFF:
#ifdef VF3_NAOMI
		if (a < 0xFF800000 && naomi_model3_crom_is_bound())
			return naomi_model3_crom_read32(a & 0x7FFFFF);
#endif
		return *reinterpret_cast<const uint32_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[a & 0x7FFFFF]);""", "read32 CROM")

path.write_text(source)
