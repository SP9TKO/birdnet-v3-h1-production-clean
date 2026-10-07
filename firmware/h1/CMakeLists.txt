# SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Audio selection controls only the application build personality. PDM remains
# the default; NONE disables the PDM devicetree node and excludes its app source.
set(H1_AUDIO_BACKEND "PDM" CACHE STRING "Audio backend: PDM or NONE")
set_property(CACHE H1_AUDIO_BACKEND PROPERTY STRINGS PDM NONE)
if(NOT H1_AUDIO_BACKEND MATCHES "^(PDM|NONE)$")
  message(FATAL_ERROR "H1_AUDIO_BACKEND must be PDM or NONE")
endif()
list(APPEND DTC_OVERLAY_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/boards/psram-model.overlay"
  "${CMAKE_CURRENT_SOURCE_DIR}/boards/usb-cdc.overlay"
  "${CMAKE_CURRENT_SOURCE_DIR}/boards/u85-ospi.overlay")
if(H1_AUDIO_BACKEND STREQUAL "PDM")
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/pdm-mic.overlay")
else()
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/audio-no-pdm.overlay")
  add_compile_definitions(H1_DIAG_SKIP_PDM_INIT=1)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(birdnet_v3_h1)

option(H1_FRONTEND_DIAGNOSTICS "Build the optional H1 frontend diagnostics adapter" OFF)
option(H1_BASELINE_V2_INSTRUMENTATION "Observe H1 baseline cache/lifecycle paths" OFF)

if(NOT ETHOSU_TARGET_NPU_CONFIG STREQUAL "ethos-u85-256")
  message(FATAL_ERROR "H1 requires -DETHOSU_TARGET_NPU_CONFIG=ethos-u85-256")
endif()
if(NOT CONFIG_SOC_SERIES_E8 OR NOT CONFIG_RTSS_HP)
  message(FATAL_ERROR "H1 requires alif_e8_dk/.../rtss_hp")
endif()
if(NOT CONFIG_ARM_ETHOS_U85_256 OR CONFIG_ARM_ETHOS_U55_256)
  message(FATAL_ERROR "H1 requires the u85-256.conf correction")
endif()

set(H1_FRONTEND_SHA256 "71bd258230cd7897a278132d973e0c9c5ea40a3d0be3ed3a3702175d387732d0")
set(H1_FRONTEND_BYTES 545776)
set(H1_BACKBONE_SHA256 "f3328db0aa2d7befc13d0f3dd7aa20906ba2164d15f589ddb1f746de398fc2e7")
set(H1_BACKBONE_BYTES 20465840)
set(H1_CLASSIFIER_SHA256 "2ff002cb4bf95b33384d70023a34e377b1d34bb2786e39efc5d358c3f80a6e93")
set(H1_CLASSIFIER_BYTES 13008016)
set(H1_SYNTHETIC_SHA256 "23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8")
set(H1_WREN_SHA256 "46be8bff9ee1fbbb90d66952cc5efb1cde3582ae21862f6744721a1be5183556")
set(H1_WAVEFORM_BYTES 384000)

foreach(required
    H1_FRONTEND_MODEL_BIN H1_BACKBONE_MODEL_BIN H1_CLASSIFIER_MODEL_BIN
    H1_SYNTHETIC_BIN H1_WREN_BIN)
  if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
    message(FATAL_ERROR "${required} must name the exact local integrated-H1 payload")
  endif()
endforeach()

function(require_identity path expected_bytes expected_sha label)
  file(SIZE "${path}" actual_bytes)
  file(SHA256 "${path}" actual_sha)
  if(NOT actual_bytes EQUAL expected_bytes OR NOT actual_sha STREQUAL expected_sha)
    message(FATAL_ERROR
      "${label} identity mismatch: bytes=${actual_bytes}, sha256=${actual_sha}")
  endif()
endfunction()

require_identity("${H1_FRONTEND_MODEL_BIN}" ${H1_FRONTEND_BYTES} "${H1_FRONTEND_SHA256}" "frontend")
require_identity("${H1_BACKBONE_MODEL_BIN}" ${H1_BACKBONE_BYTES} "${H1_BACKBONE_SHA256}" "compiled backbone")
require_identity("${H1_CLASSIFIER_MODEL_BIN}" ${H1_CLASSIFIER_BYTES} "${H1_CLASSIFIER_SHA256}" "compiled classifier")
require_identity("${H1_SYNTHETIC_BIN}" ${H1_WAVEFORM_BYTES} "${H1_SYNTHETIC_SHA256}" "synthetic waveform")
require_identity("${H1_WREN_BIN}" ${H1_WAVEFORM_BYTES} "${H1_WREN_SHA256}" "Wren waveform")

