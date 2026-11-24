#!/usr/bin/env python3
"""Patch the pinned upstream Model 3 core for Naomi NetDIMM ROM access."""
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_model3_naomi.py path/to/model3.cpp")

path = pathlib.Path(sys.argv[1])
source = path.read_text()

include_anchor = '#include "model3.h"'
include_patch = include_anchor + '\n#include "cart_vrom_bridge.h"\n#include "vf3_image.h"'
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
"""bool Model3::load(const std::string &zip, std::string &error)
{
	romset::result r = romset::load(zip, *profile);
	if (!r.ok) {
		error = r.error;
		return false;
	}
	crom = std::move(r.crom);
	banked = std::move(r.banked);
	vrom = std::move(r.vrom);
	sound_rom = std::move(r.sound);
	samples = std::move(r.samples);""",
"""bool Model3::load(const std::string &zip, std::string &error)
{
#ifdef VF3_NAOMI
	(void)zip;
	if (naomi_model3_mount_netdimm_image() != NAOMI_VF3_IMAGE_OK) {
		error = "VF3NROM NetDIMM mount or region verification failed";
		return false;
	}
#else
	romset::result r = romset::load(zip, *profile);
	if (!r.ok) {
		error = r.error;
		return false;
	}
	crom = std::move(r.crom);
	banked = std::move(r.banked);
	vrom = std::move(r.vrom);
	sound_rom = std::move(r.sound);
	samples = std::move(r.samples);
#endif""", "Model3::load NetDIMM branch")

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
"			if (a + CROM_AT + k >= crom.size() || at(a + k) != uint8_t(t[k])) return false;",
"			if (a + CROM_AT + k >= fixed_crom_size || at(a + k) != uint8_t(t[k])) return false;",
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

# The Naomi SH-4 runs the scene preparation serially. Keep the portable helpers,
# but do not pull std::thread/pthread workers into the console build.
parallel_path = path.parent.parent / "video" / "parallel.h"
parallel = parallel_path.read_text()
if "#ifdef VF3_NAOMI" not in parallel:
    parallel_body = parallel.replace("#pragma once", "", 1).lstrip()
    parallel_stub = """#pragma once
#ifdef VF3_NAOMI
#include <functional>
inline void parallel_rows(int n, const std::function<void(int, int)> &f, int = 1)
{
    if (n > 0) f(0, n);
}
inline void parallel_tasks(int n, const std::function<void(int)> &f)
{
    for (int i = 0; i < n; ++i) f(i);
}
class background_job
{
public:
    void start(std::function<void()> f) { f(); }
    void wait() {}
};
#else
""" + parallel_body + "\n#endif\n"
    parallel_path.write_text(parallel_stub)

raster_h_path = path.parent.parent / "video" / "r3d_raster.h"
raster_h = raster_h_path.read_text()
old_resize_decl = "\tvoid resize(int width, int height, int game_width = 0);"
new_resize_decl = old_resize_decl + "\n\t// Set geometry-preparation dimensions without allocating CPU colour/depth buffers.\n\tvoid resize_geometry_only(int width, int height, int game_width = 0);"
if "resize_geometry_only" not in raster_h:
    if raster_h.count(old_resize_decl) != 1:
        raise SystemExit("expected exactly one Raster::resize declaration")
    raster_h = raster_h.replace(old_resize_decl, new_resize_decl, 1)
raster_h_path.write_text(raster_h)

raster_cpp_path = path.parent.parent / "video" / "r3d_raster.cpp"
raster_cpp = raster_cpp_path.read_text()
old_same_size = """\tif (width == w && height == h && game_width == gw)
\t\treturn;"""
new_same_size = """\tif (width == w && height == h && game_width == gw) {
\t\tif (base.empty()) alloc();
\t\treturn;
\t}"""
if new_same_size not in raster_cpp:
    if raster_cpp.count(old_same_size) != 1:
        raise SystemExit("expected exactly one Raster::resize same-size check")
    raster_cpp = raster_cpp.replace(old_same_size, new_same_size, 1)

geometry_method = """void Raster::resize_geometry_only(int width, int height, int game_width)
{
\tdrop_buffers();
\tif (game_width <= 0) game_width = int(std::lround(496.0 * height / 384.0));
\tgame_width = std::min(game_width, width);
\tif (width == w && height == h && game_width == gw)
\t\treturn;
\tw = width;
\th = height;
\tgw = game_width;
\tscale = h / 384.0f;
\txscale = gw / 496.0f;
\tgx = (w - gw) / 2;
}

"""
if "void Raster::resize_geometry_only" not in raster_cpp:
    anchor = "void Raster::alloc()"
    if raster_cpp.count(anchor) != 1:
        raise SystemExit("expected exactly one Raster::alloc definition")
    raster_cpp = raster_cpp.replace(anchor, geometry_method + anchor, 1)

