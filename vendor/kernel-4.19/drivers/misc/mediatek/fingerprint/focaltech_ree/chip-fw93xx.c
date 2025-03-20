#include <linux/delay.h>

#include "ff_log.h"
#include "ff_err.h"
#include "ff_spi.h"
#include "ff_chip.h"

#undef LOG_TAG
#define LOG_TAG "focaltech:ft93xx"

#define FW93xx_CMD_SRAM_WRITE 0x05
#define FW93xx_CMD_SRAM_READ  0x04

void fw93xx_sram_write(uint16_t addr, uint16_t data)
{
    int tx_len,dlen;
    static uint8_t tx_buffer[MAX_XFER_BUF_SIZE] = {0, };
    ff_sram_buf_t *tx_buf = TYPE_OF(ff_sram_buf_t, tx_buffer);

    FF_LOGV("'%s' enter.", __func__);

    /* Sign the package.  */
    tx_buf->cmd[0] = (uint8_t)( FW93xx_CMD_SRAM_WRITE);
    tx_buf->cmd[1] = (uint8_t)(~FW93xx_CMD_SRAM_WRITE);

    /* Write it repeatedly. */
    dlen = 2;

    /* Packing. */
    tx_buf->addr = u16_swap_endian(addr | 0x8000);
    tx_buf->dlen = u16_swap_endian((dlen/2)?(dlen/2-1):(dlen/2));
    tx_len = sizeof(ff_sram_buf_t)/2*2 + dlen;
    tx_buf->data[0] = data >> 8;
    tx_buf->data[1] = data & 0xff;

    /* Low-level transfer. */
    ff_spi_write_buf(tx_buf, tx_len);
}

uint16_t fw93xx_read_sram(uint16_t addr)
{
    int tx_len;
    int dlen;
    uint8_t ucbuff[8];
    uint16_t ustemp;
    ff_sram_buf_t tx_buffer, *tx_buf = &tx_buffer;
    uint8_t *p_data = TYPE_OF(uint8_t, ucbuff);

    /* Sign the package.  */
    tx_buf->cmd[0] = (uint8_t)( FW93xx_CMD_SRAM_READ);
    tx_buf->cmd[1] = (uint8_t)(~FW93xx_CMD_SRAM_READ);

    /* Read it repeatedly. */
    dlen = 2;

    /* Packing. */
    tx_buf->addr = u16_swap_endian(addr | 0x8000);
    tx_buf->dlen = u16_swap_endian((dlen/2)?(dlen/2-1):(dlen/2));
    tx_len = sizeof(ff_sram_buf_t)/2*2;

        /* Low-level transfer. */
    ff_spi_write_then_read_buf(tx_buf, tx_len, p_data, dlen);

    ustemp = p_data[0];
    ustemp = (ustemp << 8) + p_data[1];

    return ustemp;
}


void fw93xx_int_mask_set(uint16_t usdata)
{
    uint16_t usAddr;

    usAddr = 0x3500/2 + 0x03;
    fw93xx_sram_write(usAddr, usdata);
}

void fw93xx_intflag_clear(uint16_t usdata)
{
    uint16_t usAddr;

    usAddr = 0x3500/2 + 0x04;
    fw93xx_sram_write(usAddr, usdata);
}

uint16_t fw93xx_chipid_get(void)
{
    uint16_t usAddr,usData;

    usAddr = 0x3500/2 + 0x0B;
    usData = fw93xx_read_sram(usAddr);
	FF_LOGE("got device id 0x%04x", usData);
    return usData;
}
