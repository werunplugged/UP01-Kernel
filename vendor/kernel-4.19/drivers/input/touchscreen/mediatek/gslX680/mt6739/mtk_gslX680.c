/* drivers/input/touchscreen/mediatek/gslX680/
 *
 * 2010 - 2016 silead inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the sileadinc's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include <linux/bitops.h>
#include <linux/byteorder/generic.h>
#include <linux/delay.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "mtk_gslX680.h"
#include "tpd.h"

#include "mtk_boot_common.h"
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <uapi/linux/sched/types.h>

#if !defined(CONFIG_MTK_I2C_EXTENSION) || defined(GSLTP_ENABLE_I2C_DMA)
#include <linux/dma-mapping.h>
#endif
#include <linux/cust_include/cust_project_all_config.h>
#define GSLX680_NAME "gslX680"
#define GSLX680_ADDR 0x40
#define MAX_FINGERS 10
#define MAX_CONTACTS 10
#define DMA_TRANS_LEN 0x20
#define SMBUS_TRANS_LEN 0x01
#define GSL_PAGE_REG 0xf0
/*#define GREEN_MODE*/ /*IF use this, pls close esd check*/
#ifdef GREEN_MODE
#define MODE_ON 1
#define MODE_OFF 0
#endif

#define KEYCODE_CTPC	 100 //->58 ALT_RIGHT
#define KEYCODE_CTPE	 29 //->113 CTRL_LEFT
#define KEYCODE_CTPS	 97 //->114 CTRL_RIGHT
#define KEYCODE_CTPZ	 56  //->57 ALT_LEFT
#define KEYCODE_CTPW	 104 //->92 KEY_PAGE_UP
#define KEYCODE_CTPM	 105 //->21 KEY_DPAD_LEFT
#define KEYCODE_CTPO	 106 //->22 KEY_DPAD_RIGHT
#define KEYCODE_CTPUP    103 //->19 KEY_DPAD_UP
#define KEYCODE_CTPDOWM  108 //->20 KEY_DPAD_DOWN
#define KEYCODE_CTPLEFT  109 //->93  KEY_PAGEDOWN
#define KEYCODE_CTPRIGHT 110 //->124 INSERT

#ifndef GREEN_MODE
/*#define GSL_MONITOR*/ /*if enable ESD, please close GREEN_MODE*/
#endif

enum check_meun {
	power_status = 1,
	interrupt_status = 2,
	esd_scanning = 4
}; /* select check mode  1,2,4 */

enum check_err {
	power_shutdowned = 2,
	interrupt_fail,
	esd_protected
}; /* check mode err info  2,3,4 */

#define GSL_LATE_INIT_CHIP
#define TPD_PROC_DEBUG
/* #define ADD_I2C_DEVICE_ANDROID_4_0 */
/* #define HIGH_SPEED_I2C */


#ifdef GSL_GESTURE
typedef enum{
	GE_DISABLE = 0,
	GE_ENABLE = 1,
	GE_WAKEUP = 2,
	GE_NOWORK =3,
}GE_T;
static GE_T gsl_gesture_status = GE_DISABLE;
static volatile unsigned int gsl_gesture_flag = 1;
static char gsl_gesture_c = 0;
extern int gsl_obtain_gesture(void);
extern void gsl_FunIICRead(unsigned int (*fun) (unsigned int *,unsigned int,unsigned int));
extern void gsl_GestureExternInt(unsigned int *model,int len);
unsigned int gsl_model_extern[]={
	0x10,0x56,
	0x08000f00,0x0f0c2f1f,0x12125040,0x13127060,0x16149181,0x1a18b1a1,0x1c1bd2c2,0x201df2e2,
	0x3324f7fe,0x4f41e7ef,0x6d5ed8df,0x8a7bc8d0,0xa698b7c0,0xc3b4a6af,0xe0d2959d,0xffee848d,
	0x10,0x57,
	0x00062610,0x03015c41,0x06049277,0x0f09c8ad,0x2918f7e0,0x5142e4fb,0x685eb2cb,0x77707c97,
	0x857d9177,0x978dc5ab,0xb4a1f3de,0xdbcbd5ec,0xebe4a2bd,0xf4f06c87,0xfaf73651,0xfffd001b,
	
	0x10,0x49,
	0x0e00f4ff,0x2f1ee4ec,0x4f3ed4dc,0x6f5fc4cc,0x8f7fb3bc,0xae9ea3ab,0xcebe949b,0xf0df858d,
	0xf0ff707a,0xcfdf6268,0xadbe525a,0x8d9e434a,0x6d7d353c,0x4c5c262e,0x2c3c151e,0x0c3c000b,
	0x10,0x49,
	0x775df6f9,0xab91e6ef,0xdac3cedb,0xf9eda2b9,0xfdff6d88,0xf1f93a53,0xcce21424,0x9ab50209,
	0x65800101,0x354d1409,0x0f1f3a25,0x01056e53,0x0100a288,0x1407d3bc,0x4128f1e4,0x765bfffb,	

	0x10,0x1041,
	0xfdff859f,0xe0f2566c,0xbdcf2d41,0x90a90f1d,0x5a750106,0x253f0400,0x020d2b11,0x10015c46,
	0x3823806e,0x664e9f91,0x9a80b2ab,0xd0b5bab8,0xaac5bbbc,0x7590bebb,0x445bd3c4,0x244fffe5,
	0x10,0x1042,
	0xe5ff795e,0xb0cb4f54,0x7c96444a,0x4e63293a,0x223a0917,0x39271601,0x5c4a402c,0x7a6c6e55,
	0x837fa388,0x8084d7bd,0x5871fdef,0x293febfd,0x1019bcd5,0x040987a1,0x0101526d,0x11271e38,
	0x10,0x1044,
	0x86867995,0x8386415d,0x687a1026,0x324e0003,0x03151d07,0x04005539,0x240e836f,0x553ca293,
	0x8c70b5ac,0xc4a8bebb,0xfce0bfbf,0xcae6bfbf,0x91adbfbf,0x5975bdbe,0x253dccbf,0x0534ffe4,
	0x10,0x1041,
	0x0007775b,0x1004ae93,0x3520dac6,0x6b50f4e7,0xa487fdfe,0xdec1f3f9,0xfdf8c7e3,0xd7ee9dae,
	0xa4be818f,0x6f8a6672,0x3552595d,0x33185556,0x6d505555,0xa78a5053,0xddc43a48,0xdd05001d,
	0x10,0x1042,
	0x1a00d1d1,0x4f34d6d3,0x8369dfda,0xb89eebe4,0xecd2fef4,0xd5edecf9,0xb0bec6de,0x99a597af,
	0x8a8f637d,0x87882e49,0xa28c0214,0xd4bc0c01,0xece53b22,0xfcf56f55,0xfffea489,0xf91ed9be,
	0x10,0x1044,
	0x93958166,0x9a94b79c,0xb5a5e7d1,0xe6cbfff8,0xfefbd2ed,0xeef79eb8,0xcce07386,0x9fb75562,
	0x6b854049,0x35503539,0x02193331,0x381d3535,0x6f543736,0xa58a3938,0xdbc13239,0xf409001b,
	0x10,0x1045,
	0x6d6d1e00,0x6d6d5b3d,0x6e6d987a,0x7e73d4b7,0xab8ffef0,0xe4cae9fa,0xfcf3afcd,0xfdff7290,
	0xe2f03a55,0xb5ce0f21,0x79970104,0x3d5a0900,0x1324361c,0x02077153,0x0200ae90,0x1c2be9cc,
	0x10,0x1045,
	0x9999d9f8,0x97989ab9,0x90965b7a,0x7f8a1c3b,0x48680006,0x0d291704,0x00045535,0x07019375,
	0x2112ceb2,0x5638f5e6,0x9575fffc,0xd1b4f0fc,0xf5e7bbd9,0xfffc7c9b,0xf3fc3e5c,0xd407021f,

	0x10,0x47,        
	0xc9e00105,0x9ab11008,0x6f832c1c,0x525d533c,0x765f6c68,0xa68e5b64,0xd2bc4552,0xf8e62435,                  
	0xf2fa462e,0xdfea745d,0xc4d29e89,0xa4b5c5b3,0x8093e9d9,0x526bfef7,0x2139f2fc,0x002fcae0,	
	0x10,0x36,        
	0x18009fa5,0x4930969a,0x79619193,0xaa929190,0xdbc39893,0xfdf0baa3,0xeafae5d1,0xbfd6fbf4,        
	0x8ea6ffff,0x6477eaf9,0x5356bcd5,0x5f588ea5,0x756a6176,0x94843a4c,0xb9a61829,0xe5ed000a,  

	
	0x0a,0x36,
	0xebff0002,0xc1d60100,0x98ac0c06,0x75862116,0x53633b2e,0x35435949,0x1f297c6a,0x0c15a38f,
	0x0004ccb7,0x0e03f2e0,0x3621fffd,0x5b4aecf9,0x776acbdc,0x797ba1b6,0x566b8a90,0x2c61928b,
	0x0a,0x36,
	0x8f901700,0x8186452e,0x737b725c,0x536a9287,0x273da59c,0x0414c5b3,0x1905e9db,0x452efbf4,
	0x735cfffd,0xa28afaff,0xceb8ecf4,0xf7e4d5e2,0xf9ffa7be,0xcce3979b,0x9eb59495,0x6fa79593,
	0x0a,0x36,
	0xebff0900,0xc1d61e14,0x99ad372b,0x73875044,0x4c5f695c,0x2c398977,0x121eaf9b,0x0005d9c3,
	0x1d08f8ed,0x4b34fffd,0x7762f5fb,0xa38ee7ee,0xceb9d5de,0xdfe2b6ca,0xb3caa6aa,0x85bca5a5,	

	0x10,0x4f,
	0x5d76fefd,0x2d45e5f4,0x0b1abdd4,0x01058aa3,0x02005670,0x1b0b293d,0x4a320f19,0x7d630005,
	0xae960f05,0xd7c5311e,0xf4e85e46,0xfefc9278,0xf6fec4ac,0xcee4e8d7,0x9cb6fbf6,0x6882fefe,
	0x10,0x4f,
	0x795ffffc,0xaf94fdff,0xddc7dff0,0xf8eeafc9,0xfffd7a94,0xf8fe455f,0xd3e81c2f,0xa1bb0610,
	0x6b860600,0x3b521e0f,0x16264731,0x020a7a60,0x0000b095,0x1706e0cb,0x492efaf0,0x7f84fefe,

	0x10,0x00,
	0xf4ff0d00,0xe6ee2f1f,0xd8df5240,0xc8d17262,0xbac29483,0xacb3b6a4,0x9ea4d8c7,0x8f99f9ea,
	0x7883e7f6,0x666fc7d6,0x595fa4b5,0x49518495,0x383f6474,0x27304254,0x161e2132,0x012d0411,
	
	
};