old_prepare = """\t\t\t\tconst int parts = int(std::min<size_t>(items.size(), 64));
\t\t\t\tif (parts_out.size() < size_t(parts)) parts_out.resize(size_t(parts));
\t\t\t\tparallel_tasks(parts, [&](int k) {
\t\t\t\t\tstd::vector<Tri> &out = parts_out[size_t(k)];
\t\t\t\t\tout.clear();
\t\t\t\t\tconst size_t a = items.size() * size_t(k) / size_t(parts), e = items.size() * size_t(k + 1) / size_t(parts);
\t\t\t\t\tfor (size_t i = a; i < e; i++) prepare_mesh(items[i], out);
\t\t\t\t});
\t\t\t\tfor (int k = 0; k < parts; k++) tris.insert(tris.end(), parts_out[size_t(k)].begin(), parts_out[size_t(k)].end());"""
new_prepare = """#ifdef VF3_NAOMI
\t\t\t\tfor (int item : items) prepare_mesh(item, tris);
#else
""" + old_prepare + """
#endif"""
if new_prepare not in raster_cpp:
    if raster_cpp.count(old_prepare) != 1:
        raise SystemExit("expected exactly one parallel Raster::prepare batch")
    raster_cpp = raster_cpp.replace(old_prepare, new_prepare, 1)

raster_cpp_path.write_text(raster_cpp)

scene_path = path.parent.parent / "video" / "r3d_scene.cpp"
scene = scene_path.read_text()
if '#include "cart_vrom_bridge.h"' not in scene:
    scene = scene.replace('#include "../machine/model3.h"',
        '#include "../machine/model3.h"\n#ifdef VF3_NAOMI\n#include "cart_vrom_bridge.h"\n#include "cart_vrom.h"\n#include <cstdint>\n#endif', 1)

old_shared_line = "const int shared_count[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };"
new_shared_line = """#ifdef VF3_NAOMI
constexpr uint32_t NAOMI_VROM_MODEL_SCRATCH_WORDS = 0x00020000u;
constexpr uint32_t NAOMI_VROM_MODEL_CHUNK_WORDS = 0x00001000u;
alignas(32) uint32_t naomi_vrom_model_scratch[NAOMI_VROM_MODEL_SCRATCH_WORDS];
uint32_t naomi_vrom_model_words = 0;
#endif

const int shared_count[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };"""
if new_shared_line not in scene:
    if scene.count(old_shared_line) != 1:
        raise SystemExit("expected SceneBuilder shared vertex-count table")
    scene = scene.replace(old_shared_line, new_shared_line, 1)

old_model_at = """const uint32_t *SceneBuilder::model_at(uint32_t addr) const
{
\taddr &= 0xFFFFFF;
\treturn addr < 0x100000 ? poly + addr : vrom + addr;
}"""
new_model_at = """const uint32_t *SceneBuilder::model_at(uint32_t addr) const
{
	addr &= 0xFFFFFF;
	if (addr < 0x100000) return poly + addr;
#ifdef VF3_NAOMI
	uint32_t loaded = 0;
	uint32_t scan = 0;
	naomi_vrom_model_words = 0;
	while (loaded < NAOMI_VROM_MODEL_SCRATCH_WORDS) {
		uint32_t chunk = NAOMI_VROM_MODEL_CHUNK_WORDS;
		if (chunk > NAOMI_VROM_MODEL_SCRATCH_WORDS - loaded)
			chunk = NAOMI_VROM_MODEL_SCRATCH_WORDS - loaded;
		if (naomi_model3_vrom_read_words((addr + loaded) & 0xFFFFFF,
		        naomi_vrom_model_scratch + loaded, chunk) != NAOMI_CART_VROM_OK)
			return nullptr;
		loaded += chunk;
		while (scan + 7u <= loaded) {
			const uint32_t *h = naomi_vrom_model_scratch + scan;
			if (h[6] == 0) {
				naomi_vrom_model_words = scan + 7u;
				return naomi_vrom_model_scratch;
			}
			const uint32_t words = 7u +
				(((h[0] & 0x40u) ? 4u : 3u) - shared_count[h[0] & 0xFu]) * 4u;
			if (scan + words > loaded) break;
			if (h[1] & 4u) {
				naomi_vrom_model_words = scan + words;
				return naomi_vrom_model_scratch;
			}
			scan += words;
		}
	}
	return nullptr;
#else
	return vrom + addr;
#endif
}"""
if new_model_at not in scene:
    if scene.count(old_model_at) != 1:
        raise SystemExit("expected exactly one SceneBuilder::model_at")
    scene = scene.replace(old_model_at, new_model_at, 1)

