/*
 *
 * The spi driver for FocalTech FingerPrint driver.
 *
 * Copyright (c) 2017-2022, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "ff_core.h"
#include "ff_spi.h"
#if IS_ENABLED(CONFIG_TRUSTKERNEL_TEE_SUPPORT)
#include "linux/tee_fp.h"
#endif

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#undef LOG_TAG
#define LOG_TAG "focaltech_chip"

#define FF_CMD_SFR_WRITE                        (0x09)
#define FF_CMD_SFR_READ                         (0x08)
#define FF_CMD_SRAM_WRITE                       (0x05)
#define FF_CMD_SRAM_READ                        (0x04)
#define FF_SPI_BUF_SIZE                         (32)

/*****************************************************************************
* static variable or structure
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
#if IS_ENABLED(CONFIG_TRUSTKERNEL_TEE_SUPPORT)
static struct mt_chip_conf ff_mt_chip_conf = {
    .setuptime    = 30,
    .holdtime     = 30,
    .high_time    = 25,
    .low_time     = 25, /* default 2MHz */
    .cs_idletime  = 10,
    .ulthgh_thrsh = 0,
    .sample_sel   = POSITIVE_EDGE,
    .cpol         = SPI_CPOL_0,
    .cpha         = SPI_CPHA_0,
    .rx_mlsb      = SPI_MSB,
    .tx_mlsb      = SPI_MSB,
    .tx_endian    = SPI_LENDIAN,
    .rx_endian    = SPI_LENDIAN,
    .com_mod      = FIFO_TRANSFER, /*only read ID*/
    .pause        = PAUSE_MODE_DISABLE,
    .finish_intr  = FINISH_INTR_EN,
    .deassert     = DEASSERT_DISABLE,
    .ulthigh      = ULTRA_HIGH_DISABLE,
    .tckdly       = TICK_DLY0,
};

static int ff_spi_transfer(uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len)
{
    int ret = 0;

    FF_LOGV("'%s' enter.", __func__);
    if ((!tx_buf) || (!rx_buf) || (!len)) {
        FF_LOGE("tx_buf/rx_buf/len(%d) is null", len);
        return -EINVAL;
    }

    ret = tee_spi_transfer(&ff_mt_chip_conf, sizeof(struct mt_chip_conf), tx_buf, rx_buf, len);
    if (ret) {
        FF_LOGE("tee_spi_transfer fail, ret=%d", ret);
    }
    FF_LOGV("'%s' leave.", __func__);
    return ret;
}
#endif

#if !IS_ENABLED(CONFIG_TRUSTKERNEL_TEE_SUPPORT)
static int ff_spi_transfer(uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len)
{
    int ret = 0;
    ff_context_t *ff_ctx = g_ff_ctx;
    struct spi_message msg;
    struct spi_transfer xfer = { 0 };
    uint8_t *kernel_tx_buf = NULL;
    uint8_t *kernel_rx_buf = NULL;

    if ((!ff_ctx) || (!(ff_ctx->spi)) || (!tx_buf) || (!rx_buf) || (!len)) {
        FF_LOGE("ff_ctx/spi/tx_buf/rx_buf/len(%d) is null", len);
        return -ENODATA;
    }

    kernel_tx_buf = kzalloc(FF_SPI_BUF_SIZE, GFP_KERNEL);
    kernel_rx_buf = kzalloc(FF_SPI_BUF_SIZE, GFP_KERNEL);
    if ((!kernel_tx_buf) || (!kernel_rx_buf)) {
        FF_LOGE("failed to allocate memory for kernel_tx/rx_buf");
        ret = -ENOMEM;
        goto err_spi_transfer;
    }

    memcpy(kernel_tx_buf, tx_buf, len);
    xfer.tx_buf = kernel_tx_buf;
    xfer.rx_buf = kernel_rx_buf;
    xfer.len    = len;
    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);
    ret = spi_sync(ff_ctx->spi, &msg);
    if (ret) {
        FF_LOGE("spi_sync fail,ret:%d", ret);
    }
    memcpy(rx_buf, kernel_rx_buf, len);

