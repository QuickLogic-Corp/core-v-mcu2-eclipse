/*
 * Copyright 2020 ETH Zurich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Author: Robert Balas (balasr@iis.ee.ethz.ch)
 */

#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include <FreeRTOS.h>
#include "FreeRTOSConfig.h"

#include "system_pulpissimo_ri5cy_metal.h"

#include "fll.h"
#include "irq.h"
#include "soc_eu.h"

#ifndef DISABLE_WDOG
#define DISABLE_WDOG 1
#endif

/* test some assumptions we make about compiler settings */
static_assert(sizeof(uintptr_t) == 4,
	      "uintptr_t is not 4 bytes. Make sure you are using -mabi=ilp32*");

/* Allocate heap to special section. Note that we have no references in the
 * whole program to this variable (since its just here to allocate space in the
 * section for our heap), so when using LTO it will be removed. We force it to
 * stay with the "used" attribute
 */
__attribute__((section(".heap"), used)) uint8_t ucHeap[configTOTAL_HEAP_SIZE];

/* Inform linker script about .heap section size. Note: GNU ld seems to
 * internally represent integers with the bfd_vma type, that is a type that can
 * contain memory addresses (typdefd to some int type depending on the
 * architecture). uint32_t seems to me the most fitting candidate for rv32.
 */
uint32_t __heap_size = configTOTAL_HEAP_SIZE;

uint32_t volatile system_core_clock = DEFAULT_SYSTEM_CLOCK;

/* FreeRTOS task handling */
BaseType_t xTaskIncrementTick(void);
void vTaskSwitchContext(void);

/* interrupt handling */
void timer_irq_handler(void);
void undefined_handler(void);
void (*isr_table[32])(void);

/**
 * Board init code. Always call this before anything else.
 */
void pulp_sys_init(void)
{
	/* TODO: check this code */
	pulp_fll_init();

	/* make sure irq (itc) is a good state */
	pulp_irq_init();

	/* Hook up isr table. This table is temporary until we figure out how to
	 * do proper vectored interrupts.
	 */
	isr_table[0xa] = timer_irq_handler;

	/* mtvec is set in crt0.S */

	/* deactivate all soc events as they are enabled by default */
	pulp_soc_eu_event_init();

	/* TODO: enable uart */
	/* TODO: I$ enable*/
	/* enable core level interrupt (mie) */
	irq_clint_enable();
}

//
void system_core_clock_update()
{
	//    SystemCoreClock = FLL_GetFrequency(uFLL_SOC);

	/* Need to update clock divider for each peripherals */
	//    uart_is_init     = 0;
}

void timer_irq_handler(void)
{
#warning requires critical section if interrupt nesting is used.

	if (xTaskIncrementTick() != 0) {
		vTaskSwitchContext();
	}
}

void undefined_handler(void)
{
#ifdef __PULP_USE_LIBC
	abort();
#else
	taskDISABLE_INTERRUPTS();
	for(;;);
#endif
}

void vPortSetupTimerInterrupt(void)
{
	extern int timer_irq_init(uint32_t ticks);

	/* No CLINT so use the PULP timer to generate the tick interrupt. */
	/* TODO: configKERNEL_INTERRUPT_PRIORITY - 1 ? */
	timer_irq_init(ARCHI_REF_CLOCK / configTICK_RATE_HZ);
	/* TODO: allow setting interrupt priority (to super high(?)) */
	irq_enable(IRQ_FC_EVT_TIMER0_LO);
}

void vSystemIrqHandler(uint32_t mcause)
{
	extern void (*isr_table[32])(void);
	isr_table[mcause & 0x1f]();
}
