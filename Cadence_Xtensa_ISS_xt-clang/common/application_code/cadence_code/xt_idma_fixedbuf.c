/*
 * Copyright (c) 2026 by Cadence Design Systems. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* FreeRTOS example using idma fixed buffer API. Creates 3 threads which schedule
   their own sets of DMA transfers using per-thread fixed buffers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xtensa/hal.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "testcommon.h"

#define  IDMA_USE_MULTICHANNEL

#include "idma_tests.h"


#ifndef CH
#define CH              0    // idma channel number
#endif

/* Stack size for tasks that use the C library and/or the coprocessors */
#define STACK_SIZE      ((XT_STACK_MIN_SIZE + 0x1000) / sizeof(StackType_t))

#define NUM_ITER        500

#define SIZE_1D         1024
#define SIZE_2D         8192

#ifndef NO_INTS
#define NO_INTS         0    // 0 -> use completion ints, 1 -> don't use ints
#endif


static inline void
halt(void)
{
    exit(-1);
}

//---------------------------------------------------------------------
// test1
//---------------------------------------------------------------------

TaskHandle_t * ptcb1;
SemaphoreHandle_t tcb1_sem;

// Circular buffer of descriptors.
IDMA_BUFFER_DEFINE(buffer1, 4, IDMA_1D_DESC);

// Source buffer, one transfer size.
char test1_srcbuf[SIZE_1D] IDMA_DRAM;

// Destination buffer.
ALIGNDCACHE char test1_dstbuf[SIZE_1D * 4];

// test1 - schedules 4 descriptors at a time, depends upon interrupt
// driven wakeup if interrupt use is enabled, else depends upon some
// other thread driving the polling activity.
void
test1(void * arg)
{
    int32_t  ndx;
    int32_t  ret;
    int32_t  val = 0xdeadbeef;
#if NO_INTS
    uint32_t desc_opts = 0;
#else
    uint32_t desc_opts = DESC_NOTIFY_W_INT | DESC_IDMA_PRIOR_H;
#endif

    // Init descriptor buffer for this thread.
    idma_init_loop(CH, buffer1, IDMA_1D_DESC, 4, NULL, NULL);

    // Generate source pattern.
    for (ret = 0; ret < SIZE_1D/4; ret++) {
        ((int32_t *) test1_srcbuf)[ret] = val;
    }

    // Clear the destination buffer to prep for next transfer.
    memset(test1_dstbuf, 0, SIZE_1D * 4);
    xthal_dcache_region_writeback_inv(test1_dstbuf, SIZE_1D * 4);

    // Schedule transfers.
    idma_add_desc(buffer1, test1_dstbuf, test1_srcbuf, SIZE_1D,  desc_opts );
    idma_add_desc(buffer1, test1_dstbuf + (1 * SIZE_1D), test1_srcbuf, SIZE_1D,  desc_opts );
    idma_add_desc(buffer1, test1_dstbuf + (2 * SIZE_1D), test1_srcbuf, SIZE_1D,  desc_opts );
    idma_add_desc(buffer1, test1_dstbuf + (3 * SIZE_1D), test1_srcbuf, SIZE_1D,  desc_opts );

    ndx = idma_schedule_desc(CH, 4);

    while ((ret = idma_desc_done(CH, ndx)) == 0) {
        idma_sleep(CH);
    }
    if (ret < 0) {
        printf("Error waiting for test1\n");
        halt();
    }

    // Check buffer status.
    if ((ret = idma_buffer_status(CH)) < 0) {
        printf("Error test1 buffer status %d\n", ret);
        halt();
    }

    // Check the transfer results.
    for (ret = 0; ret < 4; ret++) {
        if (memcmp(test1_srcbuf, test1_dstbuf + (ret*SIZE_1D), SIZE_1D) != 0) {
            printf("Error test1 block %d\n", ret);
            halt();
        }
    }

#if (XCHAL_SW_VERSION >= 1504000)
    // Before deleting the task, we must release its iDMA buffers so that
    // idma-os resource tracking is updated.  A subsequent instance of 
    // this task could be created with the same task handle but on a 
    // different core of an SMP system, triggering an assertion.
    // NOTE: idma_release_loop_buffer() API introduced in RJ.4
    ret = idma_release_loop_buffer(CH, buffer1);
    if (ret != IDMA_OK) {
        printf("Error test1 releasing loop buffer %d\n", ret);
        halt();
    }
#endif

    puts("test1 OK");
    xSemaphoreGive(tcb1_sem);
    vTaskSuspend(NULL);
    assert(0);
}

