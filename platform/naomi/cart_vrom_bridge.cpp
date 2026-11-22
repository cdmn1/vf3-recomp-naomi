#include "cart_vrom_bridge.h"
#include "cart_vrom.h"

#include <vector>

namespace {
const naomi_cart_image_layout_t *bound_image = 0;
alignas(32) uint8_t scratch[NAOMI_CART_VROM_SCRATCH_SIZE];
int last_status = NAOMI_CART_VROM_OK;

int append_word(void *context, uint32_t word)
{
    std::vector<uint32_t> *fifo = static_cast<std::vector<uint32_t> *>(context);
    fifo->push_back(word);
    return 1;
}
}

extern "C" void naomi_model3_vrom_bind(const naomi_cart_image_layout_t *image)
{
    bound_image = image;
    last_status = NAOMI_CART_VROM_OK;
}

extern "C" int naomi_model3_vrom_port(uint32_t word_address, void *texture_fifo)
{
    std::vector<uint32_t> *fifo = static_cast<std::vector<uint32_t> *>(texture_fifo);
    naomi_cart_vrom_result_t result;
    naomi_cart_dma_result_t io;

    if (!bound_image || !fifo)
        return 0;

    last_status = naomi_cart_vrom_texture_port(
        bound_image, word_address, static_cast<uint32_t>(fifo->size()),
        NAOMI_CART_VROM_FIFO_BYTES / 4u, scratch, sizeof(scratch), 1000u,
        append_word, fifo, &result, &io);
    return 1;
}

extern "C" int naomi_model3_vrom_last_status(void)
{
    return last_status;
}
