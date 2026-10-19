// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <stdint.h>
#ifndef MIDI_INGRESS_RECEIPTS_ENABLED
#define MIDI_INGRESS_RECEIPTS_ENABLED 0
#endif
#ifndef MIDI_INGRESS_MASK_QUALIFICATION
#define MIDI_INGRESS_MASK_QUALIFICATION 0
#endif
#if MIDI_INGRESS_MASK_QUALIFICATION && (!defined(PIO_UNIT_TEST_NATIVE) || defined(__IMXRT1062__))
#error "MIDI_INGRESS_MASK_QUALIFICATION is native-only; not target timing instrumentation"
#endif
#ifndef MIDI_INGRESS_COST_QUALIFICATION
#define MIDI_INGRESS_COST_QUALIFICATION 0
#endif
#if MIDI_INGRESS_COST_QUALIFICATION && (!MIDI_INGRESS_RECEIPTS_ENABLED || (defined(__IMXRT1062__) && !defined(SESSION_CAPTURE)) || (!defined(__IMXRT1062__) && !defined(PIO_UNIT_TEST_NATIVE)))
#error "Ingress cost probes require receipts and native or Teensy session capture"
#endif
// Qualifying observations only; never enabled by the receipt profile alone.
#if MIDI_INGRESS_RECEIPTS_ENABLED
#ifdef __cplusplus
extern "C" {
#endif
void midi_ingress_reset(uint8_t route, const void* owner, uint32_t capacity);
void midi_ingress_packet(uint8_t route, const void* owner, uint32_t capacity, uint8_t late);
void midi_ingress_publish(uint8_t route, const void* owner, uint32_t slot, uint32_t offset, uint32_t count);
void midi_ingress_consume(uint8_t route, const void* owner, uint32_t slot, uint32_t offset);
void midi_ingress_clear_last(uint8_t route, const void* owner);
void midi_ingress_invalidate(uint8_t route, const void* owner);
void midi_ingress_direct(uint8_t route, const void* owner);
#if MIDI_INGRESS_COST_QUALIFICATION
void midi_ingress_measured_packet(uint8_t route, const void* owner, uint32_t capacity, uint8_t late);
void midi_ingress_measured_direct(uint8_t route, const void* owner);
#endif
#ifdef __cplusplus
}
#endif
#define MIDI_INGRESS_RESET(r,o,c) midi_ingress_reset(r,o,c)
#if MIDI_INGRESS_COST_QUALIFICATION
#define MIDI_INGRESS_PACKET(r,o,c) midi_ingress_measured_packet(r,o,c,0)
#define MIDI_INGRESS_LATE_PACKET(r,o,c) midi_ingress_measured_packet(r,o,c,1)
#define MIDI_INGRESS_DIRECT(r,o) midi_ingress_measured_direct(r,o)
#else
#define MIDI_INGRESS_PACKET(r,o,c) midi_ingress_packet(r,o,c,0)
#define MIDI_INGRESS_LATE_PACKET(r,o,c) midi_ingress_packet(r,o,c,1)
#define MIDI_INGRESS_DIRECT(r,o) midi_ingress_direct(r,o)
#endif
#define MIDI_INGRESS_PUBLISH(r,o,s,b,n) midi_ingress_publish(r,o,s,b,n)
#define MIDI_INGRESS_CONSUME(r,o,s,b) midi_ingress_consume(r,o,s,b)
#define MIDI_INGRESS_CLEAR_LAST(r,o) midi_ingress_clear_last(r,o)
#define MIDI_INGRESS_INVALIDATE(r,o) midi_ingress_invalidate(r,o)
#else
#define MIDI_INGRESS_RESET(r,o,c) ((void)0)
#define MIDI_INGRESS_PACKET(r,o,c) ((void)0)
#define MIDI_INGRESS_LATE_PACKET(r,o,c) ((void)0)
#define MIDI_INGRESS_PUBLISH(r,o,s,b,n) ((void)0)
#define MIDI_INGRESS_CONSUME(r,o,s,b) ((void)0)
#define MIDI_INGRESS_CLEAR_LAST(r,o) ((void)0)
#define MIDI_INGRESS_INVALIDATE(r,o) ((void)0)
#define MIDI_INGRESS_DIRECT(r,o) ((void)0)
#endif
