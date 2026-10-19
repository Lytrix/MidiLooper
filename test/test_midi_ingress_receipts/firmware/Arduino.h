#pragma once
#include <cstdint>
#define FLASHMEM
inline uint32_t ingressRawMicros = 0;
inline uint32_t micros() { return ingressRawMicros; }
