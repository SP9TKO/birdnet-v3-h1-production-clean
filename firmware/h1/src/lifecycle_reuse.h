// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Full allocator ownership, including preparation, within the existing parent. */
#define H1_BACKBONE_ARENA_BYTES UINT32_C(3444832)
#define H1_CLASSIFIER_ARENA_ADDRESS UINT32_C(0x023490a0)
#define H1_CLASSIFIER_ARENA_BYTES UINT32_C(55840)
#define H1_LIFECYCLE_GUARD_BYTES UINT32_C(64)
#define H1_CLASSIFIER_PSRAM_ADDRESS UINT32_C(0xa177c280)

#ifdef __cplusplus
extern "C" {
#endif
bool h1LifecycleReuseReady(void);
#ifdef __cplusplus
}
#endif