#endif

#ifdef TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <linux/seq_file.h> /* lzk */
#include <linux/uaccess.h>
/* static struct proc_dir_entry *gsl_config_proc = NULL; */
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = {0};
static u8 gsl_proc_flag;
#endif

static int tpd_flag;
static int tpd_halt;
/*static char eint_flag;*/
static int touch_irq;
static struct i2c_client *i2c_client;
static struct task_struct *thread;

#ifdef GSL_LATE_INIT_CHIP
static struct delayed_work gsl_late_init_work;
static struct workqueue_struct *gsl_late_init_workqueue;
#define LATE_INIT_CYCLE_BY_REG_CHECK 10
#endif

#ifdef GSL_MONITOR
static struct delayed_work gsl_monitor_work;
static struct workqueue_struct *gsl_monitor_workqueue;
static u8 int_1st[4] = {0};
static u8 int_2nd[4] = {0};
static char b0_counter;
static char bc_counter;
/* i2c_lock_flag mean 0:do checking 1:skip once checking; 2:skip anyway*/
static char i2c_lock_flag;
#define MONITOR_CYCLE_NORMAL 100
#define MONITOR_CYCLE_IDLE 800
#define MONITOR_CYCLE_BY_REG_CHECK 1800
#endif

/* #define TPD_HAVE_BUTTON */
#define TPD_KEY_COUNT 4
#define TPD_KEYS                                                               \
	{                                                                      \
		KEY_MENU, KEY_HOMEPAGE, KEY_BACK, KEY_SEARCH                   \
	}
/* {button_center_x, button_center_y, button_width, button_height*/
#define TPD_KEYS_DIM                                                           \
	{                                                                      \
		{70, 2048, 60, 50}, {210, 2048, 60, 50}, {340, 2048, 60, 50},  \
		{                                                              \
			470, 2048, 60, 50                                      \
		}                                                              \
	}

static DECLARE_WAIT_QUEUE_HEAD(waiter);
static int init_chip(struct i2c_client *client);
/*static void green_mode(struct i2c_client *client, int mode);*/

#define GSLTP_REG_ADDR_LEN 1
#ifdef CONFIG_MTK_I2C_EXTENSION
/*for ARCH_MT6735,ARCH_MT6735M, ARCH_MT6753,ARCH_MT6580,ARCH_MT6755*/
#define GSLTP_ENABLE_WRRD_MODE
#ifdef GSLTP_ENABLE_I2C_DMA
#define GSLTP_DMA_MAX_TRANSACTION_LEN 255 /* for DMA mode */
#define GSLTP_DMA_MAX_WR_SIZE                                                  \
	(GSLTP_DMA_MAX_TRANSACTION_LEN - GSLTP_REG_ADDR_LEN)
#ifdef GSLTP_ENABLE_WRRD_MODE /*for WRRD(write and read) mode */
#define GSLTP_DMA_MAX_RD_SIZE 31
#else
#define GSLTP_DMA_MAX_RD_SIZE GSLTP_DMA_MAX_TRANSACTION_LEN
#endif
#endif
#else
#define GSLTP_DMA_MAX_TRANSACTION_LEN 255 /* for DMA mode */
#define GSLTP_DMA_MAX_RD_SIZE GSLTP_DMA_MAX_TRANSACTION_LEN
#define GSLTP_DMA_MAX_WR_SIZE                                                  \
	(GSLTP_DMA_MAX_TRANSACTION_LEN - GSLTP_REG_ADDR_LEN)
#endif

#ifdef CONFIG_MTK_I2C_EXTENSION
#define GSLTP_I2C_MASTER_CLOCK 100
#ifdef GSLTP_ENABLE_I2C_DMA
static u8 *g_dma_buff_va;
static u8 *g_dma_buff_pa;
#endif
#else
static u8 *g_i2c_buff;
static u8 *g_i2c_addr;
#endif
#if !defined(CONFIG_MTK_I2C_EXTENSION) || defined(GSLTP_ENABLE_I2C_DMA)
static int msg_dma_alloc(void);
static void msg_dma_release(void);
#endif

