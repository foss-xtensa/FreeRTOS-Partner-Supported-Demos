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
*********************************************************************************************************
*
*                                            SMP TESTS
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
#include "semphr.h"

#if ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 0 )
#error configUSE_CORE_AFFINITY required for this test in SMP mode
#endif


/* Create task as privileged if MPU enabled. */
#define TASK_INIT_PRIO          (20 | portPRIVILEGE_BIT)
#define INIT_TASK_STK_SIZE      ((XT_STACK_MIN_SIZE + 0x800) / sizeof(StackType_t))

#define TEST_TASK_COUNT         configNUMBER_OF_CORES
#define TEST_TASK_LOOPS         100
#define TEST_TASK_SLEEPS        10


/* Some basic task synchronization that relies on coherent shared memory */
volatile uint32_t task_done[configNUMBER_OF_CORES];

/* Global structures used for SMP tests */
static SemaphoreHandle_t xSemA, xSemB;


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
    int core = portGET_CORE_ID();
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
              (int)pdata, portGET_CORE_ID());

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


// SMP Task Test
// -------------
// Run several tasks that take a while to complete on core 0 and measure
// their execution time.  Next, move each task to its own core and rerun
// the same workload.  The result should be significantly faster, although
// precisely how much faster depends on the scheduler.
//
// NOTE: tasks will not move between cores once started.
//
static int run_task_test(void)
{
    TaskHandle_t tasks[configNUMBER_OF_CORES];
    int err, i;
    long start_ticks = 0, total_ticks_1core = 0, total_ticks_allcores = 0;

    xt_printf("SMP Task test started on core %d\n", portGET_CORE_ID());

    if (portGET_CORE_ID() > 0) {
        xt_printf("err\n");
        test_exit(-1);
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
    if ((total_ticks_allcores * 2 * 9 / 10) < total_ticks_1core) {
        xt_printf("\nSMP Task test succeeded\n");
        return 0;
    }

done:
    xt_printf("\nSMP Task test FAILED\n");
    return 1;
}


/* Task to decrement and increment semaphores.  Will move between cores. */
static void Sem_Task(void *pdata)
{
    int core_swaps = 0;
    int i;

    xt_printf("Sem Task: Starting on core %d...\n", portGET_CORE_ID());
    for (i = 0; i < TEST_TASK_LOOPS; i++) {
        int timeout_ticks = portGET_CORE_ID() ? 10 : 1;
        xSemaphoreGive(xSemB);
        while (xSemaphoreTake(xSemA, timeout_ticks) != pdTRUE) {
            // Attempt to take semaphore or yield if not available
            taskYIELD();
        }

        if ((i % TEST_TASK_SLEEPS) == 0) {
            int newcore = (portGET_CORE_ID() + 1) % configNUMBER_OF_CORES;
            xt_printf("Migrating from core %d -> %d\n", portGET_CORE_ID(), newcore);
            vTaskCoreAffinitySet(NULL, 1 << newcore);
            xt_printf("Migrated to core %d\n", portGET_CORE_ID());
            core_swaps++;
            *(volatile int *)pdata = core_swaps;
        }
    }

    xSemaphoreGive(xSemB);
    xt_printf("\nSem task complete\n");
    vTaskDelete(NULL);
}

// SMP Semaphore Test
// ------------------
// Runs two tasks and initializes 2 semaphores.  Task A (this function)
// will give semaphore A and take semaphore B, while Task B will take
// semaphore A and give semaphore B.  Task A will always run on core 0,
// while task B will be pinned to different cores and rescheduled
// to confirm functionality across the system.
//
static int run_sem_test(void)
{
    TaskHandle_t task;
    volatile int task_status = 0;
    int err, i = 0;

    xt_printf("\nSMP Sem test started on core %d\n", portGET_CORE_ID());

    if (portGET_CORE_ID() > 0) {
        xt_printf("err\n");
        test_exit(-1);
    }

    xSemA = xSemaphoreCreateBinary();
    xSemB = xSemaphoreCreateBinary();

    /* Create test task running on the same core as a control test */
    xt_printf("Creating sem test task on core 0\n");
    err = xTaskCreateAffinitySet(Sem_Task,
                                 "Sem_Task",
                                 INIT_TASK_STK_SIZE,
                                 (void *)&task_status,
                                 TASK_INIT_PRIO,
                                 1,     // core 0 only
                                 &task);
    if (err != pdPASS) {
        xt_printf(" FAILED to create Sem_Task (core 0)\n");
        return -1;
    }
    for (i = 0; i < TEST_TASK_LOOPS; i++) {
        xSemaphoreGive(xSemA);
        while (xSemaphoreTake(xSemB, 1) != pdTRUE) {
            // Attempt to take semaphore or yield if not available in 1 tick
            taskYIELD();
        }
    }

    if (task_status != 0) {
        xt_printf("SMP Sem test succeeded, swapped cores %d times\n", task_status);
        return 0;
    }
    xt_printf("SMP Sem test FAILED\n");
    return 1;
}


static void Core_Task(void *pdata)
{
    int status;

    status = run_task_test();
    status |= run_sem_test();

    /* Somewhat arbitrary check to confirm multi-core time is faster than single-core */
    if (status == 0) {
        xt_printf("SMP tests PASSED!\n");
    } else {
        xt_printf("SMP tests FAILED\n");
    }

    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    test_exit(0);

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
    test_exit(0);
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
    int core = portGET_CORE_ID();
    xt_printf("\nTest starting on core %d\n", core);

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

	/* Start task scheduler */
	xt_printf("Scheduler starting on core %d\n", core);
	vTaskStartScheduler();

done:
    exit_code = err;

    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    test_exit(exit_code);

    /* Does not reach here ('return' statement keeps compiler happy). */
    return 0;
}