err_spi_transfer:
    if (kernel_tx_buf) {
        kfree(kernel_tx_buf);
        kernel_tx_buf = NULL;
    }

    if (kernel_rx_buf) {
        kfree(kernel_rx_buf);
        kernel_rx_buf = NULL;
    }
    return ret;
}
#endif

static int ff_sfr_write(uint8_t addr, uint8_t value)
{
    int ret = 0;
    uint8_t txbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint8_t rxbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint32_t txlen = 0;

    FF_LOGV("'%s' enter.", __func__);
    memset(txbuf, 0, FF_SPI_BUF_SIZE);
    memset(rxbuf, 0, FF_SPI_BUF_SIZE);
    txbuf[txlen++] = (uint8_t)(FF_CMD_SFR_WRITE);
    txbuf[txlen++] = (uint8_t)(~FF_CMD_SFR_WRITE);
    txbuf[txlen++] = addr;
    txbuf[txlen++] = value;
    ret = ff_spi_transfer(txbuf, rxbuf, txlen);
    if (ret < 0) {
        FF_LOGE("ff_sfr_write fail,addr=0x%x,ret=%d", addr, ret);
    }
    FF_LOGV("'%s' leave.", __func__);
    return ret;
}

static int ff_sfr_read(uint8_t addr, uint8_t *value)
{
    int ret = 0;
    uint8_t txbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint8_t rxbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint32_t txlen = 0;

    FF_LOGV("'%s' enter.", __func__);
    memset(txbuf, 0, FF_SPI_BUF_SIZE);
    memset(rxbuf, 0, FF_SPI_BUF_SIZE);
    txbuf[txlen++] = (uint8_t)(FF_CMD_SFR_READ);
    txbuf[txlen++] = (uint8_t)(~FF_CMD_SFR_READ);
    txbuf[txlen++] = addr;
    txbuf[txlen++] = 0;
    txbuf[txlen++] = 0;
    ret = ff_spi_transfer(txbuf, rxbuf, txlen);
    if (ret < 0) {
        FF_LOGE("ff_sfr_read fail,addr=0x%x,ret=%d", addr, ret);
    } else {
        *value = rxbuf[txlen - 1];
    }
    FF_LOGV("'%s' leave.", __func__);
    return ret;
}

static int ff_sram_write(uint16_t addr, uint16_t value)
{
    int ret = 0;
    uint8_t txbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint8_t rxbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint32_t txlen = 0;
    uint16_t sram_addr = (addr | 0x8000);
    uint16_t sram_len = 0x0000;

    FF_LOGV("'%s' enter.", __func__);
    memset(txbuf, 0, FF_SPI_BUF_SIZE);
    memset(rxbuf, 0, FF_SPI_BUF_SIZE);
    txbuf[txlen++] = (uint8_t)(FF_CMD_SRAM_WRITE);
    txbuf[txlen++] = (uint8_t)(~FF_CMD_SRAM_WRITE);
    txbuf[txlen++] = (uint8_t)((sram_addr >> 8) & 0x00FF);
    txbuf[txlen++] = (uint8_t)(sram_addr & 0x00FF);
    txbuf[txlen++] = (uint8_t)((sram_len >> 8) & 0x00FF);
    txbuf[txlen++] = (uint8_t)(sram_len & 0x00FF);
    txbuf[txlen++] = (uint8_t)((value >> 8) & 0x00FF);
    txbuf[txlen++] = (uint8_t)(value & 0x00FF);
    ret = ff_spi_transfer(txbuf, rxbuf, txlen);
    if (ret < 0) {
        FF_LOGE("ff_sram_write fail,addr=0x%x,ret=%d", addr, ret);
    }
    FF_LOGV("'%s' leave.", __func__);
    return ret;
}


