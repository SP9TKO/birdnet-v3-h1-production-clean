// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "baseline_profile.h"

/* Branch-observed diagnostic operation masks only. The real production cache
 * callback API and its CMSIS operation arguments are unchanged. */

static inline struct H1BaselineCache *h1BaselineCleanRange(uint32_t *address, uint32_t bytes)
{
 const uint32_t operation_mask = H1_BASELINE_CACHE_CLEAN | H1_BASELINE_CACHE_RANGE;
 return h1BaselineCacheBegin(address, bytes, operation_mask, H1_EVENT_PRE_SUBMIT_CACHE_BEGIN);
}

static inline struct H1BaselineCache *h1BaselineCleanWhole(uint32_t *address, uint32_t bytes)
{
 const uint32_t operation_mask = H1_BASELINE_CACHE_CLEAN | H1_BASELINE_CACHE_WHOLE;
 return h1BaselineCacheBegin(address, bytes, operation_mask, H1_EVENT_PRE_SUBMIT_CACHE_BEGIN);
}

static inline struct H1BaselineCache *h1BaselineFlushBarrier(uint32_t *address, uint32_t bytes)
{
 const uint32_t operation_mask = H1_BASELINE_CACHE_BARRIER;
 return h1BaselineCacheBegin(address, bytes, operation_mask, H1_EVENT_PRE_SUBMIT_CACHE_BEGIN);
}

static inline struct H1BaselineCache *h1BaselineInvalidateRange(uint32_t *address, uint32_t bytes)
{
 const uint32_t operation_mask = H1_BASELINE_CACHE_INVALIDATE | H1_BASELINE_CACHE_RANGE;
 return h1BaselineCacheBegin(address, bytes, operation_mask, H1_EVENT_WAIT_CACHE_BEGIN);
}

static inline struct H1BaselineCache *h1BaselineCleanInvalidateWhole(uint32_t *address, uint32_t bytes)
{
 const uint32_t operation_mask = H1_BASELINE_CACHE_CLEAN | H1_BASELINE_CACHE_INVALIDATE | H1_BASELINE_CACHE_WHOLE;
 return h1BaselineCacheBegin(address, bytes, operation_mask, H1_EVENT_WAIT_CACHE_BEGIN);
}

static inline struct H1BaselineCache *h1BaselineInvalidateBarrier(uint32_t *address, uint32_t bytes)
{
 const uint32_t operation_mask = H1_BASELINE_CACHE_BARRIER;
 return h1BaselineCacheBegin(address, bytes, operation_mask, H1_EVENT_WAIT_CACHE_BEGIN);
}
