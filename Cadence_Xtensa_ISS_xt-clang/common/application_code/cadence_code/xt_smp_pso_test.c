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
#include    <xtensa/hal-core-state.h>

#include "testcommon.h"
#include "xt_smp_api.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"


#if ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 0 )
#error configUSE_CORE_AFFINITY required for this test in SMP mode
#endif


#if !(defined TARGET_XTSC)
/* XTSC is not able to power down nor reset cores.
 * Setting TARGET_XTSC == 1 enables this test to disable and
 * re-enable coherence only, without PSO / PWAIT / reset.
 * Requires rebuilding HAL with XTHAL_SMP_SELF_SHUTDOWN_TEST_IMM_RETURN
 * defined to remove WAITI from xthal_smp_self_shutdown() in smp_pso_self.S
 */
//#define TARGET_XTSC   1
#define TARGET_XTSC   0
#endif

#if TARGET_XTSC && !XCHAL_HAVE_WINDOWED
/* NOTE: On/off test only works with windowed configs. */
#error XTSC test mode does not work with Call0 configs
#endif


/* Create task as privileged if MPU enabled. */
#define TASK_INIT_PRIO          (20 | portPRIVILEGE_BIT)
#define INIT_TASK_STK_SIZE      ((XT_STACK_MIN_SIZE + 0x800) / sizeof(StackType_t))

#define TEST_TASK_COUNT         configNUMBER_OF_CORES
#define TEST_TASK_ITER          100
#define TEST_TASK_PSO_ITER      (configNUMBER_OF_CORES - 1)


/* Wrapper for xt_printf().  Required when running with GDBIO.
 *
 * After a power shut-off and reset, the core will come back up
 * with DCR.EnableOCD clear, meaning that file-IO syscalls will
 * trigger a debug exception and NOT a debug interrupt.  This 
 * in turn triggers an unhandled exception, which contains an
 * assertion with a print call, winding up in an infinite loop.
 *
 * To avoid this, suppress printf output on any powered-down cores.
 * Also, be sure prints are not preempted by the shutdown task.
 */
#if TARGET_XTSC
#define XT_PRINTF               xt_printf
#else
#define XT_PRINTF(...)          do {                            \
    vTaskSuspendAll();                                          \
    if (core_status[portGET_CORE_ID()].core_suspended == 0) {   \
        xt_printf(__VA_ARGS__);                                 \
    }                                                           \
    xTaskResumeAll();                                           \
} while (0)
#endif


/* Some basic task synchronization that relies on coherent shared memory */
volatile int task_done[configNUMBER_OF_CORES];

/* Global structures used for SMP tests */
static SemaphoreHandle_t xSemTest;
static SemaphoreHandle_t xSemPwrMgmt;

/* Global test data intentionally grouped together for more contention */
volatile uint32_t next_active_core;
volatile uint32_t running_cores;
volatile uint32_t running_cores_mask;
volatile uint32_t iteration;
volatile uint32_t pso_iteration;
volatile uint32_t increment;

/* Core state save area for shutdown/restore.  Only one core at a time... */
XthalCoreState core_state;

/* Task status is aligned/padded to different cache lines for cache verification */
typedef struct {
    uint32_t core_suspended;
    uint32_t core_data;
    uint32_t core_iter;
    uint8_t  padding[XCHAL_DCACHE_LINESIZE - 3 * sizeof(uint32_t)];
} core_status_t;

core_status_t core_status[configNUMBER_OF_CORES]
    __attribute__((aligned (XCHAL_DCACHE_LINESIZE)));


/* Initialize task control flags */
static void task_ctrl_init(void)
{
    int i;
    for (i = 0; i < configNUMBER_OF_CORES; i++) {
        task_done[i] = 0;
    }
}

/* Signal task "task_id" is done */
static void task_ctrl_signal_done(int task_id, int status) {
    task_done[task_id] = status;
}