//---------------------------------------------------------------------
// test2
//---------------------------------------------------------------------

TaskHandle_t tcb2;
SemaphoreHandle_t tcb2_sem;

// Circular buffer of descriptors.
IDMA_BUFFER_DEFINE(buffer2, 4, IDMA_1D_DESC);

// Source buffer, one transfer size.
char test2_srcbuf[SIZE_1D] IDMA_DRAM;

// Destination buffer.
ALIGNDCACHE char test2_dstbuf[SIZE_1D * 4];

// test2 - schedules 4 descriptors at a time, sleeps until completion.
void
test2(void * arg)
{
    int32_t  i;
    int32_t  ret;
    int32_t  val       = 5;
#if NO_INTS
    uint32_t desc_opts = 0;
#else
    uint32_t desc_opts = DESC_NOTIFY_W_INT | DESC_IDMA_PRIOR_L;
#endif

    // Init descriptor buffer for this thread.
    idma_init_loop(CH, buffer2, IDMA_1D_DESC, 4, NULL, NULL);

    for (i = 0; i < (int32_t)arg; i++) {
        // Sleep to let other threads run and schedule transfers.
        vTaskDelay(2);

        // Generate source pattern.
        memset(test2_srcbuf, val, SIZE_1D);

        // Schedule transfers.
        idma_add_desc(buffer2, test2_dstbuf, test2_srcbuf, SIZE_1D, 0);
        idma_add_desc(buffer2, test2_dstbuf + (1 * SIZE_1D), test2_srcbuf, SIZE_1D, 0);
        idma_add_desc(buffer2, test2_dstbuf + (2 * SIZE_1D), test2_srcbuf, SIZE_1D, 0);
        idma_add_desc(buffer2, test2_dstbuf + (3 * SIZE_1D), test2_srcbuf, SIZE_1D, desc_opts);
        idma_schedule_desc(CH, 4);

        // Wait for completion.
#if !NO_INTS
        idma_sleep(CH);
#endif
        while ((ret = idma_buffer_status(CH)) > 0) {
            idma_sleep(CH);
        }
        if (ret < 0) {
            printf("Error waiting for test2\n");
            halt();
        }

        // Check the transfer results.
        xthal_dcache_region_invalidate(test2_dstbuf, 4 * SIZE_1D);
        for (ret = 0; ret < 4; ret++) {
            if (memcmp(test2_srcbuf, test2_dstbuf + (ret*SIZE_1D), SIZE_1D) != 0) {
                printf("Error test2 block %d\n", ret);
                halt();
            }
        }
        printf("test2 OK (%d)\n", i);
        val += 5;
    }

#if (XCHAL_SW_VERSION >= 1504000)
    // Before deleting the task, we must release its iDMA buffers so that
    // idma-os resource tracking is updated.  A subsequent instance of 
    // this task could be created with the same task handle but on a 
    // different core of an SMP system, triggering an assertion.
    // NOTE: idma_release_loop_buffer() API introduced in RJ.4
    ret = idma_release_loop_buffer(CH, buffer2);
    if (ret != IDMA_OK) {
        printf("Error test2 releasing loop buffer %d\n", ret);
        halt();
    }
#endif

    xSemaphoreGive(tcb2_sem);
    vTaskDelete(NULL);
    assert(0);
}


//-------------------------------------------------------------------------
// Run 2D transfers. Expects malloc() to allocate in system memory.
//-------------------------------------------------------------------------

#define SIZE_2D                 8192

// TCB and task join semaphore
TaskHandle_t tcb3;
SemaphoreHandle_t tcb3_sem;

// Descriptor buffer.
IDMA_BUFFER_DEFINE(buf2d, 2, IDMA_2D_DESC);

// Source buffer for 2D transfers (in dataram).
char srcbuf_2d[32] IDMA_DRAM;

// Destination buffer.
char * dstbuf_2d;

