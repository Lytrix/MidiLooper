//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// What the undo history costs (E9.8b-3b, undo-depth sizing; owner: "99 is just arbitrarily chosen, not data
// analysed"). A diagnostic walk, O(states × runs), run only on request: the states held, and the runs and rows kept
// alive only by states that are not a slot's current — the content the history pins beyond what plays. Runs shared
// with a current state cost the history nothing; a cleared or replaced take's runs are the history's alone.
// No allocation: two bitmaps over the run table and one chain buffer on the stack.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"

namespace ContentEngine {

struct HistoryCostReport {
  uint16_t states = 0;          // history states held
  uint16_t currents = 0;        // of which a slot's current
  uint16_t historyRuns = 0;     // runs referenced only by non-current states
  uint32_t historyRows = 0;     // their rows
  uint32_t currentRows = 0;     // rows of the runs the currents reference (what plays or can play)
  uint8_t worstSlot = 0xFF;     // the slot whose non-current states pin the most rows
  uint32_t worstRows = 0;
};

template <typename History, typename Manifests, typename Runs>
CE_COLD HistoryCostReport historyCost(const History& h, const Manifests& m, const Runs& r, uint8_t slots) {
  constexpr uint16_t kRunBits = Runs::kRunCount;
  uint8_t current[(kRunBits + 7) / 8] = {}, counted[(kRunBits + 7) / 8] = {};
  const auto has = [](const uint8_t* b, RunId id) { return (b[id / 8] >> (id % 8)) & 1u; };
  const auto set = [](uint8_t* b, RunId id) { b[id / 8] = static_cast<uint8_t>(b[id / 8] | (1u << (id % 8))); };
  HistoryCostReport out;
  for (uint8_t s = 0; s < slots; ++s) {  // what the currents hold
    const ManifestId c = h.current(s);
    if (c == kNoManifest) continue;
    const auto& d = m.manifest(c);
    for (uint8_t i = 0; i < d.runCount; ++i)
      if (d.runs[i] < kRunBits && !has(current, d.runs[i])) {
        set(current, d.runs[i]);
        out.currentRows += r.run(d.runs[i]).rowCount;
      }
  }
  ManifestId chain[History::kDepth];
  for (uint8_t s = 0; s < slots; ++s) {
    int16_t cur = -1;
    bool below = false;
    const uint8_t n = h.chain(s, chain, History::kDepth, cur, below);
    out.states = static_cast<uint16_t>(out.states + n);
    if (cur >= 0) ++out.currents;
    uint32_t slotRows = 0;
    for (uint8_t k = 0; k < n; ++k) {
      if (static_cast<int16_t>(k) == cur) continue;
      const auto& d = m.manifest(chain[k]);
      for (uint8_t i = 0; i < d.runCount; ++i) {
        const RunId id = d.runs[i];
        if (id >= kRunBits || has(current, id) || has(counted, id)) continue;
        set(counted, id);
        ++out.historyRuns;
        out.historyRows += r.run(id).rowCount;
        slotRows += r.run(id).rowCount;
      }
    }
    if (slotRows > out.worstRows) {
      out.worstRows = slotRows;
      out.worstSlot = s;
    }
  }
  return out;
}

}  // namespace ContentEngine