/* Wait (yielding) until all tasks are done */
static int task_ctrl_wait_yield(void)
{
    int i;
    int core = portGET_CORE_ID();
    while (1) {
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
    for (i = 0; i < TEST_TASK_COUNT; i++) {
        if (task_done[i] < 0) {
            return -1;
        }
    }
    return 1;
}


/* Task to create contention on the same data across cores */
static void Test_Task(void *pdata)
{
    int my_core = portGET_CORE_ID();
    int my_iteration, my_suspended;
    int expected_data = 0;
    int status;

    XT_PRINTF("\nTask %d: Starting on core %d...\n", (int)pdata, my_core);
    while ((running_cores > 0) && (running_cores <= configNUMBER_OF_CORES)) {
        if (next_active_core == my_core) {
            xSemaphoreTake(xSemTest, portMAX_DELAY);
            do {
                next_active_core = (next_active_core + 1) % configNUMBER_OF_CORES;
            } while ((running_cores_mask & (1 << next_active_core)) == 0);
            if (next_active_core == 0) {
                increment++;
            }
            core_status[my_core].core_data += increment;
            core_status[my_core].core_iter++;
            my_iteration = ++iteration;
            xSemaphoreGive(xSemTest);
            XT_PRINTF("Task %d completed iteration %d\n", (int)pdata, my_iteration);
        }
    }

    expected_data = (iteration / configNUMBER_OF_CORES) + configNUMBER_OF_CORES / 2;
    my_iteration = core_status[my_core].core_iter;
    my_suspended = core_status[my_core].core_suspended;
    if (my_suspended >= 1) {
        /* Core was suspended and will have not run for a while */
        status = (my_iteration <= expected_data) ? 1 : -1;
    } else {
        status = (my_iteration >= expected_data) ? 1 : -1;
    }
    xSemaphoreTake(xSemTest, portMAX_DELAY);
    XT_PRINTF("Task %d (%s suspended): %d (# iter %d flip point %d)\n",
            (int)pdata,
            (my_suspended == 0) ? "not" : 
                (my_suspended == 1) ? "CURRENTLY" : 
                (my_suspended == 2) ? "PREVIOUSLY" : "???",
            status,
            my_iteration,
            expected_data);
    xSemaphoreGive(xSemTest);
    task_ctrl_signal_done((int)pdata, status);
    vTaskDelete(NULL);
}


/* Task is scheduled on a specific core and will cause it to 
 * leave the coherence protocol, then later then attempt to
 * rejoin it.
 *
 * If TARGET_XTSC == 1, no power shut-off / reset is performed,
 * and the core waits a few ticks before attempting to rejoin
 * the coherence protocol.
 *
 * If TARGET_XTSC == 0, the core enters the PWAIT state and is
 * powered off for some time by core 0, which later resets and
 * restores the core, allowing execution to resume where it
 * left off.
 *
 * IPIs into this core are disabled such that the scheduler 
 * will not run until it has rejoined the coherence protocol.
 */
static void Core_Coherence_Test_Stop_Start(void *pdata)
{
    int my_core = portGET_CORE_ID();
#if TARGET_XTSC
    register int start_wait_cycle, last_wait_cycle;
    register int prefctl;
#endif
    int *status = (int *)pdata;
    register int shutdown_status;

    XT_PRINTF("Core %d leaving cache coherence protocol\n", my_core);

    /* Take the test lock and update some state to keep the 
     * test running on other cores once this one leaves
     */
    xSemaphoreTake(xSemTest, portMAX_DELAY);
    xt_smp_scheduler_detach();
    running_cores--;
    running_cores_mask &= ~(1 << my_core);
    if (next_active_core == my_core) {
        do {
            next_active_core = (next_active_core + 1) % configNUMBER_OF_CORES;
        } while ((running_cores_mask & (1 << next_active_core)) == 0);
    }
    core_status[my_core].core_suspended = 1;
    xSemaphoreGive(xSemTest);

#if TARGET_XTSC
    /* assembly enforces that last_wait_cycle is in a register prior to 
     * disabling cache coherence, avoiding load between optout() and optin().
     */
    start_wait_cycle = xthal_get_ccount();
    last_wait_cycle = start_wait_cycle + 
        configNUMBER_OF_CORES * 2 * (configCPU_CLOCK_HZ / configTICK_RATE_HZ);
    __asm__ volatile ("memw; \n\t or %0, %0, %0" :: "r"(last_wait_cycle));
#endif

    /* This core now ignores FreeRTOS scheduler interrupts;
     * no preemptive context switches will occur until we reattach
     * to the scheduler.
     *
     * Proceed with gracefully leaving the cache coherence protocol,
     * which we protect by disabling interrupts.
     */
    portDISABLE_INTERRUPTS();

#if TARGET_XTSC
    /* xthal_smp_self_shutdown() will disable prefetch as part of disabling
     * coherency.  Normally, the official reenable path is called from the
     * reset vector; however, in this test mode, no reset is performed.
     * As a work-around to validate rejoining the coherency protocol,
     * save prefetch setting here and restore later, ensuring no load/store
     * operations are used (avoiding cache pollution).
     */
    prefctl = xthal_get_cache_prefetch();
#endif

#if XCHAL_HAVE_PSO_SMP && XCHAL_HAVE_XEA2
    /* xthal_smp_self_shutdown() will return here on success or failure.
     * On success, the core will have entered the PWAIT state and stopped
     * code execution until being reset and restored by core 0.
     */
    shutdown_status = xthal_smp_self_shutdown(&core_state);
#else
    shutdown_status = 0;
#endif

#if TARGET_XTSC
    /* We cannot access shared data at this point since cache coherence
     * is disabled.  Any contents may be stale and/or overwritten.
     */
    while (last_wait_cycle - (int)xthal_get_ccount() > 0) {
        /* busy-wait */
    }

    /* Re-enable caching (MPUENB), then cache coherence, then prefetch.
     * NOTE that this normally happens in reset handler...
     */
    if (xthal_cache_coherence_optin()) {
        XT_PRINTF("ERROR: xthal_cache_coherence_optin() failed\n");
        *status = -2;
        return;
    }
    xthal_set_cache_prefetch(prefctl);
#endif
    *status = (shutdown_status == 0) ? 1 : -1;

    /* Reattach to the scheduler, likely triggering an interrupt */
    xt_smp_scheduler_reattach();
    portENABLE_INTERRUPTS();

    /* Take the test lock and update some state to allow the
     * test to resume running on this core
     */
    xSemaphoreTake(xSemTest, portMAX_DELAY);
    running_cores++;
    running_cores_mask |= (1 << my_core);
    core_status[my_core].core_suspended = 2;
    xSemaphoreGive(xSemTest);

    XT_PRINTF("Core %d rejoined cache coherence protocol\n", my_core);

    vTaskDelete(NULL);
}


static void Test_Runner(void *pdata)
{
    int i, err, status;
#if !TARGET_XTSC
    int          pso_status;
    int32_t      pwait_status;
    uint32_t     start_wait_iter;
    int          my_core = portGET_CORE_ID();
#endif
    uint32_t     pso_core = configNUMBER_OF_CORES - 1;
    volatile int pso_task_status;
    TaskHandle_t tasks[configNUMBER_OF_CORES];
    TaskHandle_t coh_test_task;

    XT_PRINTF("SMP Power Management test runner started (core %d)\n", portGET_CORE_ID());

    if (portGET_CORE_ID() > 0) {
        XT_PRINTF("err\n");
        test_exit(-1);
    }

    xSemPwrMgmt = xSemaphoreCreateBinary();
    xSemaphoreGive(xSemPwrMgmt);
    xSemTest = xSemaphoreCreateBinary();
    xSemaphoreGive(xSemTest);

    next_active_core = 0;
    running_cores = configNUMBER_OF_CORES;
    running_cores_mask = (1 << configNUMBER_OF_CORES) - 1;
    iteration = 0;
    increment = 1;

    /* Create test tasks running on multiple cores */
    task_ctrl_init();
    for (i = 0; i < configNUMBER_OF_CORES; i++) {
        XT_PRINTF("Creating task %d on core %d\n", i, i);
        core_status[i].core_data = 0;
        core_status[i].core_iter = 0;
        err = xTaskCreateAffinitySet(Test_Task,
                                     "Test_Task",
                                     INIT_TASK_STK_SIZE,
                                     (void *)i,
                                     TASK_INIT_PRIO,
                                     1 << i, // 1 task per core
                                     &(tasks[i]));
        if (err != pdPASS) {
            XT_PRINTF(" FAILED to create Test_Task (all cores)\n");
            test_exit(-1);
            return;
        }
    }

    /* Let the test run for a while before initiating a coherence/PSO test */
    while (iteration < (TEST_TASK_ITER * TEST_TASK_PSO_ITER)) {
        while (iteration < (TEST_TASK_ITER * (pso_iteration + 1))) {
            taskYIELD();
        }

        if (pso_iteration == TEST_TASK_PSO_ITER) {
            break;
        }

        /* Power-down of one core at a time; control access to L2CC regs */
        xSemaphoreTake(xSemPwrMgmt, portMAX_DELAY);
        pso_task_status = 0;

        /* Schedule a PSO on core N-1 */
        err = xTaskCreateAffinitySet(
                                     Core_Coherence_Test_Stop_Start,
                                     "Coherence_Test_Task",
                                     INIT_TASK_STK_SIZE,
                                     (void *)&pso_task_status,
                                     TASK_INIT_PRIO + 1,
                                     1 << pso_core,
                                     &coh_test_task);
        if (err != pdPASS) {
            XT_PRINTF(" FAILED to create Coherence_Test_Task on core %d\n",
                    (configNUMBER_OF_CORES - 1));
            test_exit(-2);
            return;
        }

#if TARGET_XTSC
        /* Check whether the coherence stop/start task is running */
        while (pso_task_status == 0) {
        }
#else // TARGET_XTSC
        /* Only release pwrmgmt mutex once core is in PWAIT state */
        do {
            pwait_status = (xthal_smp_core_shutdown_status(pso_core) & XTSUB_PSO_STAT_PWAIT);
        } while ((pso_task_status == 0) && (pwait_status == 0));
        if ((pso_task_status < 0) || (pwait_status < 0)) {
            XT_PRINTF(" FAILED waiting for PSO status: pwait %d pso %d\n",
                    pwait_status, pso_task_status);
            test_exit(-3);
            return;
        }
#endif

        xSemaphoreGive(xSemPwrMgmt);

#if !TARGET_XTSC
        /* Request shutdown */
        XT_PRINTF("Core %d is in PWAIT; shutdown requested\n", pso_core);
        status = xthal_smp_core_shutdown(pso_core, 1);
        if (status) {
            XT_PRINTF(" xthal_smp_core_shutdown FAILED: %d\n", status);
            test_exit(-4);
        }

        /* Wait for confirmation of core power down */
        do {
            pso_status = xthal_smp_core_shutdown_status(pso_core);
        } while ((pso_status & XTSUB_PSO_STAT_POWERED_DOWN) == 0);
        XT_PRINTF("Core %d powered down (0x%x)\n", pso_core, pso_status);

        /* Wait for a while before reviving the core */
        start_wait_iter = core_status[my_core].core_iter;
        while (core_status[my_core].core_iter - start_wait_iter < 10) {
            taskYIELD();
        }

        XT_PRINTF("Core %d is powering up...\n", pso_core);
        xthal_smp_core_restore(pso_core);
#endif // TARGET_XTSC

        /* Wait for confirmation of PSO task completion */
        while (pso_task_status == 0) {
        }

#if !TARGET_XTSC
        pso_status = xthal_smp_core_shutdown_status(pso_core);
        XT_PRINTF("Core %d power restored (0x%x)\n", pso_core, pso_status);
#endif // TARGET_XTSC

        XT_PRINTF("Core %d PSO task complete\n", pso_core);
        pso_iteration++;

        /* Rotate core to power down, but avoid core 0 */ 
        pso_core--;
        if (pso_core == 0) {
            pso_core = configNUMBER_OF_CORES - 1;
        }
    }

#if !TARGET_XTSC
    /* Wait for a while before ending the test */
    start_wait_iter = core_status[my_core].core_iter;
    while (core_status[my_core].core_iter - start_wait_iter < 10) {
        taskYIELD();
    }
#endif

    running_cores = configNUMBER_OF_CORES + 1;
    status = task_ctrl_wait_yield();

    /* Shut down simulator and report error code as exit code to host (0 = OK). */
#if TARGET_XTSC
    XT_PRINTF("\nSMP Coherence test %s\n", ((status > 0) && (pso_task_status > 0)) ? "PASSED" : "FAILED");
#else
    XT_PRINTF("\nSMP Coherence test with PSO %s\n", (status > 0) ? "PASSED" : "FAILED");
#endif
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
    XT_PRINTF("\nTest starting on core %d\n", core);

    /* Create the control task initially with the high priority. */
    err = xTaskCreate(Test_Runner,
                      "Test_Runner",
                      INIT_TASK_STK_SIZE,
                      NULL,
                      TASK_INIT_PRIO,
                      &handle);
    if (err != pdPASS)
    {
        XT_PRINTF(" FAILED to create Test_Runner\n");
        goto done;
    }
    vTaskCoreAffinitySet(handle, 1);    // Core 0 only

    /* Start task scheduler */
    XT_PRINTF("Scheduler starting on core %d\n", core);
    vTaskStartScheduler();

done:
    exit_code = err;

    /* Shut down simulator and report error code as exit code to host (0 = OK). */
    test_exit(exit_code);

    /* Does not reach here ('return' statement keeps compiler happy). */
    return 0;
}