// test2d - schedules one 2D desc at a time. Does not use interrupts.
void
test_2d(void * arg)
{
    int32_t ret;
    volatile int32_t i;
    char *  p;

    // Allocate and align dest buffer.
    p = malloc(SIZE_2D + IDMA_DCACHE_ALIGN);
    dstbuf_2d = (char *)(((int32_t)p + IDMA_DCACHE_ALIGN - 1) & -IDMA_DCACHE_ALIGN);

    // Init descriptor buffer for this thread's 2D transfers.
    ret = idma_init_loop(CH, buf2d, IDMA_2D_DESC, 2, NULL, NULL);

    for (i = 0; i < (int32_t)arg/2; i++) {
        // Clear the destination buffer to prep for next transfer.
        memset(dstbuf_2d, 0, SIZE_2D);
        xthal_dcache_region_writeback_inv(dstbuf_2d, SIZE_2D);

        // Generate source pattern.
        memset(srcbuf_2d, i, 32);

        // Add and schedule the transfer.
        idma_add_2d_desc(buf2d, dstbuf_2d, srcbuf_2d, 32, 0, SIZE_2D/32, 0, 32);
        idma_schedule_desc(CH, 1);

        // Wait for completion. Sleep if no ints, else poll.
        while ((ret = idma_buffer_status(CH)) > 0) {
#if NO_INTS
            idma_sleep(CH);
#endif
        }
        if (ret < 0) {
            printf("Error waiting for 2D transfer\n");
            halt();
        }

        // Check result.
        for (ret = 0; ret < SIZE_2D/32; ret++) {
            if (memcmp(srcbuf_2d, dstbuf_2d + (ret*32), 32) != 0) {
                printf("Data mismatch in 2D transfer, block %d\n", ret);
                halt();
            }
        }

        printf("2D transfer OK (%d)\n", i);
    }

    free(p);
#if (XCHAL_SW_VERSION >= 1504000)
    // Before deleting the task, we must release its iDMA buffers so that
    // idma-os resource tracking is updated.  A subsequent instance of 
    // this task could be created with the same task handle but on a 
    // different core of an SMP system, triggering an assertion.
    // NOTE: idma_release_loop_buffer() API introduced in RJ.4
    ret = idma_release_loop_buffer(CH, buf2d);
    if (ret != IDMA_OK) {
        printf("Error 2D test releasing loop buffer %d\n", ret);
        halt();
    }
#endif

    xSemaphoreGive(tcb3_sem);
    vTaskDelete(NULL);
    assert(0);
}


#if NO_INTS
//-------------------------------------------------------------------------
// Lowest priority thread. Keeps the idma processing going while everyone
// else is asleep.
//-------------------------------------------------------------------------

// TCB and thread stack.
TaskHandle_t tcb4;

void
idma_kick(void * arg)
{
    while (1) {
        // Disable preemption around the poll to prevent context switching
        // inside the buffer call.
        vTaskSuspendAll();
        idma_buffer_status(CH);
        xTaskResumeAll();
    }

    vTaskDelete(NULL);
    assert(0);
}
#endif