old_vrom_bind = "\tvrom = reinterpret_cast<const uint32_t *>(board.vrom.data());"
new_vrom_bind = """#ifdef VF3_NAOMI
\tvrom = naomi_vrom_model_scratch;
#else
\tvrom = reinterpret_cast<const uint32_t *>(board.vrom.data());
#endif"""
if new_vrom_bind not in scene:
    if scene.count(old_vrom_bind) != 1:
        raise SystemExit("expected exactly one SceneBuilder VROM binding")
    scene = scene.replace(old_vrom_bind, new_vrom_bind, 1)

old_draw_prefix = """\tconst uint32_t *data = model_at(addr);
\tModel m;
\tm.addr = addr;
\tm.matrix = cur;
\tm.alpha = attr.alpha;
\tm.scale = attr.scale;
\tm.page = attr.page;
\tm.node_path = path;"""
new_draw_prefix = """\tModel m;
\tm.addr = addr;
\tm.matrix = cur;
\tm.alpha = attr.alpha;
\tm.scale = attr.scale;
\tm.page = attr.page;
\tm.node_path = path;
\tbool cached_rom = false;
\tif (addr >= 0x100000) {
\t\tauto cached = rom_cache.find(addr);
\t\tif (cached != rom_cache.end()) {
\t\t\tm.meshes = cached->second;
\t\t\tcached_rom = true;
\t\t}
\t}
\tif (!cached_rom) {
\t\tconst uint32_t *data = model_at(addr);
\t\tif (!data) return;"""
if new_draw_prefix not in scene:
    if scene.count(old_draw_prefix) != 1:
        raise SystemExit("expected exactly one SceneBuilder::draw_model prefix")
    scene = scene.replace(old_draw_prefix, new_draw_prefix, 1)

old_draw_end = """\t} else {
\t\tm.meshes = parse(data, true);
\t}
\t// (the marker under the notice:"""
new_draw_end = """\t} else {
\t\tm.meshes = parse(data, true);
\t}
\t}
\t// (the marker under the notice:"""
if new_draw_end not in scene:
    if scene.count(old_draw_end) != 1:
        raise SystemExit("expected exactly one SceneBuilder::draw_model end")
    scene = scene.replace(old_draw_end, new_draw_end, 1)

old_draw_end_rom = "\t\tconst uint32_t *end_rom = vrom + 0x1000000;"
new_draw_end_rom = """#ifdef VF3_NAOMI
\t\tconst uint32_t *end_rom = vrom + naomi_vrom_model_words;
#else
\t\tconst uint32_t *end_rom = vrom + 0x1000000;
#endif"""
if new_draw_end_rom not in scene:
    if scene.count(old_draw_end_rom) != 1:
        raise SystemExit("expected exactly one SceneBuilder model scan end")
    scene = scene.replace(old_draw_end_rom, new_draw_end_rom, 1)

old_parse_ends = "\tconst uint32_t *end_lo = poly + 0x100000, *end_rom = vrom + 0x1000000;"
new_parse_ends = """\tconst uint32_t *end_lo = poly + 0x100000;
#ifdef VF3_NAOMI
\tconst uint32_t *end_rom = vrom + naomi_vrom_model_words;
#else
\tconst uint32_t *end_rom = vrom + 0x1000000;
#endif"""
if new_parse_ends not in scene:
    if scene.count(old_parse_ends) != 1:
        raise SystemExit("expected exactly one SceneBuilder parse end pointer")
    scene = scene.replace(old_parse_ends, new_parse_ends, 1)

old_poly_range = "\tconst bool in_poly_ram = src >= poly && src < end_lo;"
new_poly_range = """\tconst std::uintptr_t src_address = reinterpret_cast<std::uintptr_t>(src);
\tconst std::uintptr_t poly_address = reinterpret_cast<std::uintptr_t>(poly);
\tconst bool in_poly_ram = src_address >= poly_address && src_address < poly_address + 0x100000u;"""
if new_poly_range not in scene:
    if scene.count(old_poly_range) != 1:
        raise SystemExit("expected exactly one SceneBuilder polygon RAM pointer test")
    scene = scene.replace(old_poly_range, new_poly_range, 1)


