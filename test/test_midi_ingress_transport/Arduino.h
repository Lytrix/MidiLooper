#pragma once
#include <cstdint>
#include "../test_support/MidiIngressMaskFixture.h"
extern uint32_t ingressFakeMicros;
inline uint32_t micros() { return ingressFakeMicros; }
inline void __disable_irq() { (void)IngressMaskFixture::enter(IngressMaskFixture::Kind::Driver); }
inline void __enable_irq() { IngressMaskFixture::restore(IngressMaskFixture::Kind::Driver, 0); }
#define NVIC_DISABLE_IRQ(x) ((void)0)
#define NVIC_ENABLE_IRQ(x) ((void)0)
#define IRQ_USB1 0