void
initTask(void *arg)
{
    int32_t ret = 0;
    int32_t count = (int32_t)arg;
#if ( configNUMBER_OF_CORES > 1 )
    int32_t core;
#endif
    void *  last;

#if !NO_INTS && XCHAL_HAVE_XEA2 && (XCHAL_INT_LEVEL(XCHAL_IDMA_CH0_DONE_INTERRUPT) > XCHAL_EXCM_LEVEL)
    printf("FreeRTOS does not support idma interrupts at > EXCM_LEVEL, skip\n");
    printf("PASS\n");
    return;
#else
    printf("FreeRTOS idma test running...\n");
#endif

    // Disable prefetch. Else the prefetcher will bring in stale data from idma
    // buffers between the invalidate and the read, causing test failures.
    xthal_set_cache_prefetch_long(0);

    // TODO: add SMP support

    tcb2_sem = xSemaphoreCreateCounting( 1, 0 );
#if ( configNUMBER_OF_CORES > 1 )
    core = 1;
    ret = xTaskCreateAffinitySet(test2,
                                 "test2",
                                 STACK_SIZE,
                                 (void *)count,
                                 7,
                                 1 << core,
                                 &tcb2);
#else
    ret = xTaskCreate(test2,
                      "test2",
                      STACK_SIZE,
                      (void *)count,
                      7,
                      &tcb2);
#endif
    if (ret != pdPASS) {
        printf("Error creating thread 2 : %d\n", ret);
        halt();
    }

    tcb3_sem = xSemaphoreCreateCounting( 1, 0 );
#if ( configNUMBER_OF_CORES > 1 )
    core = 2 % configNUMBER_OF_CORES;
    ret = xTaskCreateAffinitySet(test_2d,
                                 "test_2d",
                                 STACK_SIZE,
                                 (void *)count,
                                 6,
                                 1 << core,
                                 &tcb3);
#else
    ret = xTaskCreate(test_2d,
                      "test_2d",
                      STACK_SIZE,
                      (void *)count,
                      6,
                      &tcb3);
#endif
    if (ret != pdPASS) {
        printf("Error creating thread 3 : %d\n", ret);
        halt();
    }

#if NO_INTS
#if ( configNUMBER_OF_CORES > 1 )
    core = 0;
    ret = xTaskCreateAffinitySet(idma_kick,
                                 "idma_kick",
                                 STACK_SIZE,
                                 NULL,
                                 1,
                                 1 << core,
                                 &tcb4);
#else
    ret = xTaskCreate(idma_kick,
                      "idma_kick",
                      STACK_SIZE,
                      NULL,
                      1,
                      &tcb4);
#endif
    if (ret != pdPASS) {
        printf("Error creating thread kick : %d\n", ret);
        halt();
    }
#endif

    idma_init(CH, IDMA_OCD_HALT_ON, MAX_BLOCK_2, 16, TICK_CYCLES_8, 100000, idmaErrCB);
    idma_log_handler(idmaLogHander);

    // Restart this task over and over to exercise the buffer
    // init / release process. Dynamically allocating the TCB
    // ensures a changing thread (TCB) pointer value, which
    // should exercise the FreeRTOS buffer set/clear functions a
    // bit more.

    last = NULL;

    tcb1_sem = xSemaphoreCreateCounting( 1, 0 );
    while (--count) {
        ptcb1 = malloc(sizeof(TaskHandle_t));
        if (ptcb1 == NULL) {
            printf("Error creating thread 1 : no memory\n");
            halt();
        }

        // Malloc before freeing the previous pointer to ensure
        // that the value of ptcb1 changes every time.
        free(last);
        last = ptcb1;

#if ( configNUMBER_OF_CORES > 1 )
        core = portGET_CORE_ID();
        ret = xTaskCreateAffinitySet(test1,
                                     "test1",
                                     STACK_SIZE,
                                     (void *)0,
                                     6,
                                     1 << core,
                                     ptcb1);
#else
        ret = xTaskCreate(test1,
                          "test1",
                          STACK_SIZE,
                          (void *)0,
                          6,
                          ptcb1);
#endif
        if (ret != pdPASS) {
            printf("Error creating thread 1 : %d\n", ret);
            halt();
        }

        xSemaphoreTake(tcb1_sem, portMAX_DELAY);
        vTaskDelete(*ptcb1);
    }

    xSemaphoreTake(tcb2_sem, portMAX_DELAY);
    xSemaphoreTake(tcb3_sem, portMAX_DELAY);

    printf("PASS\n");
    exit(0);
}


#if ( configSUPPORT_STATIC_ALLOCATION )

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__((aligned(XCHAL_MPU_ALIGN)));

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ] __attribute__((aligned(XCHAL_MPU_ALIGN)));

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

#endif  // configSUPPORT_STATIC_ALLOCATION


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
    test_exit(0);
}

int main(int argc, char *argv[])
#else
int main_xt_idma(int argc, char *argv[])
#endif
{
    int32_t count;
    if (argc > 1) {
        count = atoi(argv[1]);
    }
    else {
        count = NUM_ITER;
    }

#if ( configNUMBER_OF_CORES > 1 )
    // Start initTask on core 0
    xTaskCreateAffinitySet(initTask,
                           "initTask",
                           STACK_SIZE,
                           (void *)count,
                           0,
                           1 << 0,
                           NULL );
#else
    xTaskCreate( initTask, "initTask", STACK_SIZE, (void *)count, 8, NULL );
#endif // ( configNUMBER_OF_CORES > 1 )

    vTaskStartScheduler();

    /* Will only reach here if there is insufficient heap available to start
       the scheduler. */
    for( ;; );
    return 0;
}
