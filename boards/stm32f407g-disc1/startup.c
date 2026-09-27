/**
 * @file boards/stm32f407g-disc1/startup.c
 * @brief STM32F407 board glue: USART2 ISR feeding the RX ring, device setup.
 */
#include "mmap.h"
#include "sai/kernel.h"
#include "sai/device.h"
#include "sai/ipc.h"
#include "sai/console.h"
#include "sai/port.h"

void sai_board_init(void);

/* ------------------------------------------------------------------ */
/* USART2 ISR: push received byte into the UART RX ring (never block). */
/* ------------------------------------------------------------------ */
extern sai_status_t sai_uart_rx_push(sai_device_t *dev, uint8_t byte);

static sai_device_t *s_console;

void USART2_IRQHandler(void)
{
    usart_t *u = (usart_t *)USART2_BASE;
    if (u->SR & USART_SR_RXNE) {
        uint8_t c = (uint8_t)(u->DR & 0xFFu);
        if (s_console != NULL) {
            (void)sai_uart_rx_push(s_console, c);
        }
    }
    /* TX complete interrupt not used: TX is polled */
}

/* ------------------------------------------------------------------ */
/* Hook the console device so the ISR can find it                       */
/* ------------------------------------------------------------------ */
sai_status_t sai_board_console_setup(void)
{
    sai_device_t *d = sai_device_get_by_node(0x100u);
    if (d == NULL) {
        return SAI_ERR_NOENT;
    }
    s_console = d;

    /* enable RX interrupt */
    usart_t *u = (usart_t *)USART2_BASE;
    u->CR1 |= USART_CR1_RXNEIE;

    /* NVIC: enable USART2 IRQ (38) at lowest urgency */
    volatile uint32_t *iser = (volatile uint32_t *)0xE000E100u;
    iser[38u / 32u] = (1u << (38u % 32u));
    volatile uint32_t *ipr = (volatile uint32_t *)0xE000E400u;
    uint8_t *prio8 = (uint8_t *)ipr;
    prio8[38u] = 0xF0u;

    return SAI_OK;
}

