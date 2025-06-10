
// Copyright (c) 2018-2025 Cadence Design Systems, Inc.
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

// FreeRTOS version of XTOS single-image multicore example mc_demo.c 
// Demonstrates multiple cores working on shared data in parallel,
// relying on hardware coherency to keep shared memory in sync.


#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include <xtensa/config/system.h>
#include <xtensa/xtruntime.h>
#include <xtensa/xtsubsystem.h>
#include <xtensa/hal.h>

#include "testcommon.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"


#ifndef SMP_TEST
#error  FreeRTOS SMP support required for mc_demo
#endif
#if (configUSE_CORE_AFFINITY == 0)
#error configUSE_CORE_AFFINITY required for this test in SMP mode
#endif

#if XCHAL_HAVE_PRID && XCHAL_HAVE_EXCLUSIVE && (XSHAL_RAM_SIZE > 0) && \
    XCHAL_DCACHE_IS_COHERENT && (XCHAL_L2CC_NUM_CORES > 1)

// Set USE_MUTEX to zero to disable use of the FreeRTOS mutex.
// The difference will be seen in the stdout output.
#ifndef USE_MUTEX
#define USE_MUTEX           1
#endif

#if USE_MUTEX
#define MLOCK()             xSemaphoreTake(mtx0, portMAX_DELAY)
#define MUNLOCK()           xSemaphoreGive(mtx0)
#else
#define MLOCK()
#define MUNLOCK()
#endif

// Print macro for convenience.
#define PRINT(...)    { MLOCK(); printf("core%d: ", portGET_CORE_ID()); printf(__VA_ARGS__); MUNLOCK(); }
#define PRINT_UNLOCKED(...)    { printf("core%d: ", portGET_CORE_ID()); printf(__VA_ARGS__); }

// Task parameters
#define TASK_PRIO           (20)
#if (defined XT_CFLAGS_O0)
#define TASK_STK_SIZE       ((XT_STACK_MIN_SIZE + 0x1800) / sizeof(StackType_t))
#else
#define TASK_STK_SIZE       ((XT_STACK_MIN_SIZE + 0x800) / sizeof(StackType_t))
#endif


#define NUM_CORES           (configNUMBER_OF_CORES)

// Global objects shared by all cores. Note bar0 is initialized
// statically so we don't have to call init on it before use.
xtos_barrier bar0 = NUM_CORES;
xtos_barrier bar1;
xtos_barrier bar2;
#if USE_MUTEX
SemaphoreHandle_t mtx0;
#endif

// Try to keep the row size a multiple of the L1 cache line size.
#ifndef ROW_SIZE
#define ROW_SIZE    ((NUM_CORES * 32) > 128 ? 128 : (NUM_CORES * 32))
#endif
#define COL_SIZE    ROW_SIZE

// Place the 'size' field at the end to ensure the matrix rows
// are cache line aligned as far as possible. If hardware FP
// support is available, use FP data.
typedef struct matrix {
#if XCHAL_HAVE_FP
    float    elements[ROW_SIZE][COL_SIZE];
#else
    uint32_t elements[ROW_SIZE][COL_SIZE];
#endif
    uint32_t size;
} matrix __attribute__ ((aligned(XCHAL_DCACHE_LINESIZE)));

// Global data structures shared by all cores.
matrix in1;
matrix in2;
matrix out1;
matrix out2;

//---------------------------------------------------------------------
// Fill the input matrix with some data pattern.
//---------------------------------------------------------------------
static void
generate_matrix(matrix * pmat, uint32_t size)
{
    uint32_t i, j;

    pmat->size = size;

    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            pmat->elements[i][j] = (i<<16) | j;
        }
    }
}

