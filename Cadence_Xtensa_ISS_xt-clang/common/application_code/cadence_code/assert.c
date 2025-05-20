
//-----------------------------------------------------------------------------
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
//-----------------------------------------------------------------------------

/* Scheduler include files. */
#include <stdlib.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <xtensa/xtutil.h>

/*-----------------------------------------------------------*/

void vAssertCalled( const char * pcFile,
                    unsigned long ulLine )
{
    volatile unsigned long ul = 0;

    ( void ) pcFile;
    ( void ) ulLine;

    // For easier debug... all demos are linked with libxtutil
#if (configNUMBER_OF_CORES == 1)
    xt_printf("vAssertCalled: %s:%d\n", pcFile, ulLine);
#else
    xt_printf("vAssertCalled (core %d): %s:%d\n", portGET_CORE_ID(), pcFile, ulLine);
#endif

    taskENTER_CRITICAL();
    {
        /* Set ul to a non-zero value using the debugger to step out of this
         * function. */
        while( ul == 0 )
        {
            portNOP();
        }
    }
    taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/

#if (defined SMP_TEST)

// For SMP tests, include exit handling logic to gracefully stop XTSC
// instead of allowing idle tasks and timer interrupts to run indefinitely
//
// Call exit() only on core 0 and _exit() on other cores.

volatile int test_exit_called = 0;
volatile int test_exit_code;

// When called directly, call exit() on core 0 and _exit() on other cores
void test_exit(int code)
{
    test_exit_code = code;
    test_exit_called = 1;
#if (configNUMBER_OF_CORES > 1)
    if (portGET_CORE_ID() > 0) {
        _exit(code);
    }
#endif
    exit(code);
}   

// Use idle hook to check if test_exit() was called only on another core
#if (configUSE_PASSIVE_IDLE_HOOK != 0)
void vApplicationPassiveIdleHook( void )
{
    if (test_exit_called) {
        test_exit(test_exit_code);
    }
}
#endif  // configUSE_PASSIVE_IDLE_HOOK

#else   // SMP_TEST

void test_exit(int code)
{
    exit(code);
}

#endif  // SMP_TEST

#if (defined PROFILE_CONTEXT_SWITCH)

// Higher accuracy profiling requires context switch trace hooks.
// Provide default weak implementation which can be overridden.

void __attribute__((weak))
test_trace_task_switched_in(void)
{
}

void __attribute__((weak))
test_trace_task_switched_out(void)
{
}

#endif  // PROFILE_CONTEXT_SWITCH

