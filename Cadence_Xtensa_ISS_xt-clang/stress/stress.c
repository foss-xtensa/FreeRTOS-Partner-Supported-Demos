/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/******************************************************************************
 * NOTE:  This file only contains the source code that is specific to the
 * basic demo.  Generic functions, such FreeRTOS hook functions, are defined
 * in main.c.
 ******************************************************************************
 */


/* Standard includes. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Kernel includes. */
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <timers.h>
#include <semphr.h>

/* Standard demo includes. */
#include "TestRunner.h"

/* For smaller Xtensa I/O APIs */
#include <xtensa/xtutil.h>

#include "../common/application_code/cadence_code/testcommon.h"


/*-----------------------------------------------------------*/

/**
 * Start all the tests.
 *
 * Note that this function starts the scheduler and therefore, never returns.
 */
extern void vStartTests( void );

/**
 * Prototypes for the standard FreeRTOS application hook (callback) functions
 * implemented within this file.
 *
 * @see http://www.freertos.org/a00016.html
 */
void vApplicationMallocFailedHook( void );
void vApplicationStackOverflowHook( TaskHandle_t pxTask,
                                    char * pcTaskName );
void vApplicationTickHook( void );

/**
 * The function called from the tick hook.
 *
 * @note Only the comprehensive demo uses application hook (callback) functions.
 *
 * @see http://www.freertos.org/a00016.html
 */
void vFullDemoTickHookFunction( void );
/*-----------------------------------------------------------*/

int main( void )
{
    /* Startup and Hardware initialization. */
#if ( configNUMBER_OF_CORES > 1 )
    // Start scheduler on (cores > 0) before issuing libc calls, e.g. printf()
    if (portGET_CORE_ID() > 0) {
        portDISABLE_INTERRUPTS();
        (void) xPortStartScheduler();

        // If we got here then scheduler failed.
        xt_printf( "xPortStartScheduler FAILED!\n" );
        test_exit(-1);
    }
    // SMP build of stress test triggers different assertions.
    // It appears to have assumptions that some tasks need to
    // run on the same core.  YMMV.
    xt_printf("WARNING: stress test not recommended for SMP build\n", CONFIG_VERIF);
    xt_printf("SMP stress test %d starting\n", CONFIG_VERIF);
#else
    xt_printf("Stress test %d starting\n", CONFIG_VERIF);
#endif
#if (defined configSTRESS_TEST_CONTINUOUS) && configSTRESS_TEST_CONTINUOUS
    xt_printf("Will run indefinitely...\n");
#endif


    /* Start tests. */
    vStartTests();

    /* Should never reach here. */
    for( ; ; );

    return 0;
}
/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
    /* vApplicationMallocFailedHook() will only be called if
     * configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
     * function that will get called if a call to pvPortMalloc() fails.
     * pvPortMalloc() is called internally by the kernel whenever a task, queue,
     * timer or semaphore is created.  It is also called by various parts of the
     * demo application.  If heap_1.c, heap_2.c or heap_4.c is being used, then
     * the size of the heap available to pvPortMalloc() is defined by
     * configTOTAL_HEAP_SIZE in FreeRTOSConfig.h, and the xPortGetFreeHeapSize()
     * API function can be used to query the size of free heap space that remains
     * (although it does not provide information on how the remaining heap might be
     * fragmented). See http://www.freertos.org/a00111.html for more information.
     */
    vAssertCalled( __FILE__, __LINE__ );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask,
                                    char * pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) pxTask;

    /* Run time stack overflow checking is performed if
     * configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
     * function is called if a stack overflow is detected. */
    vAssertCalled( __FILE__, __LINE__ );
}
/*-----------------------------------------------------------*/