file(TO_CMAKE_PATH "${H1_FRONTEND_MODEL_BIN}" H1_FRONTEND_MODEL_BIN_NORMALIZED)
file(TO_CMAKE_PATH "${H1_SYNTHETIC_BIN}" H1_SYNTHETIC_BIN_NORMALIZED)
file(TO_CMAKE_PATH "${H1_WREN_BIN}" H1_WREN_BIN_NORMALIZED)
configure_file(src/fixtures.S.in generated/h1_fixtures.S @ONLY)

set(H1_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Use the pinned A/B/C/D timing mechanism without modifying
# the SDK checkout. Each source identity and
# exact insertion anchor is checked before a generated build-tree copy is used.
get_filename_component(ETHOSU_MODULE_DIR "${CMSIS_PATH}/../ethos_u" ABSOLUTE)
set(ETHOSU_U85_SOURCE "${ETHOSU_MODULE_DIR}/src/ethosu_device_u85.c")
set(ETHOSU_U85_SHA256 "5e1e0142785b1ddac83e60ee7f51136ede539b917625c9d53916bbaaa5e23ca0")
file(SHA256 "${ETHOSU_U85_SOURCE}" actual_ethosu_u85_sha256)
if(NOT actual_ethosu_u85_sha256 STREQUAL ETHOSU_U85_SHA256)
  message(FATAL_ERROR
    "Pinned Ethos-U85 source identity mismatch: ${actual_ethosu_u85_sha256}")
endif()
file(READ "${ETHOSU_U85_SOURCE}" instrumented_ethosu_u85)
set(ethosu_include_anchor "#include \"ethosu_log.h\"\n")
set(ethosu_include_replacement
  "#include \"ethosu_log.h\"\n#include \"runtime_profile.h\"\n")
string(REPLACE "${ethosu_include_anchor}" "${ethosu_include_replacement}"
  instrumented_ethosu_u85 "${instrumented_ethosu_u85}")
set(ethosu_command_anchor
  "    cmd.word                        = dev->reg->CMD.word & NPU_CMD_PWR_CLK_MASK;\n    cmd.transition_to_running_state = 1;\n\n    dev->reg->CMD.word = cmd.word;")
set(ethosu_command_replacement
  "    cmd.word                        = dev->reg->CMD.word & NPU_CMD_PWR_CLK_MASK;\n    cmd.transition_to_running_state = 1;\n\n    h1ProfileCommandStart();\n    dev->reg->CMD.word = cmd.word;")
string(FIND "${instrumented_ethosu_u85}" "${ethosu_command_anchor}"
  ethosu_command_anchor_offset)
if(ethosu_command_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Ethos-U85 command-start insertion anchor not found")
endif()
string(REPLACE "${ethosu_command_anchor}" "${ethosu_command_replacement}"
  instrumented_ethosu_u85 "${instrumented_ethosu_u85}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_device_u85_profiled.c"
  "${instrumented_ethosu_u85}")


set(ETHOSU_DRIVER_SOURCE "${ETHOSU_MODULE_DIR}/src/ethosu_driver.c")
set(ETHOSU_DRIVER_SHA256 "227f7f247c163427f0531e43f42436eeccb202d2e5545f83af1626db10f5c5a0")
file(SHA256 "${ETHOSU_DRIVER_SOURCE}" actual_ethosu_driver_sha256)
if(NOT actual_ethosu_driver_sha256 STREQUAL ETHOSU_DRIVER_SHA256)
  message(FATAL_ERROR
    "Pinned Ethos-U driver source identity mismatch: ${actual_ethosu_driver_sha256}")
endif()
file(READ "${ETHOSU_DRIVER_SOURCE}" instrumented_ethosu_driver)
set(ethosu_driver_include_anchor "#include \"ethosu_log.h\"\n")
set(ethosu_driver_include_replacement
  "#include \"ethosu_log.h\"\n#include \"runtime_profile.h\"\n")
string(REPLACE "${ethosu_driver_include_anchor}" "${ethosu_driver_include_replacement}"
  instrumented_ethosu_driver "${instrumented_ethosu_driver}")
set(ethosu_cache_anchor
  "            ethosu_flush_dcache((uint32_t *)(uintptr_t)drv->job.base_addr[i], drv->job.base_addr_size[i]);")
set(ethosu_cache_replacement
  "            h1ProfileCachePrepareBegin((uint32_t)drv->job.base_addr[i], (uint32_t)drv->job.base_addr_size[i]);\n            ethosu_flush_dcache((uint32_t *)(uintptr_t)drv->job.base_addr[i], drv->job.base_addr_size[i]);\n            h1ProfileCachePrepareEnd();")
string(FIND "${instrumented_ethosu_driver}" "${ethosu_cache_anchor}"
  ethosu_cache_anchor_offset)
if(ethosu_cache_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Ethos-U cache-prepare insertion anchor not found")
endif()
string(REPLACE "${ethosu_cache_anchor}" "${ethosu_cache_replacement}"
  instrumented_ethosu_driver "${instrumented_ethosu_driver}")
if(H1_BASELINE_V2_INSTRUMENTATION)
  string(REPLACE "#include <stdlib.h>" "#include <stdlib.h>\n#include \"baseline_profile.h\""
    instrumented_ethosu_driver "${instrumented_ethosu_driver}")
  foreach(operation flush invalidate)
    set(cache_call "            ethosu_${operation}_dcache((uint32_t *)(uintptr_t)drv->job.base_addr[i], drv->job.base_addr_size[i]);")
    string(FIND "${instrumented_ethosu_driver}" "${cache_call}" cache_call_offset)
    if(cache_call_offset EQUAL -1)
      message(FATAL_ERROR "Pinned ${operation} cache selector anchor not found")
    endif()
    set(mask "basep_flush_mask")
    if(operation STREQUAL "invalidate")
      set(mask "basep_invalidate_mask")
    endif()
    string(REPLACE "${cache_call}"
      "            h1BaselineCacheSelector(drv->${mask}, (uint32_t)i);\n${cache_call}"
      instrumented_ethosu_driver "${instrumented_ethosu_driver}")
  endforeach()
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_driver_profiled.c"
  "${instrumented_ethosu_driver}")
set(H1_IRQ_SOURCE "${H1_SOURCE_DIR}/ethos_u_fast_memory.c")
set(H1_IRQ_SHA256 "67a5ee1bd04759e1a3ab5dc58fa4f54de8a0574080aa30c1613d34ddd9122024")
file(SHA256 "${H1_IRQ_SOURCE}" actual_h1_irq_sha256)
if(NOT actual_h1_irq_sha256 STREQUAL H1_IRQ_SHA256)
  message(FATAL_ERROR "H1 IRQ-wrapper identity mismatch: ${actual_h1_irq_sha256}")
endif()
file(READ "${H1_IRQ_SOURCE}" instrumented_h1_irq)
string(REPLACE "#include \"h1_memory_contract.h\"\n"
  "#include \"h1_memory_contract.h\"\n#include \"runtime_profile.h\"\n"
  instrumented_h1_irq "${instrumented_h1_irq}")
set(h1_irq_anchor
  "void ethosu_zephyr_irq_handler(const struct device *device)\n{\n\tstruct ethosu_data *data = device->data;")
set(h1_irq_replacement
  "void ethosu_zephyr_irq_handler(const struct device *device)\n{\n\th1ProfileIrqEntry();\n\tstruct ethosu_data *data = device->data;")
string(FIND "${instrumented_h1_irq}" "${h1_irq_anchor}" h1_irq_anchor_offset)
if(h1_irq_anchor_offset EQUAL -1)
  message(FATAL_ERROR "H1 earliest-IRQ insertion anchor not found")
endif()
string(REPLACE "${h1_irq_anchor}" "${h1_irq_replacement}"
  instrumented_h1_irq "${instrumented_h1_irq}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ethos_u_fast_memory_profiled.c"
  "${instrumented_h1_irq}")

get_filename_component(TINYCRYPT_DIR
  "${ZEPHYR_BASE}/../modules/crypto/tinycrypt" ABSOLUTE)
set(TINYCRYPT_SHA256_SOURCE "${TINYCRYPT_DIR}/lib/source/sha256.c")
set(TINYCRYPT_SHA256_HEADER "${TINYCRYPT_DIR}/lib/include/tinycrypt/sha256.h")
set(TINYCRYPT_SHA256_SOURCE_SHA256
  "a8ad4bb2c09e31e4ab86c1b4f8fa073937a42e2694afb4dde6cb1b3c7a4b5ffd")
set(TINYCRYPT_SHA256_HEADER_SHA256
  "90eb15d8cd82f81ba163c53b199c04eb9e5fda52a41901bb380dd80097f57902")
file(SHA256 "${TINYCRYPT_SHA256_SOURCE}" actual_tinycrypt_sha256_source_sha256)
file(SHA256 "${TINYCRYPT_SHA256_HEADER}" actual_tinycrypt_sha256_header_sha256)
if(NOT actual_tinycrypt_sha256_source_sha256 STREQUAL TINYCRYPT_SHA256_SOURCE_SHA256 OR
   NOT actual_tinycrypt_sha256_header_sha256 STREQUAL TINYCRYPT_SHA256_HEADER_SHA256)
  message(FATAL_ERROR
    "Pinned TinyCrypt SHA-256 identity mismatch: source=${actual_tinycrypt_sha256_source_sha256}, header=${actual_tinycrypt_sha256_header_sha256}")
endif()

if(H1_AUDIO_BACKEND STREQUAL "PDM")
set(ALIF_PDM_SOURCE "${ZEPHYR_BASE}/drivers/audio/alif_pdm.c")
set(ALIF_PDM_SHA256 "9e43b24a5cd768ac4751cef4332105e2afb7547ae63160e8139ac80488f2e566")
file(SHA256 "${ALIF_PDM_SOURCE}" actual_alif_pdm_sha256)
if(NOT actual_alif_pdm_sha256 STREQUAL ALIF_PDM_SHA256)
  message(FATAL_ERROR
    "Pinned Alif PDM source identity mismatch: ${actual_alif_pdm_sha256}")
endif()
file(READ "${ALIF_PDM_SOURCE}" instrumented_alif_pdm)

set(alif_pdm_include_anchor [=[#include "alif_pdm_reg.h"
]=])
set(alif_pdm_include_replacement [=[#include "alif_pdm_reg.h"
#include "audio_pdm_driver_hooks.h"
]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_include_anchor}"
  alif_pdm_include_anchor_offset)
if(alif_pdm_include_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM include anchor not found")
endif()
string(REPLACE "${alif_pdm_include_anchor}" "${alif_pdm_include_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_configure_enable_anchor "reg_val |= pdata->channel_map;")
set(alif_pdm_configure_enable_replacement "reg_val &= ~PDM_CHANNEL_ENABLE;")
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_configure_enable_anchor}"
  alif_pdm_configure_enable_anchor_offset)
if(alif_pdm_configure_enable_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM configure-enable anchor not found")
endif()
string(REPLACE "${alif_pdm_configure_enable_anchor}"
  "${alif_pdm_configure_enable_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")
string(REPLACE "/* Enable the PDM multiple channels */"
  "/* Retain the map; trigger START enables channels after soft-clear. */"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_stop_anchor [=[disable_interrupt(dev);]=])
set(alif_pdm_stop_replacement [=[disable_interrupt(dev);
		const uint32_t stop_error_status =
			sys_read32(DEVICE_MMIO_GET(dev) + PDM_ERROR_IRQ);
		if (stop_error_status != 0U) {
			h1PdmDriverFifoError();
		}
		sys_clear_bits(DEVICE_MMIO_GET(dev) + PDM_CONFIG_REGISTER,
			       PDM_CHANNEL_ENABLE);
		(void)sys_read32(DEVICE_MMIO_GET(dev) + PDM_CONFIG_REGISTER);]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_stop_anchor}"
  alif_pdm_stop_anchor_offset)
if(alif_pdm_stop_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM STOP anchor not found")
endif()
string(REPLACE "${alif_pdm_stop_anchor}" "${alif_pdm_stop_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_start_anchor [=[enable_interrupt(dev);]=])
set(alif_pdm_start_replacement [=[sys_clear_bits(DEVICE_MMIO_GET(dev) + PDM_CONFIG_REGISTER,
			       PDM_CHANNEL_ENABLE);
		(void)sys_read32(DEVICE_MMIO_GET(dev) + PDM_CONFIG_REGISTER);
		(void)sys_read32(DEVICE_MMIO_GET(dev) + PDM_ERROR_IRQ);
		(void)sys_read32(DEVICE_MMIO_GET(dev) + PDM_WARN_IRQ);
		sys_set_bits(DEVICE_MMIO_GET(dev) + PDM_CONFIG_REGISTER,
			     pdata->channel_map);
		enable_interrupt(dev);]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_start_anchor}"
  alif_pdm_start_anchor_offset)
if(alif_pdm_start_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM START anchor not found")
endif()
string(REPLACE "${alif_pdm_start_anchor}" "${alif_pdm_start_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_error_anchor [=[	sys_clear_bits(reg_base + PDM_INTERRUPT_REGISTER, PDM_FIFO_OVERFLOW_IRQ);
	(void)sys_read32(reg_base + PDM_ERROR_IRQ);]=])
set(alif_pdm_error_replacement [=[	sys_clear_bits(reg_base + PDM_INTERRUPT_REGISTER, PDM_FIFO_OVERFLOW_IRQ);
	const uint32_t error_status = sys_read32(reg_base + PDM_ERROR_IRQ);
	if (error_status != 0U) {
		h1PdmDriverFifoError();
	}]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_error_anchor}"
  alif_pdm_error_anchor_offset)
if(alif_pdm_error_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM FIFO-error anchor not found")
endif()
string(REPLACE "${alif_pdm_error_anchor}" "${alif_pdm_error_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_slab_anchor [=[		pdm_data->slab_missed++;
		return NULL;]=])
set(alif_pdm_slab_replacement [=[		pdm_data->slab_missed++;
		h1PdmDriverSlabMiss();
		return NULL;]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_slab_anchor}"
  alif_pdm_slab_anchor_offset)
if(alif_pdm_slab_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM slab-miss anchor not found")
endif()
string(REPLACE "${alif_pdm_slab_anchor}" "${alif_pdm_slab_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_overrun_anchor [=[		if (k_msgq_put(&pdmdata->buf_queue, &pdmdata->data_buffer, K_NO_WAIT) != 0) {
			/* Queue full: drop oldest block to make room */]=])
set(alif_pdm_overrun_replacement [=[		if (k_msgq_put(&pdmdata->buf_queue, &pdmdata->data_buffer, K_NO_WAIT) != 0) {
			h1PdmDriverQueueOverrun();
			/* Queue full: drop oldest block to make room */]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_overrun_anchor}"
  alif_pdm_overrun_anchor_offset)
if(alif_pdm_overrun_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM queue-overrun anchor not found")
endif()
string(REPLACE "${alif_pdm_overrun_anchor}" "${alif_pdm_overrun_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")

set(alif_pdm_publish_anchor [=[		}

		pdmdata->data_buffer = get_slab(pdmdata);]=])
set(alif_pdm_publish_replacement [=[		}
		h1PdmDriverBlockPublished(k_msgq_num_used_get(&pdmdata->buf_queue));

		pdmdata->data_buffer = get_slab(pdmdata);]=])
string(FIND "${instrumented_alif_pdm}" "${alif_pdm_publish_anchor}"
  alif_pdm_publish_anchor_offset)
if(alif_pdm_publish_anchor_offset EQUAL -1)
  message(FATAL_ERROR "Pinned Alif PDM block-publication anchor not found")
endif()
string(REPLACE "${alif_pdm_publish_anchor}" "${alif_pdm_publish_replacement}"
  instrumented_alif_pdm "${instrumented_alif_pdm}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/alif_pdm_instrumented.c"
  "${instrumented_alif_pdm}")

get_target_property(alif_audio_sources drivers__audio SOURCES)
set(alif_audio_sources_before "${alif_audio_sources}")
list(FILTER alif_audio_sources EXCLUDE REGEX "(^|/)alif_pdm\\.c$")
if(alif_audio_sources STREQUAL alif_audio_sources_before)
  message(FATAL_ERROR "Could not replace the pinned Alif PDM source")
endif()
list(APPEND alif_audio_sources
  "${CMAKE_CURRENT_BINARY_DIR}/generated/alif_pdm_instrumented.c")
set_property(TARGET drivers__audio PROPERTY SOURCES "${alif_audio_sources}")
target_include_directories(drivers__audio PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src" "${ZEPHYR_BASE}/drivers/audio")

endif()

set(H1_IDENTITY_FILES
  CMakeLists.txt
  README.md
  external-flash.conf
  prj.conf
  psram.conf
  u85-256.conf
  boards/psram-model.overlay
  boards/pdm-mic.overlay
  boards/usb-cdc.overlay
  src/audio_contract.hpp
  src/audio_pdm.cpp
  src/mic_usb.cpp
  src/audio_pdm.hpp
  src/audio_pdm_driver_hooks.h
  src/mic_usb.hpp
  src/pcm_ring.cpp
  src/pcm_ring.hpp
  src/sha256.cpp
  src/sha256.hpp
  src/run_state.cpp
  src/run_state.hpp
  src/sliding_window.cpp
  src/sliding_window.hpp
  src/frontend.cpp
  src/frontend.hpp
  src/frontend_core.hpp
  src/frontend_storage.hpp
  src/frontend_spectral_core.hpp
  src/frontend_m55_spectral.cpp
  src/frontend_m55_spectral.hpp
  src/frontend_diagnostics.cpp
  src/frontend_diagnostics.hpp
  src/numeric_error_metrics.cpp
  src/numeric_error_metrics.hpp
  src/classifier_bridge.cpp
  src/classifier_bridge.hpp
  boards/audio-no-pdm.overlay
  boards/u85-ospi.overlay
  src/h1_contract.h
  src/main.cpp
  src/model_storage.cpp
  src/model_storage.hpp
  src/runtime_profile.c
  src/runtime_profile.h
  src/baseline_profile.c
  src/baseline_profile.h
  src/baseline_cache_operations.h
  src/baseline_profile.ld
  src/usb_transport.cpp
  src/usb_transport.hpp
  src/build_identity.h.in
  src/fixtures.S.in
)
set(H1_SOURCE_IDENTITY_MATERIAL "")
string(APPEND H1_SOURCE_IDENTITY_MATERIAL "audio_backend:${H1_AUDIO_BACKEND}\nfrontend_diagnostics:${H1_FRONTEND_DIAGNOSTICS}\n")
foreach(identity_file IN LISTS H1_IDENTITY_FILES)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${identity_file}" identity_sha256)
  string(APPEND H1_SOURCE_IDENTITY_MATERIAL "${identity_file}:${identity_sha256}\n")
endforeach()
foreach(reused_file
    h1_memory_contract.h h1_memory.cpp h1_memory.hpp h1_memory.ld ethos_u_fast_memory.c)
  file(SHA256 "${H1_SOURCE_DIR}/${reused_file}" identity_sha256)
  string(APPEND H1_SOURCE_IDENTITY_MATERIAL
    "production_memory/${reused_file}:${identity_sha256}\n")
endforeach()
foreach(reused_file h1_gem.cpp h1_gem.hpp shared_feature.hpp)
  file(SHA256 "${H1_SOURCE_DIR}/${reused_file}" identity_sha256)
  string(APPEND H1_SOURCE_IDENTITY_MATERIAL
    "production_gem/${reused_file}:${identity_sha256}\n")
endforeach()
if(H1_AUDIO_BACKEND STREQUAL "PDM")
  string(APPEND H1_SOURCE_IDENTITY_MATERIAL "pinned_zephyr/alif_pdm.c:${ALIF_PDM_SHA256}\n")
endif()
string(APPEND H1_SOURCE_IDENTITY_MATERIAL "pinned_tinycrypt/sha256.c:${TINYCRYPT_SHA256_SOURCE_SHA256}\n")
string(APPEND H1_SOURCE_IDENTITY_MATERIAL "pinned_tinycrypt/sha256.h:${TINYCRYPT_SHA256_HEADER_SHA256}\n")
string(SHA256 H1_SOURCE_BUNDLE_SHA256 "${H1_SOURCE_IDENTITY_MATERIAL}")
file(SHA256 "${H1_SOURCE_DIR}/classifier_bridge.cpp" H1_BRIDGE_IMPLEMENTATION_SHA256)
configure_file(src/build_identity.h.in generated/build_identity.h @ONLY)

target_include_directories(app PRIVATE
  src
  "${H1_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
# The pinned Alif MEMC source directly includes the OSPI HAL public header;
# expose the matching pinned module include directory to that Zephyr library.
get_filename_component(ALIF_HAL_DIR "${ZEPHYR_BASE}/../modules/hal/alif" ABSOLUTE)
if(TARGET drivers__memc)
  target_include_directories(drivers__memc PRIVATE
    "${ALIF_HAL_DIR}/drivers/ospi/include")
  if(NOT CONFIG_USE_ALIF_HAL_OSPI)
    target_sources(drivers__memc PRIVATE
      "${ALIF_HAL_DIR}/drivers/ospi/src/ospi.c"
      "${ALIF_HAL_DIR}/drivers/ospi/src/ospi_hal.c")
  endif()
endif()

target_sources(app PRIVATE
  src/pcm_ring.cpp
  src/run_state.cpp
  src/sha256.cpp
  src/sliding_window.cpp
  src/frontend.cpp
  src/frontend_m55_spectral.cpp
  src/numeric_error_metrics.cpp
  src/classifier_bridge.cpp
  src/main.cpp
  src/model_storage.cpp
  src/runtime_profile.c
  src/baseline_profile.c
  src/usb_transport.cpp
  "${H1_SOURCE_DIR}/h1_memory.cpp"
  "${H1_SOURCE_DIR}/h1_gem.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/h1_fixtures.S"
)
if(H1_AUDIO_BACKEND STREQUAL "PDM")
  target_sources(app PRIVATE src/audio_pdm.cpp src/mic_usb.cpp)
endif()
if(H1_FRONTEND_DIAGNOSTICS)
  target_sources(app PRIVATE src/frontend_diagnostics.cpp)
  set_source_files_properties(src/main.cpp src/model_storage.cpp
    PROPERTIES COMPILE_DEFINITIONS H1_FRONTEND_DIAGNOSTICS=1)
endif()
target_compile_options(app PRIVATE -fno-fast-math -ffp-contract=off)

zephyr_linker_sources(SECTIONS "${H1_SOURCE_DIR}/h1_memory.ld")
zephyr_linker_sources(SECTIONS "${H1_SOURCE_DIR}/baseline_profile.ld")
zephyr_link_libraries(-Wl,--fatal-warnings -Wl,--check-sections)

# Real Alif callback-body observation. Preserve every original operation and
# select its actual branch; no linker wrapper/interposition is used.
if(H1_BASELINE_V2_INSTRUMENTATION)
  target_compile_definitions(app PRIVATE H1_BASELINE_V2_INSTRUMENTATION=1)
  set(alif_cache_source "${ZEPHYR_BASE}/../modules/hal/alif/drivers/ethos_u/src/ethosu_callback.c")
  file(SHA256 "${alif_cache_source}" alif_cache_sha256)
  if(NOT alif_cache_sha256 STREQUAL
      "f47577a6c6b1276d54b1f427ed134d79a96390e20a0d33074c17b93fdccd45a7")
    message(FATAL_ERROR "Pinned Alif cache callback identity mismatch")
  endif()
  file(READ "${alif_cache_source}" alif_cache_text)
  string(FIND "${alif_cache_text}" "\nvoid ethosu_flush_dcache" flush_offset)
  string(FIND "${alif_cache_text}" "\nvoid ethosu_invalidate_dcache" invalidate_offset)
  if(flush_offset LESS 0 OR invalidate_offset LESS flush_offset)
    message(FATAL_ERROR "Pinned real cache callback boundaries missing")
  endif()
  string(SUBSTRING "${alif_cache_text}" 0 ${flush_offset} callback_prefix)
  math(EXPR flush_length "${invalidate_offset} - ${flush_offset}")
  string(SUBSTRING "${alif_cache_text}" ${flush_offset} ${flush_length} flush_body)
  string(SUBSTRING "${alif_cache_text}" ${invalidate_offset} -1 invalidate_body)
  foreach(callback flush invalidate)
    set(operations "SCB_CleanDCache_by_Addr(p, bytes)|h1BaselineCleanRange;SCB_CleanDCache()|h1BaselineCleanWhole;__DSB()|h1BaselineFlushBarrier")
    set(begin "H1_EVENT_PRE_SUBMIT_CACHE_BEGIN")
    set(end "H1_EVENT_PRE_SUBMIT_CACHE_END")
    if(callback STREQUAL "invalidate")
      set(operations "SCB_InvalidateDCache_by_Addr(p, bytes)|h1BaselineInvalidateRange;SCB_CleanInvalidateDCache()|h1BaselineCleanInvalidateWhole;__DSB()|h1BaselineInvalidateBarrier")
      set(begin "H1_EVENT_WAIT_CACHE_BEGIN")
      set(end "H1_EVENT_WAIT_CACHE_END")
    endif()
    foreach(operation IN LISTS operations)
      string(REPLACE "|" ";" fields "${operation}")
      list(GET fields 0 call)
      list(GET fields 1 observer)
      string(FIND "${${callback}_body}" "${call};" call_offset)
      if(call_offset LESS 0)
        message(FATAL_ERROR "Pinned ${callback} cache operation missing: ${call}")
      endif()
      string(REPLACE "${call};"
        "struct H1BaselineCache *observation = ${observer}(p, (uint32_t)bytes);\n\t\t\t${call};\n\t\t\th1BaselineCacheEnd(observation, ${end});"
        ${callback}_body "${${callback}_body}")
    endforeach()
  endforeach()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_callback_profiled.c"
    "#include \"baseline_cache_operations.h\"\n${callback_prefix}${flush_body}${invalidate_body}")
  set(callback_target "..__modules__hal__alif__drivers__ethos_u")
  get_target_property(callback_sources ${callback_target} SOURCES)
  set(callback_replaced "")
  set(callback_found FALSE)
  foreach(source IN LISTS callback_sources)
    if(source MATCHES "ethosu_callback\\.c$")
      list(APPEND callback_replaced "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_callback_profiled.c")
      set(callback_found TRUE)
    else()
      list(APPEND callback_replaced "${source}")
    endif()
  endforeach()
  if(NOT callback_found)
    message(FATAL_ERROR "Active Alif cache callback build source missing")
  endif()
  set_property(TARGET ${callback_target} PROPERTY SOURCES "${callback_replaced}")
  target_include_directories(${callback_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

  set(custom_op_source "${ZEPHYR_BASE}/../optional/modules/lib/tflite-micro/tensorflow/lite/micro/kernels/ethos_u/ethosu.cc")
  file(SHA256 "${custom_op_source}" custom_op_sha256)
  if(NOT custom_op_sha256 STREQUAL
      "d4df23ddab651ea3f653a75d296d03f98a63eda1d0261d2edca5891499f47e67")
    message(FATAL_ERROR "Pinned Ethos-U custom-op identity mismatch")
  endif()
  file(READ "${custom_op_source}" custom_op_text)
  set(scope_definition "\nclass H1BaselineCustomScope {\n public:\n  explicit H1BaselineCustomScope(uint32_t end) : end_(end) { h1BaselineObserve(end - 1, 0); }\n  ~H1BaselineCustomScope() { h1BaselineObserve(end_, 0); }\n private:\n  uint32_t end_;\n};\n")
  string(REPLACE "namespace tflite {" "${scope_definition}\nnamespace tflite {"
    custom_op_text "${custom_op_text}")
  foreach(operation Init Prepare)
    set(custom_anchor "void* Init(TfLiteContext* context, const char* buffer, size_t length) {")
    set(custom_end "H1_EVENT_CUSTOM_OP_INIT_END")
    if(operation STREQUAL "Prepare")
      set(custom_anchor "TfLiteStatus Prepare(TfLiteContext* context, TfLiteNode* node) {")
      set(custom_end "H1_EVENT_CUSTOM_OP_PREPARE_END")
    endif()
    string(FIND "${custom_op_text}" "${custom_anchor}" custom_offset)
    if(custom_offset LESS 0)
      message(FATAL_ERROR "Pinned custom-op ${operation} anchor missing")
    endif()
    string(REPLACE "${custom_anchor}" "${custom_anchor}\n  H1BaselineCustomScope observation(${custom_end});"
      custom_op_text "${custom_op_text}")
  endforeach()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_custom_op_profiled.cc"
    "#include \"baseline_profile.h\"\n${custom_op_text}")
  get_target_property(custom_sources modules__tflite-micro SOURCES)
  set(custom_replaced "")
  set(custom_found FALSE)
  foreach(source IN LISTS custom_sources)
    if(source MATCHES "/ethos_u/ethosu\\.cc$")
      list(APPEND custom_replaced "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_custom_op_profiled.cc")
      set(custom_found TRUE)
    else()
      list(APPEND custom_replaced "${source}")
    endif()
  endforeach()
  if(NOT custom_found)
    message(FATAL_ERROR "Active Ethos-U custom-op build source missing")
  endif()
  set_property(TARGET modules__tflite-micro PROPERTY SOURCES "${custom_replaced}")
  target_include_directories(modules__tflite-micro PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

# Exact cache/fast-memory reuse plus the qualified earliest-IRQ timestamp hook.
set_property(TARGET drivers__misc__ethos_u APPEND PROPERTY INCLUDE_DIRECTORIES
  "${H1_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_property(TARGET drivers__misc__ethos_u PROPERTY SOURCES
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ethos_u_fast_memory_profiled.c")

# Replace the U85 device implementation and core-driver cache loop with
# source-hash-guarded generated copies; the pinned SDK checkout is unchanged.
get_target_property(ethosu_core_sources ethosu_core_driver SOURCES)
set(ethosu_core_sources_before_u85 "${ethosu_core_sources}")
list(FILTER ethosu_core_sources EXCLUDE REGEX "ethosu_device_u85\\.c$")
if(ethosu_core_sources STREQUAL ethosu_core_sources_before_u85)
  message(FATAL_ERROR "Could not replace the pinned Ethos-U85 device source")
endif()
set(ethosu_core_sources_before_driver "${ethosu_core_sources}")
list(FILTER ethosu_core_sources EXCLUDE REGEX "ethosu_driver\\.c$")
if(ethosu_core_sources STREQUAL ethosu_core_sources_before_driver)
  message(FATAL_ERROR "Could not replace the pinned Ethos-U core-driver source")
endif()
list(APPEND ethosu_core_sources
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_driver_profiled.c"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ethosu_device_u85_profiled.c")
set_property(TARGET ethosu_core_driver PROPERTY SOURCES "${ethosu_core_sources}")
target_include_directories(ethosu_core_driver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

message(STATUS "H1 frontend: ${H1_FRONTEND_SHA256} (${H1_FRONTEND_BYTES} bytes)")
message(STATUS "H1 compiled backbone: ${H1_BACKBONE_SHA256} (${H1_BACKBONE_BYTES} bytes)")
message(STATUS "H1 compiled classifier: ${H1_CLASSIFIER_SHA256} (${H1_CLASSIFIER_BYTES} bytes)")
message(STATUS "H1 Ethos-U85 timing authority: ${ETHOSU_U85_SHA256}")
message(STATUS "H1 Ethos-U driver timing authority: ${ETHOSU_DRIVER_SHA256}")
message(STATUS "H1 IRQ timing authority: ${H1_IRQ_SHA256}")
message(STATUS "H1 firmware source bundle: ${H1_SOURCE_BUNDLE_SHA256}")
message(STATUS "H1 TinyCrypt SHA-256 source authority: ${TINYCRYPT_SHA256_SOURCE_SHA256}")
message(STATUS "H1 TinyCrypt SHA-256 header authority: ${TINYCRYPT_SHA256_HEADER_SHA256}")
