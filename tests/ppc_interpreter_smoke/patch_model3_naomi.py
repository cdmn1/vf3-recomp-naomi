#!/usr/bin/env python3
"""Patch the pinned upstream Model 3 core for Naomi NetDIMM ROM access."""
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


def replace_in_function(start_marker, end_marker, old, new, label):
    global source
    start = source.find(start_marker)
    if start < 0:
        raise SystemExit("missing upstream function " + start_marker)
    end = source.find(end_marker, start + len(start_marker))
    if end < 0:
        raise SystemExit("missing end marker for " + start_marker)
    body = source[start:end]
    if new in body:
        return
    if body.count(old) != 1:
        raise SystemExit("expected exactly one " + label + " in " + start_marker)
    body = body.replace(old, new, 1)
    source = source[:start] + body + source[end:]


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
"	cpu.attach(this, ram.data(), crom.data());",
"""#ifdef VF3_NAOMI
	const uint8_t *fixed_crom = naomi_model3_crom_fixed();
	cpu.attach(this, ram.data(), fixed_crom ? fixed_crom : crom.data());
#else
	cpu.attach(this, ram.data(), crom.data());
#endif""", "CPU CROM attach")

replace_once(
"""std::vector<uint32_t> Model3::program_image() const
{
	// The boot copies the program from the fixed CROM to RAM 0 (Revision D's: 0xA0000 bytes from 0x710000,
	// docs/step1-measure.md)
	std::vector<uint32_t> img(profile->program_size / 4);
	memcpy(img.data(), &crom[profile->program_at], profile->program_size);
	return img;
}""",
"""std::vector<uint32_t> Model3::program_image() const
{
	// The boot copies the program from the fixed CROM to RAM 0.
	std::vector<uint32_t> img(profile->program_size / 4);
	const uint8_t *fixed = crom.data();
#ifdef VF3_NAOMI
	const uint8_t *netdimm_fixed = naomi_model3_crom_fixed();
	if (netdimm_fixed) fixed = netdimm_fixed;
#endif
	memcpy(img.data(), fixed + profile->program_at, profile->program_size);
	return img;
}""", "program_image")

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
		if (naomi_model3_crom_is_bound()) {
			if (a < 0xFF800000)
				return naomi_model3_crom_read16((a & 0x7FFFFE) ^ 2);
			return *reinterpret_cast<const uint16_t *>(naomi_model3_crom_fixed() + ((a & 0x7FFFFE) ^ 2));
		}
#endif
		return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFE) ^ 2]);
	}
	return 0;
}""", "peek16")

replace_once(
"	case 0xFF: return (a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 3];",
"""	case 0xFF:
#ifdef VF3_NAOMI
		if (naomi_model3_crom_is_bound()) {
			if (a < 0xFF800000)
				return naomi_model3_crom_read8((a & 0x7FFFFF) ^ 3);
			return naomi_model3_crom_fixed()[(a & 0x7FFFFF) ^ 3];
		}
#endif
		return (a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 3];""", "read8 CROM")

replace_once(
"	case 0xFF: return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 2]);",
"""	case 0xFF:
#ifdef VF3_NAOMI
		if (naomi_model3_crom_is_bound()) {
			if (a < 0xFF800000)
				return naomi_model3_crom_read16((a & 0x7FFFFF) ^ 2);
			return *reinterpret_cast<const uint16_t *>(naomi_model3_crom_fixed() + ((a & 0x7FFFFF) ^ 2));
		}
#endif
		return *reinterpret_cast<const uint16_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[(a & 0x7FFFFF) ^ 2]);""", "read16 CROM")

replace_once(
"	case 0xFF: return *reinterpret_cast<const uint32_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[a & 0x7FFFFF]);",
"""	case 0xFF:
#ifdef VF3_NAOMI
		if (naomi_model3_crom_is_bound()) {
			if (a < 0xFF800000)
				return naomi_model3_crom_read32(a & 0x7FFFFF);
			return *reinterpret_cast<const uint32_t *>(naomi_model3_crom_fixed() + (a & 0x7FFFFF));
		}
#endif
		return *reinterpret_cast<const uint32_t *>(&(a < 0xFF800000 ? crom_bank : crom.data())[a & 0x7FFFFF]);""", "read32 CROM")

replace_once(
"""int Model3::hide_free_play_text()
{
	if (!free_play_text.empty()) return int(free_play_text.size());   // (hidden already)""",
"""int Model3::hide_free_play_text()
{
#ifdef VF3_NAOMI
	uint8_t *fixed_crom = naomi_model3_crom_fixed_mutable();
	size_t fixed_crom_size = fixed_crom ? 0x800000u : crom.size();
	if (!fixed_crom) fixed_crom = crom.data();
#else
	uint8_t *fixed_crom = crom.data();
	size_t fixed_crom_size = crom.size();
#endif
	if (!free_play_text.empty()) return int(free_play_text.size());   // (hidden already)""", "hide_free_play_text entry")