//---------------------------------------------------------------------
// Check that the two matrices are identical in every element.
//---------------------------------------------------------------------
static bool
check_matrix(const matrix * m1, const matrix * m2, uint32_t prid)
{
    uint32_t i, j;
    uint32_t err = 0;

    for (i = 0; i < m1->size; i++) {
        for (j = 0; j < m1->size; j++) {
            if (m1->elements[i][j] != m2->elements[i][j]) {
#if XCHAL_HAVE_FP
                PRINT("mismatch at %u,%u, %f <-> %f\n", i, j,
#else
                PRINT("mismatch at %u,%u, %u <-> %u\n", i, j,
#endif
                      m1->elements[i][j], m2->elements[i][j]);
                err++;
            }
        }
    }

    if (err) {
        PRINT("%u errors\n", err);
        return false;
    }

    return true;
}

//---------------------------------------------------------------------
// Multiply two input matrices into the output matrix, either the full
// matrix or a slice of it as specified by prid and nrows. Matrix size
// must have been set beforehand. Output matrix must have been zeroed.
//---------------------------------------------------------------------
__attribute__ ((noinline))
static void
mul_matrix(const matrix * m, const matrix * n, matrix * r,
           uint32_t prid, uint32_t nrows)
{
    uint32_t i, j, k;

    uint32_t rstart = prid * nrows;
    uint32_t rend   = rstart + nrows;

    for (i = rstart; i < rend; ++i) {
        for (j = 0; j < n->size; ++j) {
            for (k = 0; k < n->size; ++k) {
                r->elements[i][k] +=
                    m->elements[i][j] * n->elements[j][k];
            }
        }
    }
}


//---------------------------------------------------------------------
// Task code that manages the matrix test itself.  The same task is
// instantiated multiple times, with each instance pinned to run on
// a different core.
//---------------------------------------------------------------------
static void
matrix_task(void *pdata)
{
    bool     ok;
    int32_t  ret;
    uint32_t c1, c2;
    uint32_t c3, c4;
    uint32_t prid = portGET_CORE_ID();

    if (prid == 0) {
        PRINT("core 0 starting...\n");
        // Generate the input matrices.
        PRINT("Test matrix size is %u x %u\n", ROW_SIZE, COL_SIZE);
        generate_matrix(&in1, ROW_SIZE);
        generate_matrix(&in2, ROW_SIZE);
        out1.size = ROW_SIZE;
        out2.size = ROW_SIZE;

        // Invalidate entire dcache. We want to measure cycles
        // with both L1 and L2 cache empty.
        xthal_dcache_all_writeback_inv();

        // Generate the single-core result.
        PRINT("Running mul_matrix() on core 0\n");
        c3 = xthal_get_cycle_count();
        mul_matrix(&in1, &in2, &out1, 0, ROW_SIZE);
        c4 = xthal_get_cycle_count();
        PRINT("mul_matrix() complete\n");
    }

    // Ensure all cores measure cycles with L1 and L2 caches empty.
    xthal_dcache_all_writeback_inv();

    // Wait for everyone to arrive at the barrier.
    ret = xtos_barrier_wait(&bar0);
    if (ret != 0) {
        fprintf(stderr, "Barrier wait error\n");
    }

    // The shared computation is done outside of an exclusion region,
    // so that all cores run in parallel.
    PRINT("Running mul_matrix() on core %d\n", prid);
    c1 = xthal_get_cycle_count();
    mul_matrix(&in1, &in2, &out2, prid, ROW_SIZE/NUM_CORES);
    c2 = xthal_get_cycle_count();
    PRINT("mul_matrix() complete\n");

    // Sync here to make sure that all cores have finished their
    // work before checking results.
    xtos_barrier_wait(&bar1);

    PRINT("Running check_matrix() on core %d\n", prid);
    ok = check_matrix(&out1, &out2, prid);

    if (prid == 0) {
        MLOCK();
        PRINT_UNLOCKED("single-core time : %u cycles\n", c4 - c3);
        PRINT_UNLOCKED("multi-core time  : %u cycles\n", c2 - c1);
        MUNLOCK();
    }
    else {
        PRINT("multi-core time  : %u cycles\n", c2 - c1);
    }

    // Sync again at the exit barrier. This prevents one core from
    // terminating the simulation and closing global file handles
    // before the others are done using them.
    xtos_barrier_sync(&bar2);
    if (prid == 0) {
        PRINT(ok ? "PASS\n" : "FAIL\n");
    }
    xtos_barrier_wait(&bar2);

    test_exit(0);
}


/* Hook functions for standalone tests */
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


//---------------------------------------------------------------------
// main
//---------------------------------------------------------------------
int
main()
{
    // The sim-mc LSP will set up all of sysram to be cached shared.
    // Nothing required here unless shared memory is to be arranged
    // in a different way.
    int i;

#if USE_MUTEX
    // Init the mutex before calling PRINT()
    mtx0 = xSemaphoreCreateMutex();
    if (mtx0 == NULL) {
        PRINT("Warning: mutex creation failed\n");
    }
#endif
    PRINT("core 0 starting...\n");

    // Init the barriers we will use. Note bar0
    // does not need init.
    xtos_barrier_init(&bar1, NUM_CORES);
    xtos_barrier_init(&bar2, NUM_CORES);

    for (i = 0; i < NUM_CORES; i++) {
        TaskHandle_t handle;
        int err = xTaskCreate(matrix_task,
                              "matrix task",
                              TASK_STK_SIZE,
                              NULL,
                              TASK_PRIO,
                              &handle);
        if (err != pdPASS) {
            PRINT("FAILED to create matrix task %d\n", i);
            return -1;
        }
        vTaskCoreAffinitySet(handle, 1 << i);   // pin to core i
    }

    // Start scheduler
    vTaskStartScheduler();

    // Execution will not return here
    return 0;
}

#else

#error "The core configuration is not compatible with this example.".

#endif // XCHAL_HAVE_PRID && XCHAL_HAVE_EXCLUSIVE && (XSHAL_RAM_SIZE > 0) etc.