static int ff_sram_read(uint16_t addr, uint16_t *value)
{
    int ret = 0;
    int i = 0;
    uint8_t txbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint8_t rxbuf[FF_SPI_BUF_SIZE] = { 0 };
    uint32_t txlen = 0;
    uint32_t dp = 0;
    uint16_t sram_addr = (addr | 0x8000);
    uint16_t sram_len = 0x0001;

    FF_LOGV("'%s' enter.", __func__);
    memset(txbuf, 0, FF_SPI_BUF_SIZE);
    memset(rxbuf, 0, FF_SPI_BUF_SIZE);
    txbuf[txlen++] = (uint8_t)(FF_CMD_SRAM_READ);
    txbuf[txlen++] = (uint8_t)(~FF_CMD_SRAM_READ);
    txbuf[txlen++] = (uint8_t)((sram_addr >> 8) & 0x00FF);
    txbuf[txlen++] = (uint8_t)(sram_addr & 0x00FF);
    txbuf[txlen++] = (uint8_t)((sram_len >> 8) & 0x00FF);
    txbuf[txlen++] = (uint8_t)(sram_len & 0x00FF);
    dp = txlen;
    txlen += 4;
    ret = ff_spi_transfer(txbuf, rxbuf, txlen);
    if (ret < 0) {
        FF_LOGE("ff_sram_read fail,addr=0x%x,ret=%d", addr, ret);
    } else {
        *value = (rxbuf[dp] << 8) + rxbuf[dp + 1];
    }
    FF_LOGV("'%s' leave.", __func__);
    return ret;
}

static int ff_chip_probe_id(void)
{
    int ret = 0;
    int i = 0;
    int read_id_retries = 0;
    uint16_t chip_id = 0xFFFF;
    uint8_t val = 0xFF;
    uint8_t set_iov_tries = 5;
    uint8_t set_spimode_tries = 3;

#if IS_ENABLED(CONFIG_TRUSTKERNEL_TEE_SUPPORT)
    /*max spi trasfer count:20*/
    set_iov_tries = 1;
    set_spimode_tries = 1;
#endif

    FF_LOGV("'%s' enter.", __func__);
    for (read_id_retries = 0; read_id_retries < 3; read_id_retries++) {
        fts_power_sequence(1);

        /*set io voltage*/
        for (i = 0; i < set_iov_tries; i++) {
            ff_sfr_write(0xFD, 0x0A);
            ff_sfr_write(0xFE, 0x7F);
            msleep(1);
            ff_sfr_read(0xFE, &val);
            if (0x7F != val) {
                FF_LOGI("set io voltage abnormal,read:%d,retry:%d", val, i);
                continue;
            }
            FF_LOGD("set io voltage pass");
            break;
        }

        if (i >= set_iov_tries) {
            FF_LOGE("set io voltage fail");
        }

        /*set spi mode*/
        for (i = 0; i < set_spimode_tries; i++) {
            ff_sfr_write(0xC6, 0x01);
            msleep(4);
            ff_sfr_read(0xC6, &val);
            if (0x01 != val) {
                FF_LOGI("set spi mode abnormal,read:%d,retry:%d", val, i);
                continue;
            }
            FF_LOGD("set spi mode pass");
            break;
        }

        if (i >= set_spimode_tries) {
            FF_LOGE("set spi mode fail");
        }

        /*clear intr*/
        ff_sram_write((0x3500 / 2 + 0x04), 0xFFFF);

        /*read id*/
        ff_sram_read((0x3500 / 2 + 0x0B), &chip_id);
        FF_LOGI("read chip id:0x%x", chip_id);
        if ((chip_id & 0xFF00) == 0x9300) {
            FF_LOGI("probe id(0x%x) pass", chip_id);
            if (g_ff_ctx) g_ff_ctx->chip_id = chip_id;
            ret = 0;
            break;
        } else {
            FF_LOGI("probe id fail,read:0x%x,retry=%d", chip_id, read_id_retries);
            fts_power_sequence(0);
            ret = -EIO;
        }
    }
    FF_LOGV("'%s' leave.", __func__);
    return ret;
}

int ff_probe_id(void)
{
    int ret = 0;

    FF_LOGI("'%s' enter.", __func__);
    ret = ff_init_driver();
    if (ret < 0) {
        FF_LOGE("ff_init_driver fail, ret=%d", ret);
        goto err_init_driver;
    }

    ret = ff_chip_probe_id();
    if (ret < 0) {
        FF_LOGE("probe chip id fail, ret=%d", ret);
        goto err_init_driver;
    }

    ret = 0;
err_init_driver:
    ff_free_driver();
    FF_LOGI("'%s' leave.", __func__);
    return ret;
}
