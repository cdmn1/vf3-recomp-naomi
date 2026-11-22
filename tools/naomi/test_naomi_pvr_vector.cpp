#include "naomi_pvr_vector.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

static size_t available = 16u * 1024u * 1024u;
static unsigned allocations, frees;

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
    return 0;
}
