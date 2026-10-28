#pragma once
#include <esp_heap_caps.h>

// Capture before logging, so formatting/queue activity cannot change this sample.
struct HeapSnapshot {
    size_t internalFree, internalLargest, dmaFree, dmaLargest;
    static HeapSnapshot capture() {
        constexpr uint32_t internal=MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        constexpr uint32_t dma=MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
        return {heap_caps_get_free_size(internal),heap_caps_get_largest_free_block(internal),
                heap_caps_get_free_size(dma),heap_caps_get_largest_free_block(dma)};
    }
    template<class Sink> void print(Sink &sink,const char *stage) const {
        sink.printf("[MEM] stage=%s internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u\n",
            stage,unsigned(internalFree),unsigned(internalLargest),unsigned(dmaFree),unsigned(dmaLargest));
    }
};