#define GSL_DEBUG (1)
#if GSL_DEBUG
#define GSL_LOGD(fmt, args...)                                                 \
	pr_debug(GSLX680_NAME "<-dbg-> [%04d] [@%s]" fmt, __LINE__, __func__,  \
		 ##args)
#define GSL_LOGF()                                                             \
	pr_debug(GSLX680_NAME "<-func-> [%04d] [@%s] is call!\n", __LINE__,    \
		 __func__)

#else
#define GSL_LOGD(fmt, args...)                                                 \
	do {                                                                   \
	} while (0)
#define GSL_LOGF()                                                             \
	do {                                                                   \
	} while (0)
#endif /* end #if GSL_DEBUG */

#define GSL_LOGE(fmt, args...)                                                 \
	pr_debug(GSLX680_NAME "<-err->[%04d] [@%s]" fmt, __LINE__, __func__,   \
		 ##args)

#ifdef TPD_HAVE_BUTTON
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT] = TPD_WARP_END;
#endif
#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
static int tpd_calmat_local[8] = TPD_CALIBRATION_MATRIX;
static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX;
#endif

#ifdef CONFIG_MTK_I2C_EXTENSION
#ifdef GSLTP_ENABLE_I2C_DMA
static int msg_dma_alloc(void)
{
	g_dma_buff_va = (u8 *)dma_alloc_coherent(
		NULL, GSLTP_DMA_MAX_TRANSACTION_LEN,
		(dma_addr_t *)(&g_dma_buff_pa), GFP_KERNEL | GFP_DMA);
	if (!g_dma_buff_va) {
		GSL_LOGE("[DMA][Error] Allocate DMA I2C Buffer failed!\n");
		return -1;
	}
	return 0;
}

static void msg_dma_release(void)
{
	if (g_dma_buff_va) {
		dma_free_coherent(NULL, GSLTP_DMA_MAX_TRANSACTION_LEN,
				  g_dma_buff_va, (dma_addr_t)g_dma_buff_pa);
		g_dma_buff_va = NULL;
		g_dma_buff_pa = NULL;
		GSL_LOGD("[DMA][release]I2C Buffer release!\n");
	}
}

#ifdef GSLTP_ENABLE_WRRD_MODE
/*WRRD(write and read) mode, no stop condition after write reg addr*/
/*max DMA read len  31  bytes */
static s32 i2c_dma_read(struct i2c_client *client, u8 addr, u8 *rxbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg;

	if (rxbuf == NULL)
		return -1;
	memset(&msg, 0, sizeof(struct i2c_msg));

	*g_dma_buff_va = addr;
	msg.addr = client->addr & I2C_MASK_FLAG;
	msg.flags = 0;
	msg.len = (len << 8) | GSLTP_REG_ADDR_LEN;
	msg.buf = g_dma_buff_pa;
	msg.ext_flag = client->ext_flag | I2C_ENEXT_FLAG | I2C_WR_FLAG |
		       I2C_RS_FLAG | I2C_DMA_FLAG;
	msg.timing = GSLTP_I2C_MASTER_CLOCK;

	/* GSL_LOGD("dma i2c read: 0x%04X, %d bytes(s)", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
			continue;
		memcpy(rxbuf, g_dma_buff_va, len);
		return 0;
	}
	GSL_LOGE("Dma I2C Read Error: 0x%04X, %d byte(s), err-code: %d", addr,
		 len, ret);
	return ret;
}
#else
/*read only mode, max read length is 65532bytes*/
static s32 i2c_dma_read(struct i2c_client *client, u8 addr, u8 *rxbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg[2];

	if (rxbuf == NULL)
		return -1;
	memset(&msg, 0, sizeof(struct i2c_msg));

	*g_dma_buff_va = addr;
	msg[0].addr = client->addr & I2C_MASK_FLAG;
	msg[0].flags = 0;
	msg[0].len = GSLTP_REG_ADDR_LEN;
	msg[0].buf = g_dma_buff_pa;
	msg[0].ext_flag = I2C_DMA_FLAG;
	msg[0].timing = GSLTP_I2C_MASTER_CLOCK;

	msg[1].addr = client->addr & I2C_MASK_FLAG;
	msg[1].flags = I2C_M_RD;
	msg[1].len = GSLTP_DMA_MAX_RD_SIZE;
	msg[1].buf = g_dma_buff_pa;
	msg[1].ext_flag = client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG;
	msg[1].timing = GSLTP_I2C_MASTER_CLOCK;

	/* GSL_LOGD("dma i2c read: 0x%04X, %d bytes(s)", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0)
			continue;
		memcpy(rxbuf, g_dma_buff_va, len);
		return 0;
	}
	GSL_LOGE("Dma I2C Read Error: 0x%04X, %d byte(s), err-code: %d", addr,
		 len, ret);
	return ret;
}
#endif
static s32 i2c_dma_write(struct i2c_client *client, u8 addr, u8 *txbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg;

	if (txbuf == NULL)
		return -1;

	memset(&msg, 0, sizeof(struct i2c_msg));
	*g_dma_buff_va = addr;

	msg.addr = (client->addr & I2C_MASK_FLAG);
	msg.flags = 0;
	msg.buf = g_dma_buff_pa;
	msg.len = 1 + len;
	msg.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG);
	msg.timing = GSLTP_I2C_MASTER_CLOCK;

	/* GSL_LOGD("dma i2c write: 0x%04X, %d bytes(s)", addr, len); */
	memcpy(g_dma_buff_va + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
			continue;
		return 0;
	}
	GSL_LOGE("Dma I2C Write Error: 0x%04X, %d bytes, err-code: %d\n", addr,
		 len, ret);
	return ret;
}

#else /*GSLTP_ENABLE_I2C_DMA*/
static s32 i2c_read_nondma(struct i2c_client *client, u8 addr, u8 *rxbuf,
			   int len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg;

	if (rxbuf == NULL)
		return -1;
	memset(&msg, 0, sizeof(struct i2c_msg));

	rxbuf[0] = addr;
	msg.addr = client->addr & I2C_MASK_FLAG;
	msg.flags = 0;
	msg.len = (len << 8) | GSLTP_REG_ADDR_LEN;
	msg.buf = rxbuf;
	msg.ext_flag = I2C_WR_FLAG | I2C_RS_FLAG;
	msg.timing = GSLTP_I2C_MASTER_CLOCK;

	/* GSL_LOGD("dma i2c read: 0x%04X, %d bytes(s)", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
			continue;
		return 0;
	}
	GSL_LOGE("Dma I2C Read Error: 0x%4X, %d bytes, err-code: %d\n", addr,
		 len, ret);
	return ret;
}

static s32 i2c_write_nondma(struct i2c_client *client, u8 addr, u8 *txbuf,
			    int len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg;
	u8 wrBuf[C_I2C_FIFO_SIZE + 1];

	if (txbuf == NULL)
		return -1;

	memset(&msg, 0, sizeof(struct i2c_msg));
	memset(wrBuf, 0, C_I2C_FIFO_SIZE + 1);
	wrBuf[0] = addr;
	memcpy(wrBuf + 1, txbuf, len);

	msg.flags = 0;
	msg.buf = wrBuf;
	msg.len = 1 + len;
	msg.addr = (client->addr & I2C_MASK_FLAG);
	msg.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG);
	msg.timing = GSLTP_I2C_MASTER_CLOCK;

	/* GSL_LOGD("dma i2c write: 0x%04X, %d bytes(s)", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
			continue;
		return 0;
	}
	GSL_LOGE("Dma I2C Write Error: 0x%04X, %d bytes, err-code: %d\n", addr,
		 len, ret);
	return ret;
}
#endif
#else /*CONFIG_MTK_I2C_EXTENSION*/
static int msg_dma_alloc(void)
{
	g_i2c_buff = kzalloc(GSLTP_DMA_MAX_TRANSACTION_LEN, GFP_KERNEL);
	if (!g_i2c_buff) {
		GSL_LOGE("[DMA][Error] Allocate DMA I2C Buffer failed!\n");
		return -1;
	}

	g_i2c_addr = kzalloc(GSLTP_REG_ADDR_LEN, GFP_KERNEL);
	if (!g_i2c_addr) {
		GSL_LOGE("[DMA]Allocate DMA I2C addr buf failed!\n");
		kfree(g_i2c_buff);
		g_i2c_buff = NULL;
		return -1;
	}

	return 0;
}

static void msg_dma_release(void)
{
	kfree(g_i2c_buff);
	g_i2c_buff = NULL;

	kfree(g_i2c_addr);
	g_i2c_addr = NULL;

	GSL_LOGD("[DMA][release]I2C Buffer release!\n");
}
static s32 i2c_dma_read(struct i2c_client *client, u8 addr, u8 *rxbuf, int len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg[2];

	if (rxbuf == NULL)
		return -1;

	memset(&msg, 0, 2 * sizeof(struct i2c_msg));
	memcpy(g_i2c_addr, &addr, GSLTP_REG_ADDR_LEN);
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = g_i2c_addr;
	msg[0].len = 1;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = g_i2c_buff;
	msg[1].len = len;

	/* GSL_LOGD("dma i2c read: 0x%04X, %d bytes(s)", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0)
			continue;
		memcpy(rxbuf, g_i2c_buff, len);
		return 0;
	}
	GSL_LOGE("Dma I2C Read Error: 0x%4X, %d bytes, err-code: %d\n", addr,
		 len, ret);
	return ret;
}

static s32 i2c_dma_write(struct i2c_client *client, u8 addr, u8 *txbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	struct i2c_msg msg;

	if (txbuf == NULL)
		return -1;

	memset(&msg, 0, sizeof(struct i2c_msg));
	*g_i2c_buff = addr;

	msg.addr = (client->addr);
	msg.flags = 0;
	msg.buf = g_i2c_buff;
	msg.len = 1 + len;

	/* GSL_LOGD("dma i2c write: 0x%04X, %d bytes(s)", addr, len); */
	memcpy(g_i2c_buff + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
			continue;
		return 0;
	}
	GSL_LOGE("Dma I2C Write Error: 0x%04X, %d bytes, err-code: %d\n", addr,
		 len, ret);
	return ret;
}
#endif

static int gsl_i2c_read_bytes(struct i2c_client *client, u8 addr, u8 *rxbuf,
			      int len)
{
	int left = len;
	int readLen = 0;
	u8 *rd_buf = rxbuf;
	int ret = 0;

	/* GSL_LOGD("Read bytes dma: 0x%04X, %d byte(s)", addr, len); */
	while (left > 0) {
#if !defined(CONFIG_MTK_I2C_EXTENSION) || defined(GSLTP_ENABLE_I2C_DMA)
		readLen = left > GSLTP_DMA_MAX_RD_SIZE ? GSLTP_DMA_MAX_RD_SIZE
						       : left;
		ret = i2c_dma_read(client, addr, rd_buf, readLen);
#else
		readLen = left > C_I2C_FIFO_SIZE ? C_I2C_FIFO_SIZE : left;
		ret = i2c_read_nondma(client, addr, rd_buf, readLen);
#endif

		if (ret < 0) {
			GSL_LOGE("dma read failed!\n");
			return -1;
		}

		left -= readLen;
		if (left > 0) {
			addr += readLen;
			rd_buf += readLen;
		}
	}
	return 0;
}

static s32 gsl_i2c_write_bytes(struct i2c_client *client, u8 addr, u8 *txbuf,
			       int len)
{

	int ret = 0;
	int write_len = 0;
	int left = len;
	u8 *wr_buf = txbuf;
	u8 offset = 0;
	u8 wrAddr = addr;

	/* GSL_LOGD("Write bytes dma: 0x%04X, %d byte(s)", addr, len); */
	while (left > 0) {
#if !defined(CONFIG_MTK_I2C_EXTENSION) || defined(GSLTP_ENABLE_I2C_DMA)
		write_len = left > GSLTP_DMA_MAX_WR_SIZE ? GSLTP_DMA_MAX_WR_SIZE
							 : left;
		ret = i2c_dma_write(client, wrAddr, wr_buf, write_len);
#else
		write_len = left > C_I2C_FIFO_SIZE ? C_I2C_FIFO_SIZE : left;
		ret = i2c_write_nondma(client, wrAddr, wr_buf, write_len);
#endif

		if (ret < 0) {
			GSL_LOGE("dma i2c write failed!\n");
			return -1;
		}
		offset += write_len;
		left -= write_len;
		if (left > 0) {
			wrAddr = addr + offset;
			wr_buf = txbuf + offset;
		}
	}
	return 0;
}

#ifdef GSL_GESTURE
static unsigned int gsl_read_oneframe_data(unsigned int *data,
				unsigned int addr,unsigned int len)

{
	u8 buf[4];
	int i;
	GSL_LOGD("tp-gsl-gesture %s\n",__func__);
	GSL_LOGD("gsl_read_oneframe_data:::addr=%x,len=%x\n",addr,len);
	for(i=0;i<len/2;i++){
		buf[0] = ((addr+i*8)/0x80)&0xff;
		buf[1] = (((addr+i*8)/0x80)>>8)&0xff;
		buf[2] = (((addr+i*8)/0x80)>>16)&0xff;
		buf[3] = (((addr+i*8)/0x80)>>24)&0xff;
		gsl_i2c_write_bytes(i2c_client,0xf0,buf,4);
		gsl_i2c_read_bytes(i2c_client,(addr+i*8)%0x80,(char *)&data[i*2],8);
		gsl_i2c_read_bytes(i2c_client,(addr+i*8)%0x80,(char *)&data[i*2],8);


	}
	if(len%2){
		buf[0] = ((addr+len*4 - 4)/0x80)&0xff;
		buf[1] = (((addr+len*4 - 4)/0x80)>>8)&0xff;
		buf[2] = (((addr+len*4 - 4)/0x80)>>16)&0xff;
		buf[3] = (((addr+len*4 - 4)/0x80)>>24)&0xff;
		gsl_i2c_write_bytes(i2c_client,0xf0,buf,4);
		gsl_i2c_read_bytes(i2c_client,(addr+len*4 - 4)%0x80,(char *)&data[len-1],4);
		gsl_i2c_read_bytes(i2c_client,(addr+len*4 - 4)%0x80,(char *)&data[len-1],4);

	}
	#if 1
	for(i=0;i<len;i++){
	GSL_LOGD("gsl_read_oneframe_data =%x\n",data[i]);
	}
	#endif
	
	return len;
}

#endif


static void startup_chip(struct i2c_client *client)
{
	u8 write_buf = 0x00;

	gsl_i2c_write_bytes(client, 0xe0, &write_buf, 1);
#ifdef GSL_NOID_VERSION
	gsl_DataInit(gsl_config_data_id);
#endif

	usleep_range(10000, 11000);
}

#ifdef GSL9XX_CHIP
static void gsl_io_control(struct i2c_client *client)
{
	u8 buf[4] = {0};
	int i;

	for (i = 0; i < 5; i++) {
		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0xfe;
		buf[3] = 0x1;
		gsl_i2c_write_bytes(client, 0xf0, buf, 4);
		buf[0] = 0x5;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0x80;
		gsl_i2c_write_bytes(client, 0x78, buf, 4);
		usleep_range(5000, 5100);
	}
	msleep(50);
}
#endif

static int reset_chip(struct i2c_client *client)
{
	u8 write_buf[4] = {0};
	int ret = 0;

	write_buf[0] = 0x88;
	ret = gsl_i2c_write_bytes(client, 0xe0, &write_buf[0], 1);
	msleep(20);

	write_buf[0] = 0x04;
	ret += gsl_i2c_write_bytes(client, 0xe4, &write_buf[0], 1);
	usleep_range(10000, 11000);

	write_buf[0] = 0x00;
	write_buf[1] = 0x00;
	write_buf[2] = 0x00;
	write_buf[3] = 0x00;
	ret += gsl_i2c_write_bytes(client, 0xbc, write_buf, 4);
	usleep_range(10000, 11000);
#ifdef GSL9XX_CHIP
	gsl_io_control(client);
#endif

	if (ret < 0)
		GSL_LOGE("reset chip fail!\n");

	return ret;
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4] = {0};

	write_buf[0] = 0x88;
	gsl_i2c_write_bytes(client, 0xe0, &write_buf[0], 1);
	msleep(20);

	write_buf[0] = 0x03;
	gsl_i2c_write_bytes(client, 0x80, &write_buf[0], 1);
	usleep_range(5000, 5100);

	write_buf[0] = 0x04;
	gsl_i2c_write_bytes(client, 0xe4, &write_buf[0], 1);
	usleep_range(5000, 5100);

	write_buf[0] = 0x00;
	gsl_i2c_write_bytes(client, 0xe0, &write_buf[0], 1);
	msleep(20);
}

#ifdef HIGH_SPEED_I2C
static u32 gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf,
			      u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;
	xfer_msg[0].timing = 400;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;
	xfer_msg[1].timing = 400;

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		usleep_range(5000, 5100);
	}

	return i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg)) ==
			       ARRAY_SIZE(xfer_msg)
		       ? 0
		       : -EFAULT;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf,
			       u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;
	xfer_msg[0].timing = 400;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static inline void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN * 4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	struct fw_data *ptr_fw;

	GSL_LOGD("===gsl load_fw start===\n");

	ptr_fw = GSLX680_FW;
	source_len = ARRAY_SIZE(GSLX680_FW);
	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		if (ptr_fw[source_line].offset == GSL_PAGE_REG) {
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		} else {
			if (send_flag ==
			    1 % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
				buf[0] = (u8)ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (send_flag ==
			    0 % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) {
				gsl_write_interface(client, buf[0], buf,
						    cur - buf - 1);
				cur = buf + 1;
			}

			send_flag++;
		}
	}

	GSL_LOGD("===gsl load_fw end===\n");
}
#else
static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[SMBUS_TRANS_LEN * 4] = {0};
	u8 reg = 0, send_flag = 1, cur = 0;

	unsigned int source_line = 0;
	unsigned int source_len = ARRAY_SIZE(GSLX680_FW);

	GSL_LOGD("===gsl load_fw start===\n");
	for (source_line = 0; source_line < source_len; source_line++) {
		if (1 == SMBUS_TRANS_LEN) {
			reg = GSLX680_FW[source_line].offset;
			memcpy(&buf[0], &GSLX680_FW[source_line].val, 4);
			gsl_i2c_write_bytes(client, reg, buf, 4);
		} else {
			/* init page trans, set the page val */
			if (GSLX680_FW[source_line].offset == GSL_PAGE_REG) {
				buf[0] = (u8)(GSLX680_FW[source_line].val &
					      0x000000ff);
				gsl_i2c_write_bytes(client, GSL_PAGE_REG,
						    &buf[0], 1);
				send_flag = 1;
			} else {
				if (send_flag ==
				    1 % (SMBUS_TRANS_LEN < 0x08
						 ? SMBUS_TRANS_LEN
						 : 0x08))
					reg = GSLX680_FW[source_line].offset;

				memcpy(&buf[cur], &GSLX680_FW[source_line].val,
				       4);
				cur += 4;

				if (send_flag ==
				    0 % (SMBUS_TRANS_LEN < 0x08
						 ? SMBUS_TRANS_LEN
						 : 0x08)) {
					gsl_i2c_write_bytes(client, reg, buf,
							    SMBUS_TRANS_LEN *
								    4);
					cur = 0;
				}

				send_flag++;
			}
		}
	}

	GSL_LOGD("===gsl load_fw end===\n");
}
#endif