replace_in_function(
"int Model3::hide_free_play_text()", "int Model3::set_dural",
"	auto at = [&](size_t a) -> uint8_t & { return crom[(a + CROM_AT) ^ 3]; };",
"	auto at = [&](size_t a) -> uint8_t & { return fixed_crom[(a + CROM_AT) ^ 3]; };",
"hide_free_play_text CROM pointer")
replace_in_function(
"int Model3::hide_free_play_text()", "int Model3::set_dural",
"			if (a + CROM_AT + k >= fixed_crom_size || at(a + k) != uint8_t(t[k])) return false;",
"		if (a + CROM_AT + k >= fixed_crom_size || at(a + k) != uint8_t(t[k])) return false;",
"hide_free_play_text CROM bounds")

replace_once(
"""int Model3::set_dural(bool on, bool offline, bool legacy)
{
	const size_t CROM_AT = profile->program_at;""",
"""int Model3::set_dural(bool on, bool offline, bool legacy)
{
#ifdef VF3_NAOMI
	uint8_t *fixed_crom = naomi_model3_crom_fixed_mutable();
	size_t fixed_crom_size = fixed_crom ? 0x800000u : crom.size();
	if (!fixed_crom) fixed_crom = crom.data();
#else
	uint8_t *fixed_crom = crom.data();
	size_t fixed_crom_size = crom.size();
#endif
	const size_t CROM_AT = profile->program_at;""", "set_dural entry")

replace_in_function(
"int Model3::set_dural(bool on, bool offline, bool legacy)", "void Model3::show_free_play_text",
"	auto at = [&](size_t a) -> uint8_t & { return crom[(a + CROM_AT) ^ 3]; };",
"	auto at = [&](size_t a) -> uint8_t & { return fixed_crom[(a + CROM_AT) ^ 3]; };",
"set_dural CROM pointer")
replace_in_function(
"int Model3::set_dural(bool on, bool offline, bool legacy)", "void Model3::show_free_play_text",
"	if (!d || crom.size() < CROM_AT + profile->program_size) return 0;",
"	if (!d || fixed_crom_size < CROM_AT + profile->program_size) return 0;",
"set_dural CROM bounds")

replace_once(
"""void Model3::show_free_play_text()
{
	for (auto &[a, bytes] : free_play_text)""",
"""void Model3::show_free_play_text()
{
#ifdef VF3_NAOMI
	uint8_t *fixed_crom = naomi_model3_crom_fixed_mutable();
	if (!fixed_crom) fixed_crom = crom.data();
#else
	uint8_t *fixed_crom = crom.data();
#endif
	for (auto &[a, bytes] : free_play_text)""", "show_free_play_text entry")

replace_in_function(
"void Model3::show_free_play_text()", "} // namespace m3",
"		for (size_t k = 0; k < bytes.size(); k++) crom[(a + profile->program_at + k) ^ 3] = bytes[k];",
"		for (size_t k = 0; k < bytes.size(); k++) fixed_crom[(a + profile->program_at + k) ^ 3] = bytes[k];",
"show_free_play_text CROM pointer")

replace_once(
"	tex_ram.assign(2048 * 2048, 0);",
"""\ttex_ram.assign(2048 * 2048, 0);
#ifdef VF3_NAOMI
\tif (tex_ram.size() != 2048u * 2048u) {
\t\terror = "Not enough Naomi PVR VRAM for Model 3 texture RAM";
\t\treturn false;
\t}
#endif""", "texture RAM allocation")

path.write_text(source)

header_path = path.with_name("model3.h")
header = header_path.read_text()
if 'naomi_pvr_vector.h' not in header:
    if header.count("#include <vector>") != 1:
        raise SystemExit("expected exactly one model3.h vector include")
    header = header.replace("#include <vector>",
        "#include <vector>\n#ifdef VF3_NAOMI\n#include \"naomi_pvr_vector.h\"\n#endif", 1)

old_texture_field = "\tstd::vector<uint16_t> tex_ram;"
new_texture_field = """#ifdef VF3_NAOMI
\tNaomiPvrBuffer<uint16_t> tex_ram;
#else
\tstd::vector<uint16_t> tex_ram;
#endif"""
if new_texture_field not in header:
    if header.count(old_texture_field) != 1:
        raise SystemExit("expected exactly one Model 3 texture RAM field")
    header = header.replace(old_texture_field, new_texture_field, 1)

header_path.write_text(header)
