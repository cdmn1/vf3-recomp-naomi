#include "naomi_pvr_vector.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

alignas(32) unsigned char test_pvr_ram[16u * 1024u * 1024u];
static size_t available = 16u * 1024u * 1024u;
static unsigned allocations, frees;
static int system_mode = 1;

extern "C" int hardware_sys_mode(void *)
{
    return system_mode;
}

extern "C" size_t pvr_mem_available(void)
{
    return available;
}

extern "C" pvr_ptr_t pvr_mem_malloc(size_t size)
{
    void *p = 0;
    if (posix_memalign(&p, 32, size) != 0)
        return 0;
    ++allocations;
    return p;
}

extern "C" void pvr_mem_free(pvr_ptr_t p)
{
    if (p) {
        ++frees;
        free(p);
    }
}

int main()
{
    NaomiPvrBuffer<uint16_t> buffer;
    system_mode = 1;
    available = 0;
    buffer.assign(4, 0x1234);
    assert(buffer.empty());

    available = 16u * 1024u * 1024u;
    buffer.assign(4, 0x1234);
    assert(buffer.size() == 4);
    assert((reinterpret_cast<uintptr_t>(buffer.data()) & 31u) == 0);
    assert(buffer[0] == 0x1234 && buffer[3] == 0x1234);
    buffer[2] = 0x5678;

    NaomiPvrBuffer<uint16_t> moved(static_cast<NaomiPvrBuffer<uint16_t> &&>(buffer));
    assert(buffer.empty());
    assert(moved.size() == 4 && moved[2] == 0x5678);
    moved.clear();
    assert(moved.empty());
    assert(allocations == 1 && frees == 1);

    system_mode = HW_TYPE_RETAIL;
    buffer.assign(4, 0xABCD);
    assert(buffer.size() == 4);
    assert(reinterpret_cast<uintptr_t>(buffer.data()) ==
           reinterpret_cast<uintptr_t>(test_pvr_ram) + 8u * 1024u * 1024u);
    assert(buffer[0] == 0xABCD && buffer[3] == 0xABCD);
    buffer.clear();
    assert(frees == 1); // Naomi's reserved upper VRAM half is not allocator-owned.
    return 0;
}