old_next_id = "static std::atomic<uint64_t> next_id{ 1 };"
new_next_id = """#ifdef VF3_NAOMI
\tstatic uint64_t next_id = 1;
#else
\tstatic std::atomic<uint64_t> next_id{ 1 };
#endif"""
if new_next_id not in scene:
    if scene.count(old_next_id) != 1:
        raise SystemExit("expected exactly one SceneBuilder atomic frame id")
    scene = scene.replace(old_next_id, new_next_id, 1)

scene_method = """#ifdef VF3_NAOMI
// Exercise the real NetDIMM-backed model_at() and polygon parser without
// constructing the full Model3 board. The caller supplies the board's polygon
// RAM address range; VROM model data is fetched through the production bridge.
bool SceneBuilder::naomi_test_vrom_model(
    uint32_t addr, const uint32_t *polygon_ram,
    size_t *mesh_count, size_t *vertex_count,
    bool *geometry_valid, bool *colour_valid)
{
    if (!polygon_ram || addr < 0x100000u)
        return false;
    poly = polygon_ram;
    vrom = naomi_vrom_model_scratch;
    Viewport test_viewport;
    vp = &test_viewport;
    attr = Attribs();
    cur = Mat4::identity();
    path = 0;
    hidden_marker[0] = hidden_marker[1] = 0;
    draw_model(addr);
    vp = nullptr;
    size_t meshes = 0, vertices = 0;
    std::vector<const Mesh *> parsed_meshes;
    for (const Model &model : test_viewport.models) {
        if (!model.meshes) continue;
        for (const Mesh &mesh : *model.meshes) {
            ++meshes;
            vertices += mesh.verts.size();
            parsed_meshes.push_back(&mesh);
        }
    }
    auto close = [](float a, float b) { return std::fabs(a - b) < 0.0001f; };
    bool geometry = parsed_meshes.size() == 2 &&
        parsed_meshes[0]->verts.size() == 3 &&
        parsed_meshes[1]->verts.size() == 3;
    if (geometry) {
        const Vertex *a = parsed_meshes[0]->verts.data();
        const Vertex *b = parsed_meshes[1]->verts.data();
        geometry =
            close(a[0].pos[0], 0.0f) && close(a[0].pos[1], 0.0f) &&
            close(a[1].pos[0], 2.0f) && close(a[1].pos[1], 0.0f) &&
            close(a[2].pos[0], 0.0f) && close(a[2].pos[1], 2.0f) &&
            close(b[0].pos[0], 1.0f) && close(b[0].pos[1], 0.0f) &&
            close(b[1].pos[0], 3.0f) && close(b[1].pos[1], 0.0f) &&
            close(b[2].pos[0], 1.0f) && close(b[2].pos[1], 2.0f);
    }
    bool colour = parsed_meshes.size() == 2 &&
        parsed_meshes[0]->verts.size() == 3 &&
        parsed_meshes[1]->verts.size() == 3;
    if (colour) {
        const uint8_t *a = parsed_meshes[0]->verts[0].colour;
        const uint8_t *b = parsed_meshes[1]->verts[0].colour;
        colour = a[0] == 255 && a[1] == 255 && a[2] == 255 && a[3] == 255 &&
                 b[0] == 255 && b[1] == 0 && b[2] == 0 && b[3] == 255;
    }
    if (mesh_count) *mesh_count = meshes;
    if (vertex_count) *vertex_count = vertices;
    if (geometry_valid) *geometry_valid = geometry;
    if (colour_valid) *colour_valid = colour;
    return !test_viewport.models.empty();
}
#endif

"""
if "bool SceneBuilder::naomi_test_vrom_model(" not in scene:
    namespace_end = "} // namespace video"
    if scene.count(namespace_end) != 1:
        raise SystemExit("expected exactly one video namespace end")
    scene = scene.replace(namespace_end, scene_method + "\n" + namespace_end, 1)

scene_header_path = path.parent.parent / "video" / "r3d_scene.h"
scene_header = scene_header_path.read_text()
test_decl = "\tsize_t cached() const { return rom_cache.size(); }"
test_decl_naomi = test_decl + """
#ifdef VF3_NAOMI
\tbool naomi_test_vrom_model(uint32_t addr, const uint32_t *polygon_ram,
\t                           size_t *mesh_count, size_t *vertex_count,\n\t                           bool *geometry_valid, bool *colour_valid);
#endif"""
if test_decl_naomi not in scene_header:
    if scene_header.count(test_decl) != 1:
        raise SystemExit("expected exactly one SceneBuilder cache accessor")
    scene_header = scene_header.replace(test_decl, test_decl_naomi, 1)
scene_header_path.write_text(scene_header)

scene_path.write_text(scene)


