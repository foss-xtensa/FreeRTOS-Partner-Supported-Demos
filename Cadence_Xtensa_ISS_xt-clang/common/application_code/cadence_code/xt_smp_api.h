/*******************************************************************************
// Copyright (c) 2003-2025 Cadence Design Systems, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
--------------------------------------------------------------------------------
*/

/*
 * Xtensa-specific SMP APIs, e.g. power management and cache coherence support.
 * Assumes access to several Xtensa port APIs and data structures.
 */

#include "FreeRTOS.h"

#if ( configNUMBER_OF_CORES <= 1 )
#error SMP Power APIs require > 1 core
#endif


/*
 * IPI interrupts used for multicore scheduler
 */
extern const uint32_t xt_ipi_intnum[configNUMBER_OF_CORES];


/*
 * Low-level interrupt enable/disable functions (for IPIs)
 */
extern void xt_interrupt_enable( uint32_t intnum );
extern void xt_interrupt_disable( uint32_t intnum );


/*
 * SMP power-management APIs -- Detach current core from scheduler
 */
static inline void
xt_smp_scheduler_detach(void)
{
    /* Cannot be called on core 0 */
    configASSERT( portGET_CORE_ID() > 0 );

    /* Ignore all yield requests from core 0 */
    xt_interrupt_disable( xt_ipi_intnum[0] );
}


/*
 * SMP power-management APIs -- Reattach current core to scheduler
 * Likely to result in an interrupt and a context switch upon returning
 */
static inline void
xt_smp_scheduler_reattach(void)
{
    /* Cannot be called on core 0 */
    configASSERT( portGET_CORE_ID() > 0 );

    /* Start handling yield requests from core 0 */
    xt_interrupt_enable( xt_ipi_intnum[0] );
}

