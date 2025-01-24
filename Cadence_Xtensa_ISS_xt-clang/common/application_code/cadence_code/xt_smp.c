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
* This test starts a couple of tasks on one core and compares their runtime with running
* the same tasks on multiple cores.
*
*********************************************************************************************************
*/

#include    <ctype.h>
#include    <string.h>
#include    <unistd.h>
#include    <assert.h>
#include    <stdio.h>
#include    <math.h>

#ifdef XT_BOARD
#include    <xtensa/xtbsp.h>
#endif
#include    <xtensa/xtutil.h>

#include "testcommon.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


/* Create task as privileged if MPU enabled. */
#define TASK_INIT_PRIO          (20 | portPRIVILEGE_BIT)
#define INIT_TASK_STK_SIZE      ((XT_STACK_MIN_SIZE + 0x800) / sizeof(StackType_t))

#define TEST_TASK_COUNT         configNUMBER_OF_CORES
#define TEST_TASK_LOOPS         500
#define TEST_TASK_SLEEPS        10


/* Some basic task synchronization that relies on coherent shared memory */
volatile uint32_t task_done[configNUMBER_OF_CORES];

/* Initialize task control flags */
static void task_ctrl_init(void)
{
    int i;
    for (i = 0; i < configNUMBER_OF_CORES; i++) {
        task_done[i] = 0;
    }
}

/* Signal task "task_id" is done */
static void task_ctrl_signal_done(int task_id) {
    task_done[task_id] = 1;
}

/* Wait (yielding) until all tasks are done */
static void task_ctrl_wait_yield(void)
{
    int core = xthal_get_coreid();
    while (1) {
        int i;
        for (i = 0; i < TEST_TASK_COUNT; i++) {
            if (task_done[i] == 0) {
                break;  // still running
            }
        }
        if (i == TEST_TASK_COUNT) {
            break;
        }
        if (task_done[core] == 0) {
            taskYIELD();
        }
    }
}


/* Task to create some noticeable processor activity */
static void Test_Task(void *pdata)
{
    int i, j;
    float x = 1.0;
    double y = 1.0;
    char statstr[4] = ".";

    xt_printf("\nTask %d: Starting on core %d...\n",
              (int)pdata, xthal_get_coreid());

    for (i = 0; i < TEST_TASK_LOOPS; i++) {
        statstr[0] = '0' + (int)pdata;
        xt_printf(statstr);
        for (j = 0; j < 100; j++) {
            x += sqrt(3.141592654 * (3.0 + (float)i));
            y += sqrt((double)3.141592654 * (double)(3.0 + (double)i));
        }
        if ((i % TEST_TASK_SLEEPS) == 0) {
            statstr[0] = 's' + (int)pdata;
            xt_printf(statstr);
            taskYIELD();
        }
    }

    xt_printf("\nTask %d: %d\n", (int)pdata, (x + y == 0.0) ? 0 : 1);
    task_ctrl_signal_done((int)pdata);
    vTaskDelete(NULL);
}


static void Core_Task(void *pdata)
{
    TaskHandle_t tasks[configNUMBER_OF_CORES];
    int err, i;
    long start_ticks = 0, total_ticks_1core = 0, total_ticks_allcores = 0;

    xt_printf("Core_Task started on core %d\n", xthal_get_coreid());
    UNUSED(pdata);

    if (xthal_get_coreid() > 0) {
        xt_printf("err\n");
        exit(-1);
    }

    /* Create test tasks running on the same core as a control test */
    task_ctrl_init();
    start_ticks = xTaskGetTickCount();
    for (i = TEST_TASK_COUNT - 1; i >= 0; i--) {
        xt_printf("Creating task %d on core %d\n", i, 0);
        err = xTaskCreateAffinitySet(Test_Task, 
                                     "Test_Task", 
                                     INIT_TASK_STK_SIZE, 
                                     (void *)i, 
                                     TASK_INIT_PRIO, 
                                     1,     // core 0 only
                                     &(tasks[i]));
        if (err != pdPASS) {
            xt_printf(" FAILED to create Test_Task (core 0)\n");
            goto done;
        }
        while ((xTaskGetTickCount() + TEST_TASK_COUNT - i) <= start_ticks) {
            // Wait for timer tick to be sure other tasks are running
        }
    }
    task_ctrl_wait_yield();
    total_ticks_1core = xTaskGetTickCount() - start_ticks;
    xt_printf("\nsingle-core ticks: %d\n", total_ticks_1core);

    /* Create test tasks running on multiple cores */
    task_ctrl_init();
    start_ticks = xTaskGetTickCount();
    for (i = TEST_TASK_COUNT - 1; i >= 0; i--) {
        xt_printf("Creating task %d on core %d\n", i, i);
        err = xTaskCreateAffinitySet(Test_Task, 
                                     "Test_Task", 
                                     INIT_TASK_STK_SIZE, 
                                     (void *)i, 
                                     TASK_INIT_PRIO, 
                                     1 << i, // 1 task per core
                                     &(tasks[i]));
        if (err != pdPASS) {
            xt_printf(" FAILED to create Test_Task (all cores)\n");
            goto done;
        }
        while ((xTaskGetTickCount() + TEST_TASK_COUNT - i) <= start_ticks) {
            // Wait for timer tick to be sure other tasks are running
        }
    }
    task_ctrl_wait_yield();
    total_ticks_allcores = xTaskGetTickCount() - start_ticks;
    xt_printf("\nmulti-core ticks: %d\n", total_ticks_allcores);

    /* Somewhat arbitrary check to confirm multi-core time is faster than single-core */
    if ((total_ticks_allcores * 2) < total_ticks_1core) {
        xt_printf("PASSED!\n");
    } else {
        xt_printf("FAILED\n");
    }

done:
    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    exit(0);

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
    UNUSED(xTask);
    UNUSED(pcTaskName);
    puts("\nStack overflow, stopping.");
    exit(0);
}

int main(void)
#else
int main_xt_smp(int argc, char *argv[])
#endif
{
    int err = 0;
    int exit_code = 0;
    TaskHandle_t handle;

    /* Display some core-specific output */
    int core = xthal_get_coreid();
    xt_printf("Test starting on core %d\n", core);

    if (xthal_get_coreid() == 0) {
        /* Create the control task initially with the high priority. */
        err = xTaskCreate(Core_Task, 
                          "Core_Task", 
                          INIT_TASK_STK_SIZE, 
                          NULL, 
                          TASK_INIT_PRIO, 
                          &handle);
        if (err != pdPASS)
        {
            xt_printf(" FAILED to create Core_Task\n");
            goto done;
        }
        vTaskCoreAffinitySet(handle, 1);    // Core 0 only
        xt_printf("Created Core_Task on core 0\n");

        /* Start task scheduler */
        xt_printf("Scheduler starting on core %d\n", core);
        vTaskStartScheduler();
    } else {
        /* Start task scheduler */
        xt_printf("Scheduler starting on core %d\n", core);
        portDISABLE_INTERRUPTS();
        (void) xPortStartScheduler();
    }

done:
    exit_code = err;

    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    exit(exit_code);

    /* Does not reach here ('return' statement keeps compiler happy). */
    return 0;
}

