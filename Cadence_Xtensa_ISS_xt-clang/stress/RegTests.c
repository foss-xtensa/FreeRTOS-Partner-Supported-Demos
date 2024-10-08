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

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"

/* Interface include files. */
#include "RegTests.h"

/* Tasks that implement register tests. */
static void prvRegisterTest1Task( void *pvParameters );
static void prvRegisterTest2Task( void *pvParameters );
static void prvRegisterTest3Task( void *pvParameters );
static void prvRegisterTest4Task( void *pvParameters );

/* Flag that will be latched to pdTRUE should any unexpected behaviour be
detected in any of the tasks. */
static volatile BaseType_t xErrorDetected = pdFALSE;

/* Counters that are incremented on each cycle of a test.  This is used to
detect a stalled task - a test that is no longer running. */
volatile uint32_t ulRegisterTest1Counter = 0;
volatile uint32_t ulRegisterTest2Counter = 0;
volatile uint32_t ulRegisterTest3Counter = 0;
volatile uint32_t ulRegisterTest4Counter = 0;

/* Register tests implemented in assembly */
extern void vRegTest1(void);
extern void vRegTest2(void);
extern void vRegTest3(void);
extern void vRegTest4(void);

/*-----------------------------------------------------------*/

static void prvRegisterTest1Task( void *pvParameters )
{
	( void ) pvParameters;

	/* Although the regtest task is written in assembly, its entry point is
	 * written in C for convenience of checking the task parameter is being
	 * passed in correctly. */

	/* 1. Fill the registers stored as part of task context with known values.
	 * 2. Force a context switch.
	 * 3. Verify that all the registers contain expected values.
	 * 4. If all the register contain expected values, increment ulRegisterTest1Counter.
	 */
	vRegTest1();
}
/*-----------------------------------------------------------*/

static void prvRegisterTest2Task( void *pvParameters )
{
	( void ) pvParameters;

	/* Although the regtest task is written in assembly, its entry point is
	 * written in C for convenience of checking the task parameter is being
	 * passed in correctly. */

	/* 1. Fill the registers stored as part of task context with known values.
	 * 2. Force a context switch.
	 * 3. Verify that all the registers contain expected values.
	 * 4. If all the register contain expected values, increment ulRegisterTest2Counter.
	 */
	vRegTest2();
}
/*-----------------------------------------------------------*/

static void prvRegisterTest3Task( void *pvParameters )
{
	( void ) pvParameters;

	/* Although the regtest task is written in assembly, its entry point is
	 * written in C for convenience of checking the task parameter is being
	 * passed in correctly. */

	/* 1. Fill the registers stored as part of task context with known values.
	 * 2. Force a context switch.
	 * 3. Verify that all the registers contain expected values.
	 * 4. If all the register contain expected values, increment ulRegisterTest2Counter.
	 */
	vRegTest3();
}
/*-----------------------------------------------------------*/

static void prvRegisterTest4Task( void *pvParameters )
{
	( void ) pvParameters;

	/* Although the regtest task is written in assembly, its entry point is
	 * written in C for convenience of checking the task parameter is being
	 * passed in correctly. */

	/* 1. Fill the registers stored as part of task context with known values.
	 * 2. Force a context switch.
	 * 3. Verify that all the registers contain expected values.
	 * 4. If all the register contain expected values, increment ulRegisterTest2Counter.
	 */
	vRegTest4();
}
/*-----------------------------------------------------------*/

void vStartRegisterTasks( UBaseType_t uxPriority )
{
	BaseType_t ret;

	ret = xTaskCreate( prvRegisterTest1Task, "RegTest1", configMINIMAL_STACK_SIZE, NULL, uxPriority, NULL );
	configASSERT( ret == pdPASS );

	ret = xTaskCreate( prvRegisterTest2Task, "RegTest2", configMINIMAL_STACK_SIZE, NULL, uxPriority, NULL );
	configASSERT( ret == pdPASS );

	ret = xTaskCreate( prvRegisterTest3Task, "RegTest3", configMINIMAL_STACK_SIZE, NULL, uxPriority, NULL );
	configASSERT( ret == pdPASS );

	ret = xTaskCreate( prvRegisterTest4Task, "RegTest4", configMINIMAL_STACK_SIZE, NULL, uxPriority, NULL );
	configASSERT( ret == pdPASS );
}
/*-----------------------------------------------------------*/

BaseType_t xAreRegisterTasksStillRunning( void )
{
static uint32_t ulLastRegisterTest1Counter = 0, ulLastRegisterTest2Counter = 0;
static uint32_t ulLastRegisterTest3Counter = 0, ulLastRegisterTest4Counter = 0;

	/* If the register test task is still running then we expect the loop
	 * counters to have incremented since this function was last called. */
	if( ulLastRegisterTest1Counter == ulRegisterTest1Counter )
	{
		xErrorDetected = pdTRUE;
	}

	if( ulLastRegisterTest2Counter == ulRegisterTest2Counter )
	{
		xErrorDetected = pdTRUE;
	}

	if( ulLastRegisterTest3Counter == ulRegisterTest3Counter )
	{
		xErrorDetected = pdTRUE;
	}

	if( ulLastRegisterTest4Counter == ulRegisterTest4Counter )
	{
		xErrorDetected = pdTRUE;
	}

	ulLastRegisterTest1Counter = ulRegisterTest1Counter;
	ulLastRegisterTest2Counter = ulRegisterTest2Counter;
	ulLastRegisterTest3Counter = ulRegisterTest3Counter;
	ulLastRegisterTest4Counter = ulRegisterTest4Counter;

	/* Errors detected in the task itself will have latched xErrorDetected
	 * to true. */
	return ( BaseType_t ) !xErrorDetected;
}
/*-----------------------------------------------------------*/