/*   ----------------------check_memdata start-------------*/
static int arry_compare(u8 *arry_1st, u8 *arry_2nd, int num)
{
	int i;
	int result = 0;

	for (i = 0; i < num; i++) {
		if (*(arry_1st + i) != *(arry_2nd + i))
			result++;
	}
	return result;
}

static int arry_copy(u8 *arry_new, u8 *arry_old, int num)
{
	int i;

	for (i = 0; i < num; i++)
		*(arry_new + i) = *(arry_old + i);

	return 0;
}

static int power_check(struct i2c_client *client)
{
	int result = 0;
	u8 read_buf[4] = {0x00};

	gsl_i2c_read_bytes(client, 0xb0, read_buf, sizeof(read_buf));
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a ||
	    read_buf[0] != 0x5a)
		result = power_shutdowned;

	return result;
}

static int interrupt_check(struct i2c_client *client)
{
	int i, num;
	int result = 0;
	u8 read_buf[4] = {0x00};
	u8 arry_1st[4] = {0x00};
	u8 arry_2nd[4] = {0x00};

	num = sizeof(read_buf);
	for (i = 0; i < (num * num); i++) {
		gsl_i2c_read_bytes(client, 0xb4, read_buf, num);
		usleep_range(10000, 11000);
		if (!(i % num))
			arry_copy(arry_1st, read_buf, num);
		else
			arry_copy(arry_2nd, read_buf, num);
	}
	result = arry_compare(arry_1st, arry_2nd, num);
	if (result)
		result = interrupt_status;
	return result;
}

static int esd_check(struct i2c_client *client)
{
	int result = 0;
	u8 read_buf[4] = {0x00};

	gsl_i2c_read_bytes(client, 0xbc, read_buf, sizeof(read_buf));
	if (read_buf[3] != 0x00 || read_buf[2] != 0x00 || read_buf[1] != 0x00 ||
	    read_buf[0] != 0x00)
		result = esd_protected;
	return result;
}

static int check_mode(struct i2c_client *client, int mode_set)
{
	int result = 0;

	switch (mode_set) {
	case power_status:
		result = power_check(client);
		break;
	case interrupt_status:
		result = interrupt_check(client);
		break;
	case esd_scanning:
		result = esd_check(client);
		break;
	case (power_status + interrupt_status):
		result = power_check(client);
		result += interrupt_check(client);
		break;
	case (power_status + esd_scanning):
		result = power_check(client);
		result += esd_check(client);
		break;
	case (interrupt_status + esd_scanning):
		result = interrupt_check(client);
		result += esd_check(client);
		break;
	case (power_status + interrupt_status + esd_scanning):
		result = power_check(client);
		result += interrupt_check(client);
		result += esd_check(client);
		break;
	default:
		result = mode_set;
		GSL_LOGE("mode_set[%d] not valid!\n", mode_set);
	}
	return result;
}

