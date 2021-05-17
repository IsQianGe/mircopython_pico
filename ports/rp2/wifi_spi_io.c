#include <stdlib.h>
#include "wifi_spi_io.h"
#include "hardware/gpio.h"

#define GPIOHS_OUT_HIGH(io) (*(volatile uint32_t *)0x3800100CU) |= (1 << (io))
#define GPIOHS_OUT_LOWX(io) (*(volatile uint32_t *)0x3800100CU) &= ~(1 << (io))

#define GET_GPIOHS_VALX(io) (((*(volatile uint32_t *)0x38001000U) >> (io)) & 1)

uint8_t cs_num = 0, rst_num = 0, rdy_num = 0;

static uint8_t _mosi_num = -1;
static uint8_t _miso_num = -1;
static uint8_t _sclk_num = -1;

void esp8285_spi_config_io(uint8_t cs, uint8_t rst, uint8_t rdy, uint8_t mosi, uint8_t miso, uint8_t sclk)
{
    //clk
    gpio_set_dir(sclk, GPIO_OUT);
    gpio_put(sclk, 0);
    //mosi
    gpio_set_dir(mosi, GPIO_OUT);
    gpio_put(mosi, 0);
    //miso
    gpio_set_dir(miso, GPIO_IN);

    _mosi_num = mosi;
    _miso_num = miso;
    _sclk_num = sclk;

    cs_num = cs;
    rdy_num = rdy;
    rst_num = rst; //if rst <0, use soft reset
}
uint8_t soft_spi_rw(uint8_t data)
{
    uint8_t i;
    uint8_t temp = 0;
    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            gpio_put(_mosi_num, 1);
        }
        else
        {
            gpio_put(_mosi_num, 0);
        }
        data <<= 1;
        gpio_put(_sclk_num, 1);

        asm volatile("nop");
        asm volatile("nop");
        asm volatile("nop");

        temp <<= 1;
        if (gpio_get(_miso_num))
        {
            temp++;
        }
        gpio_put(_sclk_num, 0);

        asm volatile("nop");
        asm volatile("nop");
        asm volatile("nop");
    }
    return temp;
}
void soft_spi_rw_len(uint8_t *send, uint8_t *recv, uint32_t len)
{
    if (send == NULL && recv == NULL)
    {
        //printf(" buffer is null\r\n");
        return;
    }

#if 0
    uint32_t i = 0;
    do
    {
        *(recv + i) = soft_spi_rw(*(send + i));
        i++;
    } while (--len);
#else

    uint32_t i = 0;
    uint8_t *stmp = NULL, sf = 0;

    if (send == NULL)
    {
        stmp = (uint8_t *)malloc(len * sizeof(uint8_t));
        // memset(stmp, 'A', len);
        sf = 1;
    }
    else
        stmp = send;

    if (recv == NULL)
    {
        do
        {
            soft_spi_rw(*(stmp + i));
            i++;
        } while (--len);
    }
    else
    {
        do
        {
            *(recv + i) = soft_spi_rw(*(stmp + i));
            i++;
        } while (--len);
    }

    if (sf)
        free(stmp);
#endif
}