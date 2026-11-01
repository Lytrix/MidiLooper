//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

/// C5 gate (DEC-246 amendment 20): the manifest-backed committed reader exists only where it is enabled. Device images
/// default to 0, so the default image carries none of its code; the native suites always build it.
#ifndef REVISION_MANIFEST_READER
#if defined(PIO_UNIT_TEST_NATIVE)
#define REVISION_MANIFEST_READER 1
#else
#define REVISION_MANIFEST_READER 0
#endif
#endif

/// C5.3 (DEC-246 amendment 22): the one track whose active slot's committed windows are read from the manifest. Every
/// other track keeps the snapshot reader. Static routing, chosen before any request; UI selection never moves it.
#if REVISION_MANIFEST_READER && !defined(REVISION_MANIFEST_READER_TRACK)
#define REVISION_MANIFEST_READER_TRACK 0
#endif