static int check_mem_data(struct i2c_client *client)
{
	int result = 0;

	result = check_mode(client, power_status);
	GSL_LOGD("---result num is[%d] ", result);
	GSL_LOGD("power_shutdowned[%d]\n", power_shutdowned);
	if (result)
		result = init_chip(client);

	return result;
}
/*   ----------------------check_memdata end-------------*/

static int test_i2c(struct i2c_client *client)
{
	u8 read_buf[4] = {0x00};
	u8 write_buf[4] = {0x00, 0x03, 0x02, 0x01};
	int result = 0;

	result = gsl_i2c_read_bytes(client, 0xf0, read_buf, sizeof(read_buf));
	GSL_LOGD("gslX680 I read reg 0xf0 is %02x%02x%02x\n", read_buf[2],
		 read_buf[1], read_buf[0]);

	usleep_range(2000, 2100);
	result +=
		gsl_i2c_write_bytes(client, 0xf0, write_buf, sizeof(write_buf));

	GSL_LOGD("gslX680 I write reg 0xf0 is %02x%02x%02x\n", write_buf[2],
		 write_buf[1], write_buf[0]);

	usleep_range(2000, 2100);
	result += gsl_i2c_read_bytes(client, 0xf0, read_buf, sizeof(read_buf));
	GSL_LOGD("gslX680 I read reg 0xf0 is %02x%02x%02x\n", read_buf[2],
		 read_buf[1], read_buf[0]);

	if (arry_compare(write_buf, read_buf, 3))
		result--;
	return result;
}
static int init_chip(struct i2c_client *client)
{
	int rc;

#ifdef GSL_MONITOR
	i2c_lock_flag = 2;
#endif
	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(20);
	tpd_gpio_output(GTP_RST_PORT, 1);
	msleep(20);

	rc = test_i2c(client);
	if (rc < 0) {
		GSL_LOGE("------gslX680 test_i2c error------\n");
		return -1;
	}
	clr_reg(client);
	reset_chip(client);
	clr_reg(client);
	rc = reset_chip(client);
	gsl_load_fw(client);
	startup_chip(client);
	rc += reset_chip(client);
	startup_chip(client);
#ifdef GSL_MONITOR
	i2c_lock_flag = 0;
#endif
	return rc;
}

#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	else
		return (ch - 'a' + 10);
}


static int gsl_config_read_proc(struct seq_file *m, void *v)
{
	char temp_data[5] = {0};
	unsigned int tmp = 0;

	if ('v' == gsl_read[0] && 's' == gsl_read[1]) {
#ifdef GSL_NOID_VERSION
		tmp = gsl_version_id();
#else
		tmp = 0x20121215;
#endif
		seq_printf(m, "version:%x\n", tmp);
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) {
		if ('i' == gsl_read[3]) {
#ifdef GSL_NOID_VERSION
			tmp = (gsl_data_proc[5] << 8) | gsl_data_proc[4];
			seq_printf(m, "gsl_config_data_id[%d] = ", tmp);
			if (tmp >= 0 && tmp < ARRAY_SIZE(gsl_config_data_id))
				seq_printf(m, "%d\n", gsl_config_data_id[tmp]);
#endif
		} else {
			gsl_i2c_write_bytes(i2c_client, 0Xf0, &gsl_data_proc[4],
					    4);
			if (gsl_data_proc[0] < 0x80)
				gsl_i2c_read_bytes(i2c_client, gsl_data_proc[0],
						   temp_data, 4);
			gsl_i2c_read_bytes(i2c_client, gsl_data_proc[0],
					   temp_data, 4);
			seq_printf(m, "offset : {0x%02x,0x", gsl_data_proc[0]);
			seq_printf(m, "%02x", temp_data[3]);
			seq_printf(m, "%02x", temp_data[2]);
			seq_printf(m, "%02x", temp_data[1]);
			seq_printf(m, "%02x};\n", temp_data[0]);
		}
	}
	/* *eof = 1; */
	return 0;
}

static ssize_t gsl_config_write_proc(struct file *file,
				     const char __user *buffer, size_t count,
				     loff_t *data)
{
	u8 buf[8] = {0};
	char temp_buf[CONFIG_LEN];
	char *path_buf;
#ifdef GSL_NOID_VERSION
	int tmp = 0;
	int tmp1 = 0;
#endif

	GSL_LOGD("[tp-gsl]\n");
	if (count > 512) {
		GSL_LOGE("size not match [%d:%d]\n", CONFIG_LEN, (int)count);
		return -EFAULT;
	}
	path_buf = kzalloc(count, GFP_KERNEL);
	if (!path_buf) {
		GSL_LOGE("alloc path_buf memory error\n");
		return -1;
	}
	if (copy_from_user(path_buf, buffer, count)) {
		GSL_LOGE("copy from user fail\n");
		goto exit_write_proc_out;
	}
	memcpy(temp_buf, path_buf, (count < CONFIG_LEN ? count : CONFIG_LEN));
	GSL_LOGD("[tp-gsl][%s][%s]\n", __func__, temp_buf);

	buf[3] = char_to_int(temp_buf[14]) << 4 | char_to_int(temp_buf[15]);
	buf[2] = char_to_int(temp_buf[16]) << 4 | char_to_int(temp_buf[17]);
	buf[1] = char_to_int(temp_buf[18]) << 4 | char_to_int(temp_buf[19]);
	buf[0] = char_to_int(temp_buf[20]) << 4 | char_to_int(temp_buf[21]);

	buf[7] = char_to_int(temp_buf[5]) << 4 | char_to_int(temp_buf[6]);
	buf[6] = char_to_int(temp_buf[7]) << 4 | char_to_int(temp_buf[8]);
	buf[5] = char_to_int(temp_buf[9]) << 4 | char_to_int(temp_buf[10]);
	buf[4] = char_to_int(temp_buf[11]) << 4 | char_to_int(temp_buf[12]);
	if ('v' == temp_buf[0] && 's' == temp_buf[1]) {
		memcpy(gsl_read, temp_buf, 4);
		GSL_LOGD("gsl version\n");
	} else if ('s' == temp_buf[0] && 't' == temp_buf[1]) {
#ifdef GSL_MONITOR
		cancel_delayed_work_sync(&gsl_monitor_work);
		i2c_lock_flag = 2;
#endif
		gsl_proc_flag = 1;
		reset_chip(i2c_client);
	} else if ('e' == temp_buf[0] && 'n' == temp_buf[1]) {
		msleep(20);
		reset_chip(i2c_client);
		startup_chip(i2c_client);
		gsl_proc_flag = 0;
	} else if ('r' == temp_buf[0] && 'e' == temp_buf[1]) {
		memcpy(gsl_read, temp_buf, 4);
		memcpy(gsl_data_proc, buf, 8);
	} else if ('w' == temp_buf[0] && 'r' == temp_buf[1]) {
		gsl_i2c_write_bytes(i2c_client, buf[4], buf, 4);
	}
#ifdef GSL_NOID_VERSION
	else if ('i' == temp_buf[0] && 'd' == temp_buf[1]) {
		tmp1 = (buf[7] << 24) | (buf[6] << 16) | (buf[5] << 8) | buf[4];
		tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		if (tmp1 >= 0 && tmp1 < ARRAY_SIZE(gsl_config_data_id))
			gsl_config_data_id[tmp1] = tmp;
	}
#endif
exit_write_proc_out:
	kfree(path_buf);
	return count;
}
static int gsl_server_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, gsl_config_read_proc, NULL);
}
static const struct file_operations gsl_seq_fops = {
	.open = gsl_server_list_open,
	.read = seq_read,
	.release = single_release,
	.write = gsl_config_write_proc,
	.owner = THIS_MODULE,
};
#endif

#ifdef TPD_ROTATION_SUPPORT
static void tpd_swap_xy(int *x, int *y)
{
	int temp = 0;

	temp = *x;
	*x = *y;
	*y = temp;
}

static void tpd_rotate_90(int *x, int *y)
{
	*x = SCREEN_MAX_X + 1 - *x;

	*x = (*x * SCREEN_MAX_Y) / SCREEN_MAX_X;
	*y = (*y * SCREEN_MAX_X) / SCREEN_MAX_Y;

	tpd_swap_xy(x, y);
}
static void tpd_rotate_180(int *x, int *y)
{
	*y = SCREEN_MAX_Y + 1 - *y;
	*x = SCREEN_MAX_X + 1 - *x;
}
static void tpd_rotate_270(int *x, int *y)
{
	*y = SCREEN_MAX_Y + 1 - *y;

	*x = (*x * SCREEN_MAX_Y) / SCREEN_MAX_X;
	*y = (*y * SCREEN_MAX_X) / SCREEN_MAX_Y;

	tpd_swap_xy(x, y);
}
#endif

u8 rs_value1;
static void tpd_down(int id, int x, int y, int p)
{
	GSL_LOGD("----tpd_down id: %d, x:%d, y:%d----\n", id, x, y);

#ifdef TPD_ROTATION_SUPPORT
	switch (tpd_rotation_type) {
	case TPD_ROTATION_90:
		tpd_rotate_90(&x, &y);
		break;
	case TPD_ROTATION_270:
		tpd_rotate_270(&x, &y);
		break;
	case TPD_ROTATION_180:
		tpd_rotate_180(&x, &y);
		break;
	default:
		break;
	}
#endif

	input_report_key(tpd->dev, BTN_TOUCH, 1);
	input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
	input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
	input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(tpd->dev);
}

