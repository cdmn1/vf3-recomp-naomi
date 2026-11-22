#include "vf3_image.h"

namespace {
naomi_vf3_image_t image;
alignas(32) uint8_t fixed_crom[NAOMI_VF3_IMAGE_FIXED_SIZE];
alignas(32) uint8_t page_cache[NAOMI_VF3_IMAGE_BANK_CACHE_SIZE];
alignas(32) uint8_t verify_scratch[NAOMI_VF3_IMAGE_VERIFY_SIZE];
}

extern "C" naomi_vf3_image_status_t naomi_model3_mount_netdimm_image(void)
{
    naomi_cart_dma_result_t io = {};
    return naomi_vf3_image_mount(
        &image,
        fixed_crom, sizeof(fixed_crom),
        page_cache, sizeof(page_cache),
        verify_scratch, sizeof(verify_scratch),
        1000u, &io);
}
