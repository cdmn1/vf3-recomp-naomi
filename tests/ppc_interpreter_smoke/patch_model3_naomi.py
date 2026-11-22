#!/usr/bin/env python3
"""Patch the pinned upstream Model 3 texture-port handler for Naomi VROM streaming."""
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

old = """void Model3::r3d_texture_port(unsigned, uint32_t data)
{
	// Step 1.0: the data is a VROM word address; the words there go into the texture FIFO
	const uint32_t *v = reinterpret_cast<const uint32_t *>(vrom.data());
	uint32_t addr = data & 0xFFFFFF;
	uint32_t n = (2 + v[addr] / 2) / 4;
	for (uint32_t i = 0; i < n && tex_fifo.size() < 0x100000 / 4; i++)   // (the same words; a count of millions, a VROM word a program in a snapshot points at, no longer run through once the FIFO's full)
		tex_fifo.push_back(v[(addr + i) & 0xFFFFFF]);
}"""
new = """void Model3::r3d_texture_port(unsigned, uint32_t data)
{
#ifdef VF3_NAOMI
	// The bound NetDIMM image is streamed in bounded chunks into the same FIFO.
	if (naomi_model3_vrom_port(data, &tex_fifo))
		return;
#endif
	// Desktop fallback: retain upstream's resident VROM implementation.
	const uint32_t *v = reinterpret_cast<const uint32_t *>(vrom.data());
	uint32_t addr = data & 0xFFFFFF;
	uint32_t n = (2 + v[addr] / 2) / 4;
	for (uint32_t i = 0; i < n && tex_fifo.size() < 0x100000 / 4; i++)
		tex_fifo.push_back(v[(addr + i) & 0xFFFFFF]);
}"""
if 'naomi_model3_vrom_port(data, &tex_fifo)' not in source:
    if source.count(old) != 1:
        raise SystemExit("expected exactly one upstream r3d_texture_port implementation")
    source = source.replace(old, new, 1)

path.write_text(source)