static void tpd_up(void)
{
	GSL_LOGD("------tpd_up------\n");

	input_report_key(tpd->dev, BTN_TOUCH, 0);
	input_mt_sync(tpd->dev);
}


#ifdef  GSL_GESTURE
static void gsl_enter_doze(struct i2c_client *client)
{
		int i;
		u8 buf[4] = {0};
#if 0
		u32 tmp;
		gsl_reset_core(ts->client);
		temp = ARRAY_SIZE(GSLX68X_FW_GESTURE);
		gsl_load_fw(ts->client,GSLX68X_FW_GESTURE,temp);
		gsl_start_core(ts->client);
		msleep(1000);		
#endif
		for(i = 0;i < 5; i++){
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		gsl_i2c_write_bytes(client,0xf0,buf,4);
		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0x1;
		buf[3] = 0x5a;
		gsl_i2c_write_bytes(client,0x8,buf,4);
		msleep(20);
	}

		gsl_gesture_status = GE_ENABLE;
	
}

static void gsl_quit_doze(struct i2c_client *client)
{
		int i;
		u8 buf[4] = {0};

   GSL_LOGD("gsl_quit_doze () : GSL_GESTURE is OK\n");	
		gsl_gesture_status = GE_DISABLE;
	
		tpd_gpio_output(GTP_RST_PORT, 0);	
		msleep(20);
		tpd_gpio_output(GTP_RST_PORT, 1);
		msleep(20);
	
		for(i = 0 ;i < 5;i++){
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		gsl_i2c_write_bytes(client,0xf0,buf,4);
		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0x5a;
		gsl_i2c_write_bytes(client,0x8,buf,4);
		msleep(10);
	}
	
#if 0
		gsl_reset_core(ddata->client);
		temp = ARRAY_SIZE(GSLX68X_FW_CONFIG);

		gsl_load_fw(ddata->client,GSLX68X_FW_CONFIG,temp);
		gsl_start_core(ddata->client);
#endif
	
	}

#endif


static void gsl_report_point(struct gsl_touch_info *ti)
{
	int tmp = 0;
	static int gsl_up_flag; /*prevent more up event*/

//	GSL_LOGD("gsl report_point %d\n", ti->finger_num);
	if (unlikely(ti->finger_num == 0)) {
		if (gsl_up_flag == 0)
			return;
		gsl_up_flag = 0;
		tpd_up();

		if ((get_boot_mode() == FACTORY_BOOT) ||
		    (get_boot_mode() == RECOVERY_BOOT))
			tpd_button(ti->x[tmp], ti->y[tmp], 0);

	} else {
		gsl_up_flag = 1;
		for (tmp = 0; ti->finger_num > tmp; tmp++) {


			tpd_down(ti->id[tmp] - 1, ti->x[tmp], ti->y[tmp], 0);
			if ((get_boot_mode() == FACTORY_BOOT) ||
			    (get_boot_mode() == RECOVERY_BOOT))
				tpd_button(ti->x[tmp], ti->y[tmp], 1);
		}
	}
	input_sync(tpd->dev);
}

static void report_data_handle(void)
{
	u8 touch_data[44] = {0};
	unsigned char point_num = 0;
	unsigned int temp_a, temp_b, i;

#ifdef GSL_NOID_VERSION
	u8 buf[4] = {0};
	struct gsl_touch_info cinfo = {{0} };
	int tmp1 = 0;
#endif

#ifdef GSL_GESTURE
	static struct timeval time_now;

	static struct timeval time_last={0,0};
#endif


#ifdef GSL_MONITOR
	if (i2c_lock_flag != 0)
		return;

	i2c_lock_flag = 1;
#endif

#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		return;
#endif

	gsl_i2c_read_bytes(i2c_client, 0x80, &touch_data[0], 4);
	point_num = touch_data[0];
	if (point_num > 0)
		gsl_i2c_read_bytes(i2c_client, 0x84, &touch_data[4], 4);
	if (point_num > 1)
		gsl_i2c_read_bytes(i2c_client, 0x88, &touch_data[8], 4);
	if (point_num > 2)
		gsl_i2c_read_bytes(i2c_client, 0x8c, &touch_data[12], 4);
	if (point_num > 3)
		gsl_i2c_read_bytes(i2c_client, 0x90, &touch_data[16], 4);
	if (point_num > 4)
		gsl_i2c_read_bytes(i2c_client, 0x94, &touch_data[20], 4);
	if (point_num > 5)
		gsl_i2c_read_bytes(i2c_client, 0x98, &touch_data[24], 4);
	if (point_num > 6)
		gsl_i2c_read_bytes(i2c_client, 0x9c, &touch_data[28], 4);
	if (point_num > 7)
		gsl_i2c_read_bytes(i2c_client, 0xa0, &touch_data[32], 4);
	if (point_num > 8)
		gsl_i2c_read_bytes(i2c_client, 0xa4, &touch_data[36], 4);
	if (point_num > 9)
		gsl_i2c_read_bytes(i2c_client, 0xa8, &touch_data[40], 4);

	#if 0//def GSL_GESTURE
		GSL_LOGD("GSL:::0x80=%02x%02x%02x%02x\n",tmp_buf[3],tmp_buf[2],tmp_buf[1],tmp_buf[0]);
		GSL_LOGD("GSL:::0x84=%02x%02x%02x%02x\n",tmp_buf[7],tmp_buf[6],tmp_buf[5],tmp_buf[4]);
		GSL_LOGD("GSL:::0x88=%02x%02x%02x%02x\n",tmp_buf[11],tmp_buf[10],tmp_buf[9],tmp_buf[8]);
	#endif

#ifdef GSL_NOID_VERSION
	cinfo.finger_num = point_num;
//	GSL_LOGD("tp-gsl  finger_num = %d\n", cinfo.finger_num);
	for (i = 0; i < (point_num < MAX_CONTACTS ? point_num : MAX_CONTACTS);
	     i++) {
		temp_a = touch_data[(i + 1) * 4 + 3] & 0x0f;
		temp_b = touch_data[(i + 1) * 4 + 2];
		cinfo.x[i] = temp_a << 8 | temp_b;
		temp_a = touch_data[(i + 1) * 4 + 1];
		temp_b = touch_data[(i + 1) * 4 + 0];
		cinfo.y[i] = temp_a << 8 | temp_b;
		cinfo.id[i] = ((touch_data[(i + 1) * 4 + 3] & 0xf0) >> 4);
//		GSL_LOGD(
//			"tp-gsl  before: x[%d] = %d, y[%d] = %d, id[%d] = %d\n",
//			i, cinfo.x[i], i, cinfo.y[i], i, cinfo.id[i]);
	}
	cinfo.finger_num = (touch_data[3] << 24) | (touch_data[2] << 16) |
			   (touch_data[1] << 8) | touch_data[0];
	gsl_alg_id_main(&cinfo);
	tmp1 = gsl_mask_tiaoping();
//	GSL_LOGD("[tp-gsl] tmp1=%x\n", tmp1);
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		gsl_i2c_write_bytes(i2c_client, 0xf0, buf, 4);
		buf[0] = (u8)(tmp1 & 0xff);
		buf[1] = (u8)((tmp1 >> 8) & 0xff);
		buf[2] = (u8)((tmp1 >> 16) & 0xff);
		buf[3] = (u8)((tmp1 >> 24) & 0xff);
//		GSL_LOGD(
//			"tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",
//			tmp1, buf[0], buf[1], buf[2], buf[3]);
		gsl_i2c_write_bytes(i2c_client, 0x8, buf, 4);
	}
	point_num = cinfo.finger_num;
#endif

#ifdef GSL_GESTURE
       GSL_LOGD("gsl_gesture_status=%d,gsl_gesture_flag=%d\n",gsl_gesture_status,gsl_gesture_flag);

		if(tpd_halt == 1 && gsl_gesture_flag == 1)
		
    {
			int tmp_c;
			u8 key_data = 0;
			tmp_c = gsl_obtain_gesture();
			GSL_LOGD("gsl_obtain_gesture():tmp_c=0x%x\n",tmp_c);
			GSL_LOGD("gsl_obtain_gesture():tmp_c=0x%x\n",tmp_c);
			switch(tmp_c){
			case (int)'C':
				key_data = KEYCODE_CTPC;
				break;
			case (int)'E':
				key_data = KEYCODE_CTPE;
				break;
			case (int)'W':
				key_data = KEYCODE_CTPW;
				break;
			case (int)'O':
				key_data = KEYCODE_CTPO;
				break;
			case (int)'M':
				key_data = KEYCODE_CTPM;
				break;
			case (int)'Z':
				key_data = KEYCODE_CTPZ;
				break;
			case (int)'S':
				key_data = KEYCODE_CTPS;
				break;
			case (int)'*':	
				key_data = KEY_POWER;
				break;/* double click */			
			}
	
			if(key_data != 0){
				gsl_gesture_c = (char)(tmp_c & 0xff);

			do_gettimeofday(&time_now);
			if(time_now.tv_sec - time_last.tv_sec <= 1 
			&& (time_now.tv_sec - time_last.tv_sec)*1000*1000 + (time_now.tv_usec - time_last.tv_usec) < 700*1000)
				return;

			
			tpd_gpio_output(GTP_RST_PORT, 0);	
			msleep(5);
			tpd_gpio_output(GTP_RST_PORT, 1);
			msleep(2);
		
			reset_chip(i2c_client);
			startup_chip(i2c_client);

				
			msleep(2);
			do_gettimeofday(&time_last);

				printk("gsl_obtain_gesture():tmp_c=%c\n",gsl_gesture_c);
				input_report_key(tpd->dev,key_data,1);

				input_sync(tpd->dev);
				input_report_key(tpd->dev,key_data,0);

				input_sync(tpd->dev);
				msleep(400);
			}
			return;
		}		

