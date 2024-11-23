/*
 * Copyright (C) Kris Kwiatkowski, Among Bytes LTD
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <libopencm3/cm3/dwt.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/rng.h>
#include <libopencm3/stm32/usart.h>
#include <platform/platform.h>
#include <stddef.h>

#include "printf.h"

#define SERIAL_GPIO GPIOG
#define SERIAL_USART LPUART1
#define SERIAL_PINS (GPIO8 | GPIO7)

__attribute__((unused)) static uint32_t _clock_freq;

/* Patched function for newer PLL not yet supported by opencm3 */
void _rcc_set_main_pll(uint32_t source, uint32_t pllm, uint32_t plln,
                       uint32_t pllp, uint32_t pllq, uint32_t pllr) {
    RCC_PLLCFGR = (RCC_PLLCFGR_PLLM(pllm) << RCC_PLLCFGR_PLLM_SHIFT) |
                  (plln << RCC_PLLCFGR_PLLN_SHIFT) |
                  ((pllp & 0x1Fu) << 27u) | /* NEWER PLLP */
                  (source << RCC_PLLCFGR_PLLSRC_SHIFT) |
                  (pllq << RCC_PLLCFGR_PLLQ_SHIFT) |
                  (pllr << RCC_PLLCFGR_PLLR_SHIFT) | RCC_PLLCFGR_PLLREN;
}

/// ############################
/// Internal implementation
/// ############################

volatile unsigned long long stm32_sys_tick_overflowcnt = 0;

static void usart_setup(int baud) {
    rcc_periph_clock_enable(RCC_GPIOG);
    rcc_periph_clock_enable(RCC_LPUART1);

    PWR_CR2 |= PWR_CR2_IOSV;
    gpio_set_output_options(SERIAL_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ,
                            SERIAL_PINS);
    gpio_set_af(SERIAL_GPIO, GPIO_AF8, SERIAL_PINS);
    gpio_mode_setup(SERIAL_GPIO, GPIO_MODE_AF, GPIO_PUPD_NONE, SERIAL_PINS);

    usart_set_baudrate(SERIAL_USART, baud);
    usart_set_databits(SERIAL_USART, 8);
    usart_set_stopbits(SERIAL_USART, USART_STOPBITS_1);
    usart_set_mode(SERIAL_USART, USART_MODE_TX_RX);
    usart_set_parity(SERIAL_USART, USART_PARITY_NONE);
    usart_set_flow_control(SERIAL_USART, USART_FLOWCONTROL_NONE);
    usart_disable_rx_interrupt(SERIAL_USART);
    usart_disable_tx_interrupt(SERIAL_USART);
    usart_enable(SERIAL_USART);
}

static void systick_setup(void) {
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(0xFFFFFFu);
    systick_interrupt_enable();
    systick_counter_enable();
}

static void set_clock(platform_attr_stm32_t a) {
    // Benchmark @ 20MhZ
    static const size_t speed_benchMhZ           = 20000000;
    static const unsigned flash_wait_state_bench = FLASH_ACR_LATENCY_0WS;
    // Benchmark @ 120MhZ
    static const size_t speed_fastMhz           = 120000000;
    static const unsigned flash_wait_state_fast = 0x05;
    size_t speedMhZ;
    size_t flash_wait_state;

    if (a == PLATFORM_CLOCK_MAX) {
        speedMhZ         = speed_fastMhz;
        flash_wait_state = flash_wait_state_fast;
    } else if (a == PLATFORM_CLOCK_USERSPACE) {
        speedMhZ         = speed_benchMhZ;
        flash_wait_state = flash_wait_state_bench;
    } else {
        // Do nothing
        return;
    }

    rcc_periph_clock_enable(RCC_PWR);
    rcc_periph_clock_enable(RCC_SYSCFG);
    pwr_set_vos_scale(PWR_SCALE1);

    rcc_osc_on(RCC_HSI16);
    rcc_wait_for_osc_ready(RCC_HSI16);
    rcc_ahb_frequency  = speedMhZ;
    rcc_apb1_frequency = speedMhZ;
    rcc_apb2_frequency = speedMhZ;
    _clock_freq        = speedMhZ;
    rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
    rcc_set_ppre1(RCC_CFGR_PPRE_NODIV);
    rcc_set_ppre2(RCC_CFGR_PPRE_NODIV);
    rcc_osc_off(RCC_PLL);

    while (rcc_is_osc_ready(RCC_PLL)) {};

    if (a == PLATFORM_CLOCK_MAX) {
        /* Configure the PLL oscillator (use CUBEMX tool -> scale HSI16 to 120MHz). */
        _rcc_set_main_pll(RCC_PLLCFGR_PLLSRC_HSI16, 1, 15, 2,
                          RCC_PLLCFGR_PLLQ_DIV2, RCC_PLLCFGR_PLLR_DIV2);
        rcc_osc_on(RCC_PLL);
        rcc_wait_for_osc_ready(RCC_PLL);
    } else {
        /* Configure the PLL oscillator (use CUBEMX tool -> scale HSI16 to 20MHz). */
        _rcc_set_main_pll(RCC_PLLCFGR_PLLSRC_HSI16, 1, 10, 2,
                          RCC_PLLCFGR_PLLQ_DIV2, RCC_PLLCFGR_PLLR_DIV8);
        rcc_osc_on(RCC_PLL);
    }
    /* Enable PLL oscillator and wait for it to stabilize. */
    flash_dcache_enable();
    flash_icache_enable();
    flash_set_ws(flash_wait_state);
    flash_prefetch_enable();
    rcc_set_sysclk_source(RCC_CFGR_SW_PLL);
    rcc_wait_for_sysclk_status(RCC_PLL);
}

static void setup_rng(void) {
    rcc_osc_on(RCC_HSI48); /* HSI48 must always be on for RNG */
    rcc_wait_for_osc_ready(RCC_HSI48);
    rcc_periph_clock_enable(RCC_RNG);
    rcc_set_clock48_source(RCC_CCIPR_CLK48SEL_HSI48);
    rng_enable();
}

// Implements printf. Send a char to the terminal.
void _putchar(char character) {
    usart_send_blocking(SERIAL_USART, (unsigned char)(character));
}

static volatile unsigned long long overflowcnt = 0;

/// ############################
/// External API
/// ############################

int platform_init(void) {
    set_clock(PLATFORM_CLOCK_MAX);
    setup_rng();
    usart_setup(115200);
    systick_setup();
    // wait for the first systick overflow
    // improves reliability of the benchmarking scripts since it makes it much
    // less likely that the host will miss the start of the output
    return 0;
}

void platform_set_attr(const struct platform_attr_t *a) {
    size_t i;
    for (i = 0; i < a->n; i++) {
        switch (a->attr[i]) {
            case PLATFORM_CLOCK_USERSPACE:
                set_clock(PLATFORM_CLOCK_USERSPACE);
                break;
            case PLATFORM_CLOCK_MAX:
                set_clock(PLATFORM_CLOCK_MAX);
            default:
                break;
        }
    }
}

uint64_t platform_cpu_cyclecount(void) {
    while (true) {
        unsigned long long before = stm32_sys_tick_overflowcnt;
        unsigned long long result =
            (before + 1) * 16777216llu - systick_get_value();
        if (stm32_sys_tick_overflowcnt == before) {
            return result;
        }
    }
}
