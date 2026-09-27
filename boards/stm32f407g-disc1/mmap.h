/**
 * @file boards/stm32f407g-disc1/mmap.h
 * @brief STM32F407 register definitions (only what the drivers need).
 */
#ifndef STM32F407_MMAP_H
#define STM32F407_MMAP_H

#include <stdint.h>

/* =================================================================== */
/* RCC                                                                  */
/* =================================================================== */
#define RCC_BASE            0x40023800u
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00u))
#define RCC_PLLCFGR         (*(volatile uint32_t *)(RCC_BASE + 0x04u))
#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x08u))
#define RCC_AHB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x30u))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x40u))
#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x44u))

#define RCC_CR_HSION        (1u << 0)
#define RCC_CR_HSIRDY       (1u << 1)
#define RCC_CR_HSEON        (1u << 16)
#define RCC_CR_HSERDY       (1u << 17)
#define RCC_CR_PLLON        (1u << 24)
#define RCC_CR_PLLRDY       (1u << 25)

/* AHB1 peripherals */
#define RCC_AHB1ENR_GPIOA   (1u << 0)
#define RCC_AHB1ENR_GPIOB   (1u << 1)
#define RCC_AHB1ENR_GPIOC   (1u << 2)
#define RCC_AHB1ENR_GPIOD   (1u << 3)
#define RCC_AHB1ENR_GPIOE   (1u << 4)
#define RCC_APB1ENR_USART2  (1u << 17)
#define RCC_APB2ENR_USART1  (1u << 4)

/* =================================================================== */
/* GPIO                                                                 */
/* =================================================================== */
#define GPIOA_BASE          0x40020000u
#define GPIOB_BASE          0x40020400u
#define GPIOC_BASE          0x40020800u
#define GPIOD_BASE          0x40020C00u
#define GPIOE_BASE          0x40021000u

typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFRL;
    volatile uint32_t AFRH;
} gpio_t;

#define GPIO_MODE_INPUT     0x0u
#define GPIO_MODE_OUTPUT    0x1u
#define GPIO_MODE_AF        0x2u
#define GPIO_MODE_ANALOG    0x3u

/* =================================================================== */
/* USART                                                                */
/* =================================================================== */
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} usart_t;

#define USART1_BASE         0x40011000u
#define USART2_BASE         0x40004400u
#define USART6_BASE         0x40011400u

#define USART_SR_TXE        (1u << 7)
#define USART_SR_RXNE       (1u << 5)
#define USART_SR_TC         (1u << 6)
#define USART_CR1_UE        (1u << 13)
#define USART_CR1_RE        (1u << 2)
#define USART_CR1_TE        (1u << 3)
#define USART_CR1_RXNEIE    (1u << 5)
#define USART_CR1_TCIE      (1u << 6)

/* =================================================================== */
/* SPI / I2C register bases                                             */
/* =================================================================== */
#define SPI1_BASE           0x40013000u
#define SPI2_BASE           0x40003800u
#define I2C1_BASE           0x40005400u

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t CRCPR;
    volatile uint32_t RXCRCR;
    volatile uint32_t TXCRCR;
    volatile uint32_t I2SCFGR;
    volatile uint32_t I2SPR;
} spi_t;

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t DR;
    volatile uint32_t SR1;
    volatile uint32_t SR2;
    volatile uint32_t CCR;
    volatile uint32_t TRISE;
} i2c_t;

/* TIM */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SMCR;
    volatile uint32_t DIER;
    volatile uint32_t SR;
    volatile uint32_t EGR;
    volatile uint32_t CCMR1;
    volatile uint32_t CCMR2;
    volatile uint32_t CCER;
    volatile uint32_t CNT;
    volatile uint32_t PSC;
    volatile uint32_t ARR;
} tim_t;

#define TIM2_BASE           0x40000000u
#define TIM5_BASE           0x40000C00u

#endif /* STM32F407_MMAP_H */