#endif

	gsl_report_point(&cinfo);
#ifdef GSL_MONITOR
	i2c_lock_flag = 0;
#endif
}

#ifdef GSL_MONITOR
static void gsl_monitor_worker(struct work_struct *work)
{
	int result = 0;
	int mon_work_cycle = MONITOR_CYCLE_NORMAL;

	GSL_LOGD("---------gsl monitor_worker-------\n");
#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		return;
#endif
	if (i2c_lock_flag == 0) {
		result = check_mode(
			i2c_client,
			(power_status + interrupt_status + esd_scanning));
		if (result)
			init_chip(client);
		GSL_LOGD("---result num is[%d] ", result);
		GSL_LOGD("power_shutdowned[%d]", power_shutdowned);
		GSL_LOGD("interrupt_fail[%d] ", interrupt_fail);
		GSL_LOGD("esd_protected[%d]\n", esd_protected);
	} else if (i2c_lock_flag == 1) {
		mon_work_cycle = MONITOR_CYCLE_IDLE;
		i2c_lock_flag = 0;
	} else if (i2c_lock_flag == 2) {
		mon_work_cycle = MONITOR_CYCLE_BY_REG_CHECK;
	}
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work,
			   mon_work_cycle);
}
#endif

#define SUPPORT_TP_KERNEL_CHECK
#ifdef SUPPORT_TP_KERNEL_CHECK

#if defined(ATA_TP_ADDR)
#define RAWDATA_ADDR ATA_TP_ADDR
#endif

#define DRV_NUM 15
#define SEN_NUM 10
#define RAWDATA_THRESHOLD 6000
#define DAC_THRESHOLD 20
#define MAX_SEN_NUM 15
static const u8 sen_order[SEN_NUM] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

int ctp_factory_test(void)
{
	u8 buf[4], i, offset;
	u32 rawdata_value, dac_value;
	struct i2c_client *client = i2c_client;

	if (!client) {
		GSL_LOGE("err ,client is NULL,ctp factory_test\n");
		return -1;
	}

	msleep(800);
	/* msleep(20000); */
	for (i = 0; i < DRV_NUM; i++) {
		buf[3] = 0;
		buf[2] = 0;
		buf[1] = 0;
		buf[0] = (RAWDATA_ADDR + SEN_NUM * 2 * i) / 0x80;
		offset = (RAWDATA_ADDR + SEN_NUM * 2 * i) % 0x80;
		gsl_i2c_write_bytes(client, 0xf0, buf, 4);
		gsl_i2c_read_bytes(client, offset, buf, 4);
		gsl_i2c_read_bytes(client, offset, buf, 4);
		rawdata_value = (buf[1] << 8) + buf[0];
		GSL_LOGD("rawdata_value = %d\n", rawdata_value);
		if (rawdata_value > RAWDATA_THRESHOLD) {
			rawdata_value = (buf[3] << 8) + buf[2];
			GSL_LOGD("===>rawdata_value = %d\n", rawdata_value);
			if (rawdata_value > RAWDATA_THRESHOLD) {
				GSL_LOGE("###>rawdata_value = %d\n",
					 rawdata_value);
				return -1; /* fail */
			}
		}
	}

	for (i = 0; i < SEN_NUM; i++) {
		buf[3] = 0x01;
		buf[2] = 0xfe;
		buf[1] = 0x10;
		buf[0] = 0x00;
		offset = 0x10 + (sen_order[i] / 4) * 4;
		gsl_i2c_write_bytes(client, 0xf0, buf, 4);
		gsl_i2c_read_bytes(client, offset, buf, 4);
		gsl_i2c_read_bytes(client, offset, buf, 4);

		dac_value = buf[sen_order[i] % 4];
		GSL_LOGD("===dac_value = %d DAC_THRESHOLD = %d===\n", dac_value,
			 DAC_THRESHOLD);
		if (dac_value < DAC_THRESHOLD) {
			GSL_LOGE("dac_value %d < thres %d\n", dac_value,
				 DAC_THRESHOLD);
			return -1; /* fail */
		}
	}

	return 0; /* pass */
}
#endif
static int touch_event_handler(void *unused)
{
	struct sched_param param = {.sched_priority = 4};

	sched_setscheduler(current, SCHED_RR, &param);

	GSL_LOGF();
	do {
		enable_irq(touch_irq);
		set_current_state(TASK_INTERRUPTIBLE);
		wait_event_interruptible(waiter, tpd_flag != 0);
		disable_irq(touch_irq);
		tpd_flag = 0;
		TPD_DEBUG_SET_TIME;
		set_current_state(TASK_RUNNING);
		GSL_LOGD("===touch event_handler, task running===\n");

		report_data_handle();
	} while (!kthread_should_stop());

	return 0;
}

static irqreturn_t tpd_eint_interrupt_handler(void)
{
	tpd_flag = 1;
	wake_up_interruptible(&waiter);

	return IRQ_HANDLED;
}

static int tpd_i2c_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	strcpy(info->type, TPD_DEVICE);
	return 0;
}

#ifdef GREEN_MODE
static void green_mode(struct i2c_client *client, int mode)
{
	int i;
	u8 buf[4] = {0x00};

	if ((mode != MODE_ON) && (mode != MODE_OFF))
		return;

	for (i = 0; i < 5; i++) {
		buf[0] = 0x0a;
		gsl_i2c_write_bytes(client, 0xf0, &buf[0], 1);
		buf[0] = 0x00;
		buf[1] = 0x00;
		if (mode == MODE_ON) {
			GSL_LOGD("green mode is on.");
			buf[2] = 0x01;
		} else if (mode == MODE_OFF) {
			GSL_LOGD("green mode is off.");
			buf[2] = 0x00;
		}
		buf[3] = 0x5a;
		gsl_i2c_write_bytes(client, 0x08, buf, 4);
		msleep(20);
	}
}
#endif

#ifdef GSL_LATE_INIT_CHIP
static void gsl_late_init_worker(struct work_struct *work)
{
	int result = 0;
	int ret = 0;

	GSL_LOGD("---------gsl late_init_worker-------\n");
	if (1) {
		result = check_mode(
			i2c_client,
			(power_status + interrupt_status + esd_scanning));
		if (result)
			init_chip(i2c_client);
		GSL_LOGD("---result num is[%d] ", result);
		GSL_LOGD("power_shutdowned[%d]", power_shutdowned);
		GSL_LOGD("interrupt_fail[%d] ", interrupt_fail);
		GSL_LOGD("esd_protected[%d]\n", esd_protected);
	}
	check_mem_data(i2c_client);
	if (ret < 0) {
		GSL_LOGE("Failed to init chip!\n");
		return;
	}
}
#endif

static int tpd_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0;
	struct device_node *node = NULL;

	GSL_LOGF();

	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(100);
#ifdef GSL_POWEN_ON
	ret = regulator_enable(tpd->reg);
	if (ret != 0) {
		GSL_LOGE("Failed to enable regulator: %d\n", ret);
		return -1;
	}

	msleep(100);
#endif

#ifdef __CUST_GPIO_CTP_EN_PIN__
#if __CUST_GPIO_CTP_EN_PIN__
	tpd_gpio_output(GTP_POWER_PORT, 1);
#endif
#endif
	tpd_gpio_output(GTP_RST_PORT, 1);
	tpd_gpio_as_int(GTP_INT_PORT);
	msleep(50);

	i2c_client = client;

#ifdef GSL_GESTURE
	gsl_FunIICRead(gsl_read_oneframe_data);
    	gsl_GestureExternInt(gsl_model_extern,sizeof(gsl_model_extern)/sizeof(unsigned int)/18);
#endif

	
#ifdef GSL_LATE_INIT_CHIP
	GSL_LOGD("tpd i2c_probe () : queue gsl_late_init_workqueue\n");
	INIT_DELAYED_WORK(&gsl_late_init_work, gsl_late_init_worker);
	gsl_late_init_workqueue =
		create_singlethread_workqueue("gsl_late_init_workqueue");
	queue_delayed_work(gsl_late_init_workqueue, &gsl_late_init_work,
			   LATE_INIT_CYCLE_BY_REG_CHECK);
#else
	ret = init_chip(i2c_client);
	check_mem_data(i2c_client);
	if (ret < 0) {
		GSL_LOGE("Failed to init chip!\n");
		return -1;
	}
#endif
#ifdef GREEN_MODE
	green_mode(i2c_client, MODE_ON);
	reset_chip(i2c_client);
	startup_chip(i2c_client);
