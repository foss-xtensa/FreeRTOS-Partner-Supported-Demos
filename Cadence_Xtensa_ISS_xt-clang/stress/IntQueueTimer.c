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
 * copies or substantial portions of the Software. If you wish to use our Amazon
 * FreeRTOS name, please do so in a fair use way that does not cause confusion.
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

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Demo includes. */
#include "IntQueueTimer.h"
#include "IntQueue.h"

/* Xtensa includes. */
#include <xtensa/corebits.h>
#include <xtensa/config/system.h>
#include <xtensa_api.h>
#include <xtensa/hal.h>
/*-----------------------------------------------------------*/

/* FreeRTOSConfig.h will only define configSTART_INTERRUPT_QUEUE_TESTS
 * if multiple timers exist with different priorities <= EXCM_LEVEL.
 * No assumptions can be made as to timer IDs.
 */
#define SECOND_TIMER_AVAILABLE  configSTART_INTERRUPT_QUEUE_TESTS

/**
 * XT_TIMER_INDEX is used to drive systick. We use XT_TIMER_NEST
 * as second interrupt which runs on a higher priority than
 * systick. This ensures that systick will get interrupted by
 * this timer and hence we can test interrupt nesting.
 */
#define SECOND_TIMER_INDEX					XT_TIMER_NEST

#if (SECOND_TIMER_INDEX == 0)
#define SECOND_TIMER_INT  					XCHAL_TIMER0_INTERRUPT
#elif (SECOND_TIMER_INDEX == 1)
#define SECOND_TIMER_INT  					XCHAL_TIMER1_INTERRUPT
#elif (SECOND_TIMER_INDEX == 2)
#define SECOND_TIMER_INT  					XCHAL_TIMER2_INTERRUPT
#elif (SECOND_TIMER_INDEX == 3)
#define SECOND_TIMER_INT  					XCHAL_TIMER3_INTERRUPT
#endif

/**
 * Frequency of the second timer - This timer is configured at
 * a frequency offset of 17 from the systick timer.
 */
#define SECOND_TIMER_TICK_RATE_HZ			( configTICK_RATE_HZ + 17 )
#define SECOND_TIMER_TICK_DIVISOR			( configCPU_CLOCK_HZ / SECOND_TIMER_TICK_RATE_HZ )
/*-----------------------------------------------------------*/

#if !defined(CONFIG_VERIF)
/* Defined in main_full.c. */
extern BaseType_t xTimerForQueueTestInitialized;
#endif

/*-----------------------------------------------------------*/

/**
 * Interrupt handler for timer interrupt.
 */
#if( SECOND_TIMER_AVAILABLE == 1 )
	static void prvTimer2Handler( void *arg );
#endif /* SECOND_TIMER_AVAILABLE */
/*-----------------------------------------------------------*/

void vInitialiseTimerForIntQueueTest( void )
{
	#if !defined(CONFIG_VERIF)
	/* Inform the tick hook function that it can access queues now. */
	xTimerForQueueTestInitialized = pdTRUE;
	#endif

	#if( SECOND_TIMER_AVAILABLE == 1 )
	{
	unsigned currentCycleCount, firstComparatorValue;

		/* Install the interrupt handler for second timer. */
		xt_set_interrupt_handler( SECOND_TIMER_INT, prvTimer2Handler, NULL );

		/* Read the current cycle count. */
		currentCycleCount = xthal_get_ccount();

		/* Calculate time of the first timer interrupt. */
		firstComparatorValue = currentCycleCount + SECOND_TIMER_TICK_DIVISOR;

		/* Set the comparator. */
		xthal_set_ccompare( SECOND_TIMER_INDEX, firstComparatorValue );

		/* Enable timer interrupt. */
		xt_interrupt_enable( SECOND_TIMER_INT );
	}
	#endif /* SECOND_TIMER_AVAILABLE */
}
/*-----------------------------------------------------------*/

void IntQueueTestTimerHandler( void )
{
	portYIELD_FROM_ISR( xSecondTimerHandler() );
}
/*-----------------------------------------------------------*/

/*
 * Xtensa timers work by comparing a cycle counter with a preset value.
 * Once the match occurs an interrupt is generated, and the handler has
 * to set a new cycle count into the comparator. To avoid clock drift
 * due to interrupt latency, the new cycle count is computed from the
 * old, not the time the interrupt was serviced. However if a timer
 * interrupt is ever serviced more than one tick late, it is necessary
 * to process multiple ticks until the new cycle count is in the future,
 * otherwise the next timer interrupt would not occur until after the
 * cycle counter had wrapped (2^32 cycles later).

do {
    ticks++;
    old_ccompare = read_ccompare_i();
    write_ccompare_i( old_ccompare + divisor );
    service one tick;
    diff = read_ccount() - old_ccompare;
} while ( diff > divisor );
*/
#if( SECOND_TIMER_AVAILABLE == 1 )

	static void prvTimer2Handler( void *arg )
	{
	unsigned oldComparatorValue, newComparatorValue, currentCycleCount;

		/* Unused arguments. */
		( void )arg;

		do
		{
			/* Read old comparator value. */
			oldComparatorValue = xthal_get_ccompare( SECOND_TIMER_INDEX );

			/* Calculate the new comparator value. */
			newComparatorValue = oldComparatorValue + SECOND_TIMER_TICK_DIVISOR;

			/* Update comparator and clear interrupt. */
			xthal_set_ccompare( SECOND_TIMER_INDEX, newComparatorValue );

			/* Process. */
			IntQueueTestTimerHandler();

			/* Ensure comparator update is complete. */
			xthal_icache_sync();

			/* Read current cycle count to check if we need to process more
			 * ticks to catch up. */
			currentCycleCount = xthal_get_ccount();

		} while( ( currentCycleCount - oldComparatorValue ) > SECOND_TIMER_TICK_DIVISOR );
	}

#endif /* SECOND_TIMER_AVAILABLE */
/*-----------------------------------------------------------*/
