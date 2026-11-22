#include "cart_vrom_bridge.h"
#include "cart_vrom.h"

#include <vector>

namespace {
const naomi_cart_image_layout_t *bound_image = 0;
naomi_cart_rom_t *bound_crom = 0;
alignas(32) uint8_t scratch[NAOMI_CART_VROM_SCRATCH_SIZE];
int last_vrom_status = NAOMI_CART_VROM_OK;
int last_crom_status = NAOMI_CART_IMAGE_OK;
naomi_cart_dma_result_t crom_io;

int append_word(void *context, uint32_t word)
{
    std::vector<uint32_t> *fifo = static_cast<std::vector<uint32_t> *>(context);
    fifo->push_back(word);
    return 1;
}

const uint8_t *crom_page(uint32_t offset)
{
    if (!bound_crom) {
        last_crom_status = NAOMI_CART_IMAGE_INVALID;
        return 0;
    }
    const uint8_t *page = naomi_cart_rom_banked_page(bound_crom, offset, &crom_io);
    last_crom_status = page ? NAOMI_CART_IMAGE_OK : NAOMI_CART_IMAGE_IO_ERROR;
    return page;
}
}

extern "C" void naomi_model3_vrom_bind(const naomi_cart_image_layout_t *image)
{
    bound_image = image;
    last_vrom_status = NAOMI_CART_VROM_OK;
}

extern "C" int naomi_model3_vrom_port(uint32_t word_address, void *texture_fifo)
{
    std::vector<uint32_t> *fifo = static_cast<std::vector<uint32_t> *>(texture_fifo);
    naomi_cart_vrom_result_t result;
    naomi_cart_dma_result_t io;
    if (!bound_image || !fifo)
        return 0;
    last_vrom_status = naomi_cart_vrom_texture_port(
        bound_image, word_address, static_cast<uint32_t>(fifo->size()),
        NAOMI_CART_VROM_FIFO_BYTES / 4u, scratch, sizeof(scratch), 1000u,
        append_word, fifo, &result, &io);
    return 1;
}

extern "C" int naomi_model3_vrom_last_status(void)
{
    return last_vrom_status;
}

extern "C" void naomi_model3_crom_bind(naomi_cart_rom_t *rom)
{
    bound_crom = rom;
    last_crom_status = NAOMI_CART_IMAGE_OK;
    if (bound_crom)
        last_crom_status = naomi_cart_rom_select_bank(bound_crom, 0u);
}

extern "C" const uint8_t *naomi_model3_crom_fixed(void)
{
    return naomi_cart_rom_fixed(bound_crom);
}

extern "C" uint8_t *naomi_model3_crom_fixed_mutable(void)
{
    return bound_crom && bound_crom->fixed_valid ? bound_crom->fixed_crom : 0;
}

extern "C" int naomi_model3_crom_is_bound(void)
{
    return bound_crom != 0;
}

extern "C" int naomi_model3_crom_select_bank(uint8_t bank_register)
{
    if (!bound_crom)
        return 0;
    last_crom_status = naomi_cart_rom_select_bank(
        bound_crom, (uint32_t)(~bank_register) & 0x0Fu);
    return 1;
}

extern "C" uint8_t naomi_model3_crom_read8(uint32_t bank_offset)
{
    const uint8_t *page = crom_page(bank_offset);
    return page ? page[bank_offset & (NAOMI_CART_CROM_PAGE_SIZE - 1u)] : 0xFFu;
}

extern "C" uint16_t naomi_model3_crom_read16(uint32_t bank_offset)
{
    const uint8_t *page = crom_page(bank_offset);
    if (!page) return 0xFFFFu;
    return *(const uint16_t *)(page + (bank_offset & (NAOMI_CART_CROM_PAGE_SIZE - 1u)));
}

extern "C" uint32_t naomi_model3_crom_read32(uint32_t bank_offset)
{
    const uint8_t *page = crom_page(bank_offset);
    if (!page) return 0xFFFFFFFFu;
    return *(const uint32_t *)(page + (bank_offset & (NAOMI_CART_CROM_PAGE_SIZE - 1u)));
}

extern "C" int naomi_model3_crom_last_status(void)
{
    return last_crom_status;
}
