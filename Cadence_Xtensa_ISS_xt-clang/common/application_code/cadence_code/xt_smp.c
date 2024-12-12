/*******************************************************************************
// Copyright (c) 2003-2024 Cadence Design Systems, Inc.
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
*********************************************************************************************************
*
*                                            SMP TEST
*
* This test starts a couple of tasks on different cores and prints their core IDs.
*
*********************************************************************************************************
*/

#include    <ctype.h>
#include    <string.h>
#include    <unistd.h>
#include    <alloca.h>
#include    <assert.h>
#include    <stdio.h>

#ifdef XT_BOARD
#include    <xtensa/xtbsp.h>
#endif

#include "testcommon.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
*********************************************************************************************************
*                                             LOCAL FUNCTIONS
*********************************************************************************************************
*/

/* Output a simple string to the console. */
static void putstr(const char *s)
{
    char c;

    while ((c = *s) != '\0') {
        if (c == '\n') {
            outbyte('\r');
            outbyte('\n');
        }
        else if (iscntrl((int)c) && c != '\r') {
            outbyte('^');
            outbyte('@' + c);
        }
        else outbyte(c);
        ++s;
    }
}

/*
*********************************************************************************************************
*                                          APP INITIALZATION TASK
*
*
*********************************************************************************************************
*/

/* Create task as privileged if MPU enabled. */
#define TASK_INIT_PRIO          (20 | portPRIVILEGE_BIT)
#define INIT_TASK_STK_SIZE      ((XT_STACK_MIN_SIZE + 0x800) / sizeof(StackType_t))

const char *basestr = "Hello from Core X\n";


static void Core_Task(void *pdata)
{
    int core;
    char corestr[64];
    char *p;

    UNUSED(pdata);

    /* Call a function that does an alloca over my base save area. */
    core = xthal_get_coreid();
    strcpy(corestr, basestr);
    p = strchr(corestr, 'X');
    *p = '0' + core;
    putstr(corestr);
    putstr("PASSED!\n");

    #ifdef XT_SIMULATOR
    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    _exit(0);
    #endif

    /* Terminate this task. OS will continue to run timer, stats and idle tasks. */
    vTaskDelete(NULL);
    // Should never come here.
    assert(0);
}

/*
*********************************************************************************************************
*                                             C ENTRY POINT
*
* Initializes FreeRTOS after the platorm's run-time system has initialized the basic platform.
* Creates at least the first task, which can then create other tasks.
* Starts multitasking.
*
*********************************************************************************************************
*/

/* Hook functions for standalone tests */
#ifdef STANDALONE

#if configUSE_TICK_HOOK
void vApplicationTickHook( void )
{
}
#endif

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    /* For some reason printing pcTaskName is not working */
    UNUSED(xTask);
    UNUSED(pcTaskName);
    puts("\nStack overflow, stopping.");
    exit(0);
}

int main(void)
#else
int main_xt_alloca(int argc, char *argv[])
#endif
{
    int err = 0;
    int exit_code = 0;
    int i;
    TaskHandle_t handles[configNUMBER_OF_CORES];

    putstr("Test running...\n");

    for (i = 0; i < configNUMBER_OF_CORES; i++) {
        /* Create the control task initially with the high priority. */
        err = xTaskCreate(Core_Task, 
                          "Core_Task", 
                          INIT_TASK_STK_SIZE, 
                          NULL, 
                          TASK_INIT_PRIO, 
                          &(handles[i]));
        if (err != pdPASS)
        {
            putstr(" FAILED to create Init_Task\n");
            goto done;
        }
        vTaskCoreAffinitySet(handles[i], 1 << i);
    }

    /* Start task scheduler */
    vTaskStartScheduler();

done:
    exit_code = err;

    #ifdef XT_SIMULATOR
    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    _exit(exit_code);
    #endif

    /* Does not reach here ('return' statement keeps compiler happy). */
    return 0;
}

