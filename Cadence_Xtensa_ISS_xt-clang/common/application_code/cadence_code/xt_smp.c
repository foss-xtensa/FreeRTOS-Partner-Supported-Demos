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
uint32_t task_done[configNUMBER_OF_CORES];

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
        taskYIELD();
    }
}


/* Output a simple string to the console to avoid printf dependency. */
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
**  Public Domain ltoa()
**
**  Converts a long integer to a string.
**
**  Copyright 1988-90 by Robert B. Stout dba MicroFirm
**
**  Released to public domain, 1991
**
**  Parameters: 1 - number to be converted
**              2 - buffer in which to build the converted string
**              3 - number base to use for conversion
**
**  Returns:  A character pointer to the converted string if
**            successful, a NULL pointer if the number base specified
**            is out of range.
*/

#include <stdlib.h>
#include <string.h>

#define BUFSIZE (sizeof(long) * 8 + 1)

char *ltoa(long N, char *str, int base)
{
      register int i = 2;
      long uarg;
      char *tail, *head = str, buf[BUFSIZE];

      if (36 < base || 2 > base)
            base = 10;                    /* can only use 0-9, A-Z        */
      tail = &buf[BUFSIZE - 1];           /* last character position      */
      *tail-- = '\0';

      if (10 == base && N < 0L)
      {
            *head++ = '-';
            uarg    = -N;
      }
      else  uarg = N;

      if (uarg)
      {
            for (i = 1; uarg; ++i)
            {
                  register ldiv_t r;

                  r       = ldiv(uarg, base);
                  *tail-- = (char)(r.rem + ((9L < r.rem) ?
                                  ('A' - 10L) : '0'));
                  uarg    = r.quot;
            }
      }
      else  *tail-- = '0';

      memcpy(head, ++tail, i);
      return str;
}

/* Output a long to the console to avoid printf dependency. */
static void putlong(const long l)
{
    char c[64];
    char *p = ltoa(l, c, 10);
    putstr(p ? p : "");
}


/* Task to create some noticeable processor activity */
static void Test_Task(void *pdata)
{
    int i;
    float x = 1.0;
    double y = 1.0;
    char taskstr[64] = "\nTask N: Starting...\n";
    char statstr[4] = ".";
    char *p;

    p = strchr(taskstr, 'N');
    *p = '0' + (int)pdata;
    putstr(taskstr);

    for (i = 0; i < TEST_TASK_LOOPS; i++) {
        statstr[0] = '0' + (int)pdata;
        putstr(statstr);
        x += sqrt(3.141592654 * (3.0 + (float)i));
        y += sqrt((double)3.141592654 * (double)(3.0 + (double)i));
        if ((i % TEST_TASK_SLEEPS) == 0) {
            statstr[0] = 's' + (int)pdata;
            putstr(statstr);
            taskYIELD();
        }
    }

    p = strchr(taskstr, 'S');
    *p = (x + y == 0.0) ? '0' : '1';
    *(p+1) = '\0';
    putstr(taskstr);
    task_ctrl_signal_done((int)pdata);
    vTaskDelete(NULL);
}


const char *basestr = "Hello from Core X\n";

static void Core_Task(void *pdata)
{
    TaskHandle_t tasks[configNUMBER_OF_CORES];
    char corestr[64];
    int core, err, i;
    long start_ticks, total_ticks_1core, total_ticks_allcores;
    char *p;

    UNUSED(pdata);

    /* Display some core-specific output */
    core = xthal_get_coreid();
    strcpy(corestr, basestr);
    p = strchr(corestr, 'X');
    *p = '0' + core;
    putstr(corestr);

    /* Create test tasks running on the same core as a control test */
    task_ctrl_init();
    start_ticks = xTaskGetTickCount();
    for (i = 0; i < TEST_TASK_COUNT; i++) {
        err = xTaskCreateAffinitySet(Test_Task, 
                                     "Test_Task", 
                                     INIT_TASK_STK_SIZE, 
                                     (void *)i, 
                                     TASK_INIT_PRIO, 
                                     1,     // core 0 only
                                     &(tasks[i]));
        if (err != pdPASS)
        {
            putstr(" FAILED to create Test_Task (core 0)\n");
            goto done;
        }
    }
    task_ctrl_wait_yield();
    total_ticks_1core = xTaskGetTickCount() - start_ticks;
    putstr("\nsingle-core ticks: ");
    putlong(total_ticks_1core);
    putstr("\n");


    /* Create test tasks running on multiple cores */
    task_ctrl_init();
    start_ticks = xTaskGetTickCount();
    for (i = 0; i < TEST_TASK_COUNT; i++) {
        err = xTaskCreateAffinitySet(Test_Task, 
                                     "Test_Task", 
                                     INIT_TASK_STK_SIZE, 
                                     (void *)i, 
                                     TASK_INIT_PRIO, 
                                     1 << i, // 1 task per core
                                     &(tasks[i]));
        if (err != pdPASS)
        {
            putstr(" FAILED to create Test_Task (all cores)\n");
            goto done;
        }
    }
    task_ctrl_wait_yield();
    total_ticks_allcores = xTaskGetTickCount() - start_ticks;
    putstr("\nmulti-core ticks: ");
    putlong(total_ticks_allcores);
    putstr("\n");

    /* Somewhat arbitrary check to confirm multi-core time is faster than single-core */
    if ((total_ticks_allcores * (TEST_TASK_COUNT - 1)) < total_ticks_1core) {
        putstr("PASSED!\n");
    } else {
        putstr("FAILED\n");
    }

done:
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

    putstr("Test running...\n");

    /* Create the control task initially with the high priority. */
    err = xTaskCreate(Core_Task, 
                      "Core_Task", 
                      INIT_TASK_STK_SIZE, 
                      NULL, 
                      TASK_INIT_PRIO, 
                      &handle);
    if (err != pdPASS)
    {
        putstr(" FAILED to create Core_Task\n");
        goto done;
    }
    vTaskCoreAffinitySet(handle, 1);    // Core 0 only

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

