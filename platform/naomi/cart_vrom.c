#include "cart_vrom.h"
#include <stdint.h>
#define VROM_ADDRESS_MASK 0x00FFFFFFu
#define VROM_PAGE_WORDS (NAOMI_CART_VROM_SCRATCH_SIZE / 4u)
static int aligned32(const void *p){return (((uintptr_t)p)&31u)==0u;}
static naomi_cart_vrom_status_t stream_words(const naomi_cart_image_layout_t *image,uint32_t address,uint32_t count,uint8_t *scratch,uint32_t timeout_ms,naomi_cart_vrom_word_sink_t sink,void *context,naomi_cart_dma_result_t *io_result){
 uint32_t emitted=0;naomi_cart_dma_result_t last={0,0,0,0,0};
 while(emitted<count){
  uint32_t current=(address+emitted)&VROM_ADDRESS_MASK;
  uint32_t logical=current&(NAOMI_CART_VROM_WORD_COUNT-1u);
  uint32_t page_word=logical&~(VROM_PAGE_WORDS-1u),skip=logical-page_word,take=count-emitted,available=VROM_PAGE_WORDS-skip;
  uint32_t read_words,read_bytes;const uint32_t *words;
  if(take>available)take=available;
  read_words=(skip+take+7u)&~7u;read_bytes=read_words*4u;
  if(naomi_cart_image_read_region(image,NAOMI_CART_REGION_VROM,page_word*4u,scratch,read_bytes,timeout_ms,&last)!=NAOMI_CART_IMAGE_OK){if(io_result)*io_result=last;return NAOMI_CART_VROM_IO_ERROR;}
  words=(const uint32_t*)scratch;
  for(uint32_t i=0;i<take;i++)if(!sink(context,words[skip+i])){if(io_result)*io_result=last;return NAOMI_CART_VROM_SINK_ERROR;}
  emitted+=take;
 }
 if(io_result) *io_result=last;
 return NAOMI_CART_VROM_OK;
}
naomi_cart_vrom_status_t naomi_cart_vrom_texture_port(const naomi_cart_image_layout_t *image,uint32_t word_address,uint32_t fifo_words_used,uint32_t fifo_capacity_words,void *scratch,uint32_t scratch_size,uint32_t timeout_ms,naomi_cart_vrom_word_sink_t sink,void *sink_context,naomi_cart_vrom_result_t *result,naomi_cart_dma_result_t *io_result){
 const naomi_cart_image_region_t *vrom;uint32_t header,declared,available,emit;naomi_cart_vrom_result_t r={0,0};naomi_cart_dma_result_t last={0,0,0,0,0};naomi_cart_vrom_status_t stream_rc;
 if(result)*result=r;
 if(!image||!scratch||!sink||!timeout_ms||!aligned32(scratch)||scratch_size<NAOMI_CART_VROM_SCRATCH_SIZE||fifo_words_used>fifo_capacity_words||fifo_capacity_words>(NAOMI_CART_VROM_FIFO_BYTES/4u))return NAOMI_CART_VROM_INVALID;
 vrom=naomi_cart_image_find_region(image,NAOMI_CART_REGION_VROM);
 if(!vrom||vrom->size!=NAOMI_CART_VROM_LOGICAL_SIZE)return NAOMI_CART_VROM_INVALID;
 {
  uint32_t logical=(word_address&VROM_ADDRESS_MASK)&(NAOMI_CART_VROM_WORD_COUNT-1u),aligned_word=logical&~7u;
  if(naomi_cart_image_read_region(image,NAOMI_CART_REGION_VROM,aligned_word*4u,scratch,32u,timeout_ms,&last)!=NAOMI_CART_IMAGE_OK){if(io_result)*io_result=last;return NAOMI_CART_VROM_IO_ERROR;}
  header=((const uint32_t*)scratch)[logical-aligned_word];
 }
 declared=(2u+header/2u)/4u;available=fifo_capacity_words-fifo_words_used;emit=declared<available?declared:available;r.declared_words=declared;
 stream_rc=emit?stream_words(image,word_address,emit,(uint8_t*)scratch,timeout_ms,sink,sink_context,&last):NAOMI_CART_VROM_OK;
 if(stream_rc!=NAOMI_CART_VROM_OK){if(io_result)*io_result=last;return stream_rc;}
 r.emitted_words=emit;if(result)*result=r;if(io_result)*io_result=last;return NAOMI_CART_VROM_OK;
}
