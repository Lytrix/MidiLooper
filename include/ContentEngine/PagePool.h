//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Fixed page pool of the content engine. Pages are non-contiguous 16-byte-row arrays handed out from a LIFO free
// stack. Every page carries an owner tag, so conservation is checked by ownership (each page is either on the free
// stack or owned by exactly one holder), not only by a count. No allocation; `init()` writes every field the pool reads
// because RAM2 / EXTMEM are not zeroed at reset.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "RevisionStore/RevisionStore.h"

namespace ContentEngine {

using PassEvent = RevisionStore::PassEvent;

/// Who holds a page. Values below `kOwnerReservedBase` are run ids.
using PageOwner = uint16_t;
constexpr PageOwner kOwnerFree = 0xFFFFu;
constexpr PageOwner kOwnerIndex = 0xFFFEu;
constexpr PageOwner kOwnerCompaction = 0xFFFDu;
constexpr PageOwner kOwnerReservedBase = 0xFFF0u;

template <uint16_t PageCount, uint16_t RowsPerPage>
class PagePool {
 public:
  static constexpr uint16_t kPageCount = PageCount;
  static constexpr uint16_t kRowsPerPage = RowsPerPage;
  static constexpr uint32_t kPageBytes = sizeof(PassEvent) * RowsPerPage;
  static constexpr uint32_t kIdsPerDirPage = kPageBytes / sizeof(PageId);
  static_assert(PageCount < kOwnerReservedBase, "page ids must stay below the owner sentinels");

  void init() {
    for (uint16_t i = 0; i < PageCount; ++i) {
      free_[i] = static_cast<PageId>(PageCount - 1 - i);  // page 0 is handed out first
      owner_[i] = kOwnerFree;
    }
    top_ = PageCount;
  }

  /// O(1). `kNoPage` when empty. The caller's reservation accounting decides whether it may ask.
  PageId acquire(PageOwner owner) {
    if (top_ == 0 || owner == kOwnerFree) return kNoPage;
    const PageId id = free_[--top_];
    owner_[id] = owner;
    return id;
  }

  /// O(1). Refuses (returns false) a page not held by `owner`, so a double release cannot corrupt the stack.
  bool release(PageId id, PageOwner owner) {
    if (id >= PageCount || owner_[id] != owner || owner == kOwnerFree) return false;
    owner_[id] = kOwnerFree;
    free_[top_++] = id;
    return true;
  }

  uint16_t freeCount() const { return top_; }
  PageOwner ownerOf(PageId id) const { return id < PageCount ? owner_[id] : kOwnerFree; }

  PassEvent* rows(PageId id) { return reinterpret_cast<PassEvent*>(pages_[id].raw); }
  const PassEvent* rows(PageId id) const { return reinterpret_cast<const PassEvent*>(pages_[id].raw); }
  PageId* dirIds(PageId id) { return reinterpret_cast<PageId*>(pages_[id].raw); }
  const PageId* dirIds(PageId id) const { return reinterpret_cast<const PageId*>(pages_[id].raw); }

  /// Conservation by ownership: the free stack holds exactly the pages tagged free, each once, and every other page is
  /// tagged with a holder. O(PageCount); diagnostics and tests only.
  bool checkFreeStack() const {
    uint32_t taggedFree = 0;
    for (uint16_t i = 0; i < PageCount; ++i) taggedFree += owner_[i] == kOwnerFree ? 1u : 0u;
    if (taggedFree != top_) return false;
    for (uint16_t i = 0; i < top_; ++i) {
      const PageId id = free_[i];
      if (id >= PageCount || owner_[id] != kOwnerFree) return false;
      for (uint16_t j = static_cast<uint16_t>(i + 1); j < top_; ++j)
        if (free_[j] == id) return false;
    }
    return true;
  }

  /// Pages tagged with `owner`. O(PageCount); diagnostics and tests only.
  uint32_t countOwnedBy(PageOwner owner) const {
    uint32_t n = 0;
    for (uint16_t i = 0; i < PageCount; ++i) n += owner_[i] == owner ? 1u : 0u;
    return n;
  }

  static constexpr size_t storageBytes() { return sizeof(Page) * PageCount; }
  static constexpr size_t metadataBytes() { return sizeof(PageId) * PageCount + sizeof(PageOwner) * PageCount + 2; }

 private:
  struct Page {
    alignas(PassEvent) unsigned char raw[kPageBytes];
  };
  Page pages_[PageCount];
  PageId free_[PageCount];
  PageOwner owner_[PageCount];
  uint16_t top_;
};

}  // namespace ContentEngine