#endif
	node = of_find_matching_node(NULL, touch_of_match);
	if (node) {
		GSL_LOGD("node -> name = %s , touch_irq = %d\n", node->name,
			 touch_irq);
		touch_irq = irq_of_parse_and_map(node, 0);
		GSL_LOGD("touch_irq = %d\n ", touch_irq);
		ret = request_irq(touch_irq,
				  (irq_handler_t)tpd_eint_interrupt_handler,
				  IRQF_TRIGGER_RISING, TPD_DEVICE, NULL);
		if (ret > 0) {
			ret = -1;
			GSL_LOGE(
				" gslX680 -- error : tpd request_irq IRQ LINE NOT AVAILABLE!.\n");
			return ret;
		}
	} else {
		ret = -1;
		GSL_LOGE("gslX680 -- error : no irq node!!\n");
		return ret;
	}
	disable_irq(touch_irq);

	tpd_load_status = 1;
	GSL_LOGD("tpd_load_status = 1");

	thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		GSL_LOGE(TPD_DEVICE " failed to create kernel thread: %d\n",
			 ret);
		return ret;
	}
#if 0 /* def SUPPORT_TP_KERNEL_CHECK */
	tp_check_flag = ctp_factory_test();
	GSL_LOGD("\ntp_check_flag = %x\n", tp_check_flag);
	if (tp_check_flag == 0)
		tp_check_flag = 1;
	else
		tp_check_flag = 0;
	/* mdelay(500); */
	/* eboda_support_tp_check_put(tp_check_flag); */
	tpd_load_status = tp_check_flag;
#endif

#ifdef GSL_MONITOR
	GSL_LOGD("tpd i2c_probe () : queue gsl_monitor_workqueue\n");

	INIT_DELAYED_WORK(&gsl_monitor_work, gsl_monitor_worker);
	gsl_monitor_workqueue =
		create_singlethread_workqueue("gsl_monitor_workqueue");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work,
			   MONITOR_CYCLE_BY_REG_CHECK);
#endif

#ifdef TPD_PROC_DEBUG
#if 0
		gsl_config_proc = create_proc_entry(GSL_CONFIG_PROC_FILE,
		0666, NULL);
		if (gsl_config_proc == NULL) {
			GSL_LOGD("create_proc_entry %s failed\n",
			GSL_CONFIG_PROC_FILE);
		} else {
			gsl_config_proc->read_proc = gsl_config_read_proc;
			gsl_config_proc->write_proc = gsl_config_write_proc;
		}
#else
	proc_create(GSL_CONFIG_PROC_FILE, 0660, NULL, &gsl_seq_fops);
#endif
	gsl_proc_flag = 0;
#endif

#ifdef GSL_GESTURE
			input_set_capability(tpd->dev, EV_KEY, KEY_POWER);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPC);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPE);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPO);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPW);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPM);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPZ);
			input_set_capability(tpd->dev, EV_KEY, KEYCODE_CTPS);
#endif


	/* enable_irq(touch_irq); */
	GSL_LOGD("tpd i2c_probe is ok -----------------");

	return 0;
}

static int tpd_i2c_remove(struct i2c_client *client)
{
	GSL_LOGD("==tpd i2c_remove==\n");
#if !defined(CONFIG_MTK_I2C_EXTENSION) || defined(GSLTP_ENABLE_I2C_DMA)
	msg_dma_release();
#endif

	return 0;
}

static const struct i2c_device_id tpd_i2c_id[] = {{TPD_DEVICE, 0}, {} };

static unsigned short force[] = {0, (GSLX680_ADDR << 1), I2C_CLIENT_END,
				 I2C_CLIENT_END};
static const unsigned short *const forces[] = {force, NULL};
/* static struct i2c_client_address_data addr_data = { .forces = forces,}; */
/* #endif */

static const struct of_device_id tpd_of_match[] = {
	{.compatible = "mediatek,cap_touch_gsl"}, {},
};

struct i2c_driver tpd_i2c_driver = {
	.driver = {

			.name = TPD_DEVICE,
			.of_match_table = tpd_of_match,
#ifndef ADD_I2C_DEVICE_ANDROID_4_0
			.owner = THIS_MODULE,
#endif
		},
	.probe = tpd_i2c_probe,
	.remove = tpd_i2c_remove,
	.id_table = tpd_i2c_id,
	.detect = tpd_i2c_detect,
#ifndef ADD_I2C_DEVICE_ANDROID_4_0
/* .address_data = &addr_data, */
#endif
	.address_list = (const unsigned short *)forces,
};

int tpd_local_init(void)
{
	int retval;

	GSL_LOGF();
#if !defined(CONFIG_MTK_I2C_EXTENSION) || defined(GSLTP_ENABLE_I2C_DMA)
	retval = msg_dma_alloc();
	if (retval)
		return retval;
#endif
#ifdef GSL_POWEN_ON
	tpd->reg = regulator_get(tpd->tpd_dev, "vtouch");
	retval = regulator_set_voltage(tpd->reg, 2800000, 2800000);
	if (retval != 0) {
		GSL_LOGE("Failed to set reg-vgp6 voltage: %d\n", retval);
		return -1;
	}
#endif
	if (i2c_add_driver(&tpd_i2c_driver) != 0) {
		GSL_LOGE("unable to add i2c driver.\n");
		return -1;
	}

	if (tpd_load_status == 0) {
		GSL_LOGE("add error touch panel driver.\n");
		i2c_del_driver(&tpd_i2c_driver);
		return -1;
	}

	input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0,
			     (MAX_CONTACTS + 1), 0, 0);
#ifdef TPD_HAVE_BUTTON
	tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local,
			   tpd_keys_dim_local); /* initialize tpd button data */
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
	TPD_DO_WARP = 1;
	memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT * 4);
	memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT * 4);
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
	memcpy(tpd_calmat, tpd_calmat_local, 8 * 4);
	memcpy(tpd_def_calmat, tpd_def_calmat_local, 8 * 4);
#endif
	tpd_type_cap = 1;

	GSL_LOGD("tpd local_init is ok.");
	return 0;
}

/* Function to manage low power suspend */
static void tpd_suspend(struct device *h)
{
	if (tpd_halt == 1) {
		pr_info("gslX680 already in suspended status\n");
		return;
	}
	GSL_LOGD("tpd suspend is begin.");
	GSL_LOGF();

#ifdef GSL_MONITOR
	GSL_LOGD("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif
	tpd_halt = 1;
#ifdef GSL_GESTURE
		if(gsl_gesture_flag == 1){
			gsl_enter_doze(i2c_client);
			return;
		}
#endif

	tpd_gpio_output(GTP_RST_PORT, 0);
	msleep(20);

	disable_irq(touch_irq);


	GSL_LOGD("tpd suspend is ok.");
}

/* Function to manage power-on resume */
static void tpd_resume(struct device *h)
{
	if (tpd_halt == 0) {
		pr_info("gslX680 already in resumed status\n");
		return;
	}
	GSL_LOGD("tpd resume is begin.");
	GSL_LOGF();

#ifdef GSL_GESTURE
			if(gsl_gesture_flag == 1){
				gsl_quit_doze(i2c_client);
			}
#endif
	tpd_gpio_output(GTP_RST_PORT, 1);

	msleep(20);

	reset_chip(i2c_client);

#ifdef GSL_GESTURE	
#ifdef GSL_NOID_VERSION
		gsl_DataInit(gsl_config_data_id);
#endif
#endif

	startup_chip(i2c_client);
	check_mem_data(i2c_client);

#if defined(GSL_MONITOR)
	GSL_LOGD("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work,
			   MONITOR_CYCLE_IDLE);
#endif
	enable_irq(touch_irq);
	tpd_halt = 0;
	GSL_LOGD("tpd resume is ok.");
}

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = GSLX680_NAME,
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
#ifdef TPD_HAVE_BUTTON
	.tpd_have_button = 1,
#else
	.tpd_have_button = 0,
#endif
};

#if 0
static ssize_t db_value_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	unsigned long rs_tmp;

	if (kstrtoul(buf, 10, &rs_tmp))
		return 0;

	rs_value1 = rs_tmp;

	return count;
}

static ssize_t db_value_show(struct class *class,
			struct class_attribute *attr,	char *buf)
{
	return sprintf(buf, "rs_value1 = %d \r\n", rs_value1);
}

static struct class_attribute db_class_attrs[] = {
	__ATTR(db, 0644, db_value_show, db_value_store),
	__ATTR_NULL
};
#endif

static struct class db_interface_class = {
	.name = "db_interface",
	/* .class_attrs = db_class_attrs, */
};

/* called when loaded into kernel */
static int __init tpd_driver_init(void)
{
	int ret = 0;

	GSL_LOGF();
	tpd_get_dts_info();
	/* register usr space */
	ret = class_register(&db_interface_class);
#ifdef ADD_I2C_DEVICE_ANDROID_4_0
	i2c_register_board_info(1, &gslX680_i2c_tpd, 1);
#endif
	if (tpd_driver_add(&tpd_device_driver) < 0)
		GSL_LOGE("add gslX680 driver failed\n");

	GSL_LOGD("gslX680 driver init ok");
	return 0;
}

/* should never be called */
static void __exit tpd_driver_exit(void)
{
	GSL_LOGD("Sileadinc gslX680 touch panel driver exit\n");
	/* input_unregister_device(tpd->dev); */
	class_unregister(&db_interface_class);
	tpd_driver_remove(&tpd_device_driver);
}
module_init(tpd_driver_init);
module_exit(tpd_driver_exit);

MODULE_AUTHOR("leweihua");
MODULE_DESCRIPTION("GSLX680 TouchScreen  Driver");
MODULE_LICENSE("GPL v2");
