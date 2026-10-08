/* SPDX-License-Identifier: Apache-2.0 */
/* Save/restore the enclosing linker cursor; never consume production SRAM1. */
frontendObserverSavedCursor = ABSOLUTE(.);
}
MEMORY { H1_FRONTEND_OBSERVER (rw) : ORIGIN = 0x02520000, LENGTH = 851968 }
SECTIONS
{
.h1_frontend_observer 0x02520000 (NOLOAD) :
{
 __h1_frontend_observer_start = .;
 KEEP(*(.h1_frontend_observer))
 __h1_frontend_observer_end = .;
} > H1_FRONTEND_OBSERVER
. = frontendObserverSavedCursor;
ASSERT(ADDR(.h1_frontend_observer)==0x02520000, "frontend observer origin")
ASSERT(SIZEOF(.h1_frontend_observer)==823360, "frontend observer ABI allocation")
ASSERT(ALIGNOF(.h1_frontend_observer)==32, "frontend observer alignment")
ASSERT(h1FrontendObserverStorage==__h1_frontend_observer_start, "frontend observer sole object")
ASSERT(__h1_frontend_observer_end<=0x025f0000, "frontend observer reservation")
ASSERT(ORIGIN(H1_FRONTEND_OBSERVER)>=ORIGIN(SRAM1) &&
       ORIGIN(H1_FRONTEND_OBSERVER)+LENGTH(H1_FRONTEND_OBSERVER)<=ORIGIN(SRAM1)+LENGTH(SRAM1), "frontend observer SRAM1 bounds")
ASSERT(__h1_production_result_end<=__h1_frontend_observer_start, "frontend observer metadata overlap")
ASSERT(__h1_waveform_lifecycle_end<=__h1_frontend_observer_start, "frontend observer waveform overlap")
ASSERT(__h1_validation_observer_end<=__h1_frontend_observer_start, "frontend observer validation overlap")
ASSERT(__h1_postprocess_observer_end<=__h1_frontend_observer_start, "frontend observer postprocessing overlap")
ASSERT(__h1_baseline_end<=__h1_frontend_observer_start, "frontend observer baseline overlap")
ASSERT(__SRAM1_end<=__h1_frontend_observer_start, "frontend observer production overlap")
