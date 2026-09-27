/**
 * @file boards/stm32f407g-disc1/board.c
 * @brief STM32F407-Discovery board support: clocks, console, LEDs, USART2.
 *
 * Clock bring-up: HSI 16 MHz -> PLL x168/2/4 -> SYSCLK 84 MHz (APB1) with
 * APB2 at 168 MHz. Kept conservative and independent of any vendor library.
 */
#include "mmap.h"
#include "sai/kernel.h"
#include "sai/device.h"
#include "sai/console.h"
#include "sai/port.h"

#define LED_PORT       ((gpio_t *)GPIOD_BASE)
#define LED_ORANGE_PIN 3u     /* LD3 */
#define LED_GREEN_PIN  12u    /* LD4 */
#define LED_RED_PIN    14u    /* LD5 */
#define LED_BLUE_PIN   15u    /* LD6 */

#define CONSOLE_USART  ((usart_t *)USART2_BASE)

/* ------------------------------------------------------------------ */
/* Clock bring-up                                                      */
/* ------------------------------------------------------------------ */
static void clock_init(void)
{
    /* start HSE */
    RCC_CR |= RCC_CR_HSEON;
    while (!(RCC_CR & RCC_CR_HSERDY)) {
    }

    /* PLL: HSE 8 MHz * 336 / 2 / 4 = 168 MHz */
    RCC_PLLCFGR = (8u)                    /* PLLM */
                | (336u << 6)             /* PLLN */
                | (0u << 16)              /* PLLP = /2 -> but we use /4 below */
                | (1u << 22)              /* PLLSRC = HSE */
                | (7u << 24);             /* PLLQ = /7 -> 48 MHz USB */
    /* APB1 = /4 (42 MHz), APB2 = /2 (84 MHz) */
    RCC_CFGR = (RCC_CFGR & ~0xF000u) | (0x5u << 10) | (0x4u << 13);

    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) {
    }

    /* switch SYSCLK to PLL */
    RCC_CFGR = (RCC_CFGR & ~0x3u) | 0x2u;
    while ((RCC_CFGR & (0x3u << 2)) != (0x2u << 2)) {
    }
}

/* ------------------------------------------------------------------ */
/* GPIO                                                                */
/* ------------------------------------------------------------------ */
static void gpio_led_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOD;

    uint32_t pins = ((uint32_t)1u << LED_ORANGE_PIN) |
                    ((uint32_t)1u << LED_GREEN_PIN)  |
                    ((uint32_t)1u << LED_RED_PIN)    |
                    ((uint32_t)1u << LED_BLUE_PIN);

    LED_PORT->MODER &= ~(pins * 3u);
    LED_PORT->MODER |=  (pins);           /* output mode */
    LED_PORT->OTYPER &= ~pins;            /* push-pull   */
}

static void gpio_set_led(uint32_t pin, bool on)
{
    if (on) {
        LED_PORT->BSRR = (1u << pin);
    } else {
        LED_PORT->BSRR = (1u << (pin + 16u));
    }
}

void sai_board_led_set(uint32_t idx, bool on)
{
    switch (idx) {
    case 0: gpio_set_led(LED_ORANGE_PIN, on); break;
    case 1: gpio_set_led(LED_GREEN_PIN, on); break;
    case 2: gpio_set_led(LED_RED_PIN, on); break;
    case 3: gpio_set_led(LED_BLUE_PIN, on); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Console (polled USART2, PA2=TX PA3=RX, 115200 @ 42 MHz APB1)         */
/* ------------------------------------------------------------------ */
static void console_gpio_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOA;

    gpio_t *g = (gpio_t *)GPIOA_BASE;
    uint32_t pins = (1u << 2) | (1u << 3);
    g->MODER &= ~(pins * 3u);
    g->MODER |=  pins * GPIO_MODE_AF;      /* AF mode */
    g->AFRL  &= ~((0xFu << 8) | (0xFu << 12));
    g->AFRL  |=  (7u << 8) | (7u << 12);   /* AF7 = USART2 */
}

static void console_usart_init(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_USART2;

    /* 16x oversampling: BRR = fCK / baud = 42MHz / 115200 ≈ 365 (0x16D) */
    CONSOLE_USART->BRR = 365;
    CONSOLE_USART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void port_putchar(char c)
{
    if (c == '\n') {
        while (!(CONSOLE_USART->SR & USART_SR_TXE)) {
        }
        CONSOLE_USART->DR = '\r';
    }
    while (!(CONSOLE_USART->SR & USART_SR_TXE)) {
    }
    CONSOLE_USART->DR = (uint8_t)c;
}

int stm32_uart_poll_in(sai_device_t *dev, char *c)
{
    (void)dev;
    if (CONSOLE_USART->SR & USART_SR_RXNE) {
        *c = (char)(CONSOLE_USART->DR & 0xFFu);
        return 0;
    }
    return SAI_ERR_WOULD_BLOCK;
}

int stm32_uart_poll_out(sai_device_t *dev, char c)
{
    (void)dev;
    port_putchar(c);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Device registration                                                 */
/* ------------------------------------------------------------------ */
static const sai_dev_ops_t console_ops = {
    .uart = { stm32_uart_poll_out, stm32_uart_poll_in },
};

static sai_device_t console_device;
static sai_device_t led_device;

static int led_configure(sai_device_t *d, uint32_t pin, uint32_t flags)
{
    (void)d; (void)flags;
    gpio_led_init();
    (void)pin;
    return 0;
}

static int led_set(sai_device_t *d, uint32_t pin, bool value)
{
    (void)d;
    sai_board_led_set(pin, value);
    return 0;
}

static int led_get(sai_device_t *d, uint32_t pin, bool *value)
{
    (void)d;
    *value = (LED_PORT->ODR >> pin) & 1u;
    return 0;
}

static int led_toggle(sai_device_t *d, uint32_t pin)
{
    (void)d;
    LED_PORT->ODR ^= (1u << pin);
    return 0;
}

static const sai_dev_ops_t led_ops = {
    .gpio = { led_configure, led_set, led_get, led_toggle },
};

void sai_board_init(void)
{
    clock_init();
    gpio_led_init();
    console_gpio_init();
    console_usart_init();

    (void)sai_device_register(&console_device, "console0", SAI_DEVICE_UART,
                              0x100u, CONSOLE_USART, 38u, &console_ops, NULL);
    (void)sai_device_register(&led_device, "leds", SAI_DEVICE_GPIO,
                              0x101u, LED_PORT, 0u, &led_ops, NULL);
}

void sai_printf_safe(const char *fmt, ...)
{
    /* crash-safe console: polled UART, no locks, no formatting state */
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)sai_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sai_uart_puts(sai_device_get_by_node(0x100u), buf);
}

void board_early_init(void)
{
    sai_board_init();
}
