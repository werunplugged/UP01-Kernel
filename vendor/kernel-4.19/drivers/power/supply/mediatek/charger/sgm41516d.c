// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "sgm41516d.h"


/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/
//extern int hw_charging_get_charger_type(void);

int g_chg_info;
int led_ctl_gpio;

/*SMG41516 start*/
enum chg_info {
	BQ25601D = 1,
	BQ25601,
	SGM41516,
	SGM41516D,
	CHG_INFO_MAX,
};

enum cv_fine_val {
	CV_NORMAL=0,
	CV_POST_8MV,
	CV_NEG_8MV,
	CV_NEG_16MV,
	CV_MAX,
};

enum usb_val {
	USB_HIZ=0,
	USB_0V,
	USB_0_6V,
	USB_3_3V,
	USB_VAL_MAX,
};

enum wdt_value {
	DISABLE_WDT=0,
	WDT_40S,
	WDT_80S,
	WDT_160S,
};
const unsigned int VBAT_CV_FINE[] = {32000,8000};
/*SMG41516 end*/

enum sgm41516d_pmu_chg_type {
	SGM41516D_CHG_TYPE_NOVBUS = 0,
	SGM41516D_CHG_TYPE_SDP,
	SGM41516D_CHG_TYPE_CDP,
	SGM41516D_CHG_TYPE_DCP,
	SGM41516D_CHG_TYPE_UNKNOWN=5,
	SGM41516D_CHG_TYPE_SDPNSTD,
	SGM41516D_CHG_TYPE_OTG,
	SGM41516D_CHG_TYPE_MAX,
};

static enum charger_type bq_chg_type;
#define BQ_DET_COUNT_MAX 16

#define GETARRAYNUM(array) (ARRAY_SIZE(array))

const unsigned int VBAT_CV_VTH[] = {
	3856000, 3888000, 3920000, 3952000,
	3984000, 4016000, 4048000, 4080000,
	4112000, 4144000, 4176000, 4208000,
	4240000, 4272000, 4304000, 4336000,
	4368000, 4400000, 4432000, 4464000,
	4496000, 4528000, 4560000, 4592000,
	4624000

};

const unsigned int CS_VTH[] = {
	0, 6000, 12000, 18000, 24000,
	30000, 36000, 42000, 48000, 54000,
	60000, 66000, 72000, 78000, 84000,
	90000, 96000, 102000, 108000, 114000,
	120000, 126000, 132000, 138000, 144000,
	150000, 156000, 162000, 168000, 174000,
	180000, 186000, 192000, 198000, 204000,
	210000, 216000, 222000, 228000, 234000,
	240000, 246000, 252000, 258000, 264000,
	270000, 276000, 282000, 288000, 294000,
	300000, 306000
};

const unsigned int INPUT_CS_VTH[] = {
	10000, 20000, 30000, 40000,
	50000, 60000, 70000, 80000,
	90000, 100000, 110000, 120000,
	130000, 140000, 150000, 160000,
	170000, 180000, 190000, 200000,
	210000, 220000, 230000, 250000,
	260000, 270000, 280000, 290000,
	300000, 310000, 320000
};


const unsigned int VCDT_HV_VTH[] = {
	4200000, 4250000, 4300000, 4350000,
	4400000, 4450000, 4500000, 4550000,
	4600000, 6000000, 6500000, 7000000,
	7500000, 8500000, 9500000, 10500000

};


const unsigned int VINDPM_REG[] = {
	3900, 4000, 4100, 4200, 4300, 4400,
	4500, 4600, 4700, 4800, 4900, 5000,
	5100, 5200, 5300, 5400, 5500, 5600,
	5700, 5800, 5900, 6000, 6100, 6200,
	6300, 6400
};

/* SGM41516D REG0A BOOST_LIM[2:0], mA */
const unsigned int BOOST_CURRENT_LIMIT[] = {
	500, 1200
};

DEFINE_MUTEX(g_input_current_mutex);
static const struct i2c_device_id sgm41516d_i2c_id[] = { {"sgm41516d", 0}, {} };

static int sgm41516d_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id);

unsigned int charging_value_to_parameter(const unsigned int
		*parameter, const unsigned int array_size,
		const unsigned int val)
{
	if (val < array_size)
		return parameter[val];

	pr_info("Can't find the parameter\n");
	return parameter[0];

}

unsigned int charging_parameter_to_value(const unsigned int
		*parameter, const unsigned int array_size,
		const unsigned int val)
{
	unsigned int i;

	pr_debug_ratelimited("array_size = %d\n", array_size);

	for (i = 0; i < array_size; i++) {
		if (val == *(parameter + i))
			return i;
	}

	pr_info("NO register value match\n");
	/* TODO: ASSERT(0);    // not find the value */
	return 0;
}

static unsigned int bmt_find_closest_level(const unsigned int *pList,
		unsigned int number,
		unsigned int level)
{
	unsigned int i;
	unsigned int max_value_in_last_element;

	if (pList[0] < pList[1])
		max_value_in_last_element = 1;
	else
		max_value_in_last_element = 0;

	if (max_value_in_last_element == 1) {
		for (i = (number - 1); i != 0;
		     i--) {	/* max value in the last element */
			if (pList[i] <= level) {
				pr_debug_ratelimited("zzf_%d<=%d, i=%d\n",
					pList[i], level, i);
				return pList[i];
			}
		}

		pr_info("Can't find closest level\n");
		return pList[0];
		/* return CHARGE_CURRENT_0_00_MA; */
	} else {
		/* max value in the first element */
		for (i = 0; i < number; i++) {
			if (pList[i] <= level)
				return pList[i];
		}

		pr_info("Can't find closest level\n");
		return pList[number - 1];
		/* return CHARGE_CURRENT_0_00_MA; */
	}
}

static unsigned int sgm41516d_closest_reg(u32 min, u32 max, u32 step, u32 target)
{
	if (target < min)
		return 0;

	if (target >= max)
		target = max;

	return (target - min) / step;
}

/**********************************************************
 *
 *   [I2C Function For Read/Write sgm41516d]
 *
 *********************************************************/
#ifdef CONFIG_MTK_I2C_EXTENSION
unsigned int sgm41516d_read_byte(struct sgm41516d_info *info, unsigned char cmd,
			       unsigned char *returnData)
{
	char cmd_buf[1] = { 0x00 };
	char readData = 0;
	int ret = 0;

	mutex_lock(&info->sgm41516d_i2c_access);

	/* info->client->addr = ((info->client->addr) & I2C_MASK_FLAG) |
	 * I2C_WR_FLAG;
	 */
	info->client->ext_flag =
		((info->client->ext_flag) & I2C_MASK_FLAG) | I2C_WR_FLAG |
		I2C_DIRECTION_FLAG;

	cmd_buf[0] = cmd;
	ret = i2c_master_send(info->client, &cmd_buf[0], (1 << 8 | 1));
	if (ret < 0) {
		/* info->client->addr = info->client->addr & I2C_MASK_FLAG; */
		info->client->ext_flag = 0;
		mutex_unlock(&info->sgm41516d_i2c_access);

		return 0;
	}

	readData = cmd_buf[0];
	*returnData = readData;

	/* info->client->addr = info->client->addr & I2C_MASK_FLAG; */
	info->client->ext_flag = 0;
	mutex_unlock(&info->sgm41516d_i2c_access);

	return 1;
}

unsigned int sgm41516d_write_byte(struct sgm41516d_info *info,  unsigned char cmd,
				unsigned char writeData)
{
	char write_data[2] = { 0 };
	int ret = 0;

	mutex_lock(&info->sgm41516d_i2c_access);

	write_data[0] = cmd;
	write_data[1] = writeData;

	info->client->ext_flag = ((info->client->ext_flag) & I2C_MASK_FLAG) |
			       I2C_DIRECTION_FLAG;

	ret = i2c_master_send(info->client, write_data, 2);
	if (ret < 0) {
		info->client->ext_flag = 0;
		mutex_unlock(&info->sgm41516d_i2c_access);
		return 0;
	}

	info->client->ext_flag = 0;
	mutex_unlock(&info->sgm41516d_i2c_access);
	return 1;
}
#else
unsigned int sgm41516d_read_byte(struct sgm41516d_info *info, unsigned char cmd,
			       unsigned char *returnData)
{
	unsigned char xfers = 2;
	int ret, retries = 1;

	mutex_lock(&info->sgm41516d_i2c_access);

	do {
		struct i2c_msg msgs[2] = {
			{
				.addr = info->client->addr,
				.flags = 0,
				.len = 1,
				.buf = &cmd,
			},
			{

				.addr = info->client->addr,
				.flags = I2C_M_RD,
				.len = 1,
				.buf = returnData,
			}
		};

		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(info->client->adapter, msgs, xfers);

		if (ret == -ENXIO) {
			pr_info("skipping non-existent adapter %s\n",
				info->client->adapter->name);
			break;
		}
	} while (ret != xfers && --retries);

	mutex_unlock(&info->sgm41516d_i2c_access);

	return ret == xfers ? 1 : -1;
}

unsigned int sgm41516d_write_byte(struct sgm41516d_info *info, unsigned char cmd,
				unsigned char writeData)
{
	unsigned char xfers = 1;
	int ret, retries = 1;
	unsigned char buf[8];

	mutex_lock(&info->sgm41516d_i2c_access);

	buf[0] = cmd;
	memcpy(&buf[1], &writeData, 1);

	do {
		struct i2c_msg msgs[1] = {
			{
				.addr = info->client->addr,
				.flags = 0,
				.len = 1 + 1,
				.buf = buf,
			},
		};

		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(info->client->adapter, msgs, xfers);

		if (ret == -ENXIO) {
			pr_info("skipping non-existent adapter %s\n",
				info->client->adapter->name);
			break;
		}
	} while (ret != xfers && --retries);

	mutex_unlock(&info->sgm41516d_i2c_access);

	return ret == xfers ? 1 : -1;
}
#endif
/**********************************************************
 *
 *   [Read / Write Function]
 *
 *********************************************************/
unsigned int sgm41516d_read_interface(struct sgm41516d_info *info, unsigned char RegNum,
				    unsigned char *val, unsigned char MASK,
				    unsigned char SHIFT)
{
	unsigned char sgm41516d_reg = 0;
	unsigned int ret = 0;

	mutex_lock(&info->sgm41516d_access_lock);

	ret = sgm41516d_read_byte(info, RegNum, &sgm41516d_reg);
	if (ret < 0)
		pr_info("[%s] read Reg[%x] failed ret = %d\n", __func__, RegNum, ret);

	pr_info("[%s] Reg[%x]=0x%x\n", __func__, RegNum, sgm41516d_reg);

	sgm41516d_reg &= (MASK << SHIFT);
	*val = (sgm41516d_reg >> SHIFT);

	pr_info("[%s] val=0x%x\n", __func__, *val);
	mutex_unlock(&info->sgm41516d_access_lock);

	return ret;
}

unsigned int sgm41516d_config_interface(struct sgm41516d_info *info, unsigned char RegNum,
				      unsigned char val, unsigned char MASK,
				      unsigned char SHIFT)
{
	unsigned char sgm41516d_reg = 0;
	unsigned char sgm41516d_reg_ori = 0;
	unsigned int ret = 0;

	mutex_lock(&info->sgm41516d_access_lock);

	ret = sgm41516d_read_byte(info, RegNum, &sgm41516d_reg);
	if (ret < 0)
		pr_info("[%s] read Reg[%x] failed ret = %d\n", __func__, RegNum, ret);

	sgm41516d_reg_ori = sgm41516d_reg;
	sgm41516d_reg &= ~(MASK << SHIFT);
	sgm41516d_reg |= (val << SHIFT);


	ret = sgm41516d_write_byte(info, RegNum, sgm41516d_reg);
	if (ret < 0)
		pr_info("[%s] write Reg[%x] failed ret = %d\n", __func__, RegNum, ret);

	mutex_unlock(&info->sgm41516d_access_lock);
	pr_info("[%s] write Reg[%x]=0x%x from 0x%x\n", __func__,
			     RegNum,
			     sgm41516d_reg, sgm41516d_reg_ori);

	return ret;
}

/**********************************************************
 *
 *   [platform_driver API]
 *
 *********************************************************/
unsigned char g_reg_value_sgm41516d;
static ssize_t show_sgm41516d_access(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	pr_info("[%s] 0x%x\n", __func__, g_reg_value_sgm41516d);
	return sprintf(buf, "0x%x\n", (unsigned int)g_reg_value_sgm41516d);
}

static ssize_t store_sgm41516d_access(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret = 0;
	char *pvalue = NULL, *addr, *val;
	unsigned int reg_value = 0;
	unsigned int reg_address = 0;
	struct sgm41516d_info *info = dev_get_drvdata(dev);

	pr_info("[%s]\n", __func__);

	if (buf != NULL && size != 0) {
		pr_info("[%s] buf is %s and size is %zu\n", __func__, buf,
			size);

		pvalue = (char *)buf;
		if (size > 5) {
			addr = strsep(&pvalue, " ");
			ret = kstrtou32(addr, 16,
				(unsigned int *)&reg_address);
		} else
			ret = kstrtou32(pvalue, 16,
				(unsigned int *)&reg_address);

		if (size > 5) {
			val = strsep(&pvalue, " ");
			ret = kstrtou32(val, 16, (unsigned int *)&reg_value);
			pr_info(
			"[%s] write sgm41516d reg 0x%x with value 0x%x !\n",
			__func__,
			(unsigned int) reg_address, reg_value);
			//ret = sgm41516d_write_byte(info, reg_address, reg_value);

			ret = sgm41516d_config_interface(info, reg_address,
				reg_value, 0xFF, 0x0);
		} else {
			ret = sgm41516d_read_byte(info, reg_address, &g_reg_value_sgm41516d);
			pr_info(
			"[%s] read sgm41516d reg 0x%x with value 0x%x !\n",
			__func__,
			(unsigned int) reg_address, g_reg_value_sgm41516d);
			pr_info(
			"[%s] use \"cat sgm41516d_access\" to get value\n",
			__func__);
		}
	}
	return size;
}

static DEVICE_ATTR(sgm41516d_access, 0664, show_sgm41516d_access,
		   store_sgm41516d_access);	/* 664 */


/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
/* CON0---------------------------------------------------- */
void sgm41516d_set_en_hiz(struct sgm41516d_info *info, bool val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON0),
				       (unsigned char) (val),
				       (unsigned char) (CON0_EN_HIZ_MASK),
				       (unsigned char) (CON0_EN_HIZ_SHIFT)
				      );
}

void sgm41516d_set_iinlim(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON0),
				       (unsigned char) (val),
				       (unsigned char) (CON0_IINLIM_MASK),
				       (unsigned char) (CON0_IINLIM_SHIFT)
				      );
}

void sgm41516d_set_stat_ctrl(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON0),
				   (unsigned char) (val),
				   (unsigned char) (CON0_STAT_IMON_CTRL_MASK),
				   (unsigned char) (CON0_STAT_IMON_CTRL_SHIFT)
				   );
}

/* CON1---------------------------------------------------- */
/*It can only be used once during boot initialization*/
void sgm41516d_set_reg_rst(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON11),
				       (unsigned char) (val),
				       (unsigned char) (CON11_REG_RST_MASK),
				       (unsigned char) (CON11_REG_RST_SHIFT)
				      );
}

void sgm41516d_set_pfm(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_PFM_MASK),
				       (unsigned char) (CON1_PFM_SHIFT)
				      );
}

void sgm41516d_set_wdt_rst(struct sgm41516d_info *info, bool val)
{

	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_WDT_RST_MASK),
				       (unsigned char) (CON1_WDT_RST_SHIFT)
				      );

}

void sgm41516d_set_otg_config(struct sgm41516d_info *info, bool val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_OTG_CONFIG_MASK),
				       (unsigned char) (CON1_OTG_CONFIG_SHIFT)
				      );
}


void sgm41516d_set_chg_config(struct sgm41516d_info *info, bool val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_CHG_CONFIG_MASK),
				       (unsigned char) (CON1_CHG_CONFIG_SHIFT)
				      );
}


void sgm41516d_set_sys_min(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_SYS_MIN_MASK),
				       (unsigned char) (CON1_SYS_MIN_SHIFT)
				      );
}

void sgm41516d_set_batlowv(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_MIN_VBAT_SEL_MASK),
				       (unsigned char) (CON1_MIN_VBAT_SEL_SHIFT)
				      );
}



/* CON2---------------------------------------------------- */
void sgm41516d_set_rdson(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_Q1_FULLON_MASK),
				       (unsigned char) (CON2_Q1_FULLON_SHIFT)
				      );
}

void sgm41516d_set_boost_lim(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_BOOST_LIM_MASK),
				       (unsigned char) (CON2_BOOST_LIM_SHIFT)
				      );
}

void sgm41516d_set_ichg(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_ICHG_MASK),
				       (unsigned char) (CON2_ICHG_SHIFT)
				      );
}

/* CON3---------------------------------------------------- */

void sgm41516d_set_iprechg(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON3),
				       (unsigned char) (val),
				       (unsigned char) (CON3_IPRECHG_MASK),
				       (unsigned char) (CON3_IPRECHG_SHIFT)
				      );
}

void sgm41516d_set_iterm(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON3),
				       (unsigned char) (val),
				       (unsigned char) (CON3_ITERM_MASK),
				       (unsigned char) (CON3_ITERM_SHIFT)
				      );
}

/* CON4---------------------------------------------------- */

void sgm41516d_set_vreg(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_VREG_MASK),
				       (unsigned char) (CON4_VREG_SHIFT)
				      );
}

void sgm41516d_set_topoff_timer(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_TOPOFF_TIMER_MASK),
				       (unsigned char) (CON4_TOPOFF_TIMER_SHIFT)
				      );

}


void sgm41516d_set_vrechg(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_VRECHG_MASK),
				       (unsigned char) (CON4_VRECHG_SHIFT)
				      );
}

/* CON5---------------------------------------------------- */

void sgm41516d_set_en_term(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_EN_TERM_MASK),
				       (unsigned char) (CON5_EN_TERM_SHIFT)
				      );
}



void sgm41516d_set_watchdog(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_WATCHDOG_MASK),
				       (unsigned char) (CON5_WATCHDOG_SHIFT)
				      );
}

void sgm41516d_set_en_timer(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_EN_TIMER_MASK),
				       (unsigned char) (CON5_EN_TIMER_SHIFT)
				      );
}

void sgm41516d_set_chg_timer(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_CHG_TIMER_MASK),
				       (unsigned char) (CON5_CHG_TIMER_SHIFT)
				      );
}

void sgm41516d_set_chg_thermal(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_TREG_MASK),
				       (unsigned char) (CON5_TREG_SHIFT)
				      );
}

/* CON6---------------------------------------------------- */

void sgm41516d_set_treg(struct sgm41516d_info *info, unsigned int val)
{
#if 0
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_BOOSTV_MASK),
				       (unsigned char) (CON6_BOOSTV_SHIFT)
				      );
#endif
}

void sgm41516d_set_vindpm(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_VINDPM_MASK),
				       (unsigned char) (CON6_VINDPM_SHIFT)
				      );
}

unsigned char sgm41516d_get_vindpm(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON6),
				       &val,
				       (unsigned char) (CON6_VINDPM_MASK),
				       (unsigned char) (CON6_VINDPM_SHIFT)
				      );
	return val;
}


void sgm41516d_set_ovp(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_OVP_MASK),
				       (unsigned char) (CON6_OVP_SHIFT)
				      );

}

void sgm41516d_set_boostv(struct sgm41516d_info *info, unsigned int val)
{

	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_BOOSTV_MASK),
				       (unsigned char) (CON6_BOOSTV_SHIFT)
				      );
}



/* CON7---------------------------------------------------- */

void sgm41516d_set_tmr2x_en(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_TMR2X_EN_MASK),
				       (unsigned char) (CON7_TMR2X_EN_SHIFT)
				      );
}

void sgm41516d_set_batfet_disable(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON7),
				(unsigned char) (val),
				(unsigned char) (CON7_BATFET_Disable_MASK),
				(unsigned char) (CON7_BATFET_Disable_SHIFT)
				);
}


void sgm41516d_set_batfet_delay(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_BATFET_DLY_MASK),
				       (unsigned char) (CON7_BATFET_DLY_SHIFT)
				      );
}

void sgm41516d_set_batfet_reset_enable(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON7),
				(unsigned char) (val),
				(unsigned char) (CON7_BATFET_RST_EN_MASK),
				(unsigned char) (CON7_BATFET_RST_EN_SHIFT)
				);
}


/* CON8---------------------------------------------------- */

unsigned int sgm41516d_get_system_status(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON8),
				     (&val), (unsigned char) (0xFF),
				     (unsigned char) (0x0)
				    );
	return val;
}

unsigned int sgm41516d_get_vbus_stat(struct sgm41516d_info *info)
{
	unsigned int i, j, ret = 0;
	unsigned char val = 0;
	const int retry_count = 5;
	unsigned int unknown_count = 0,sdp_count = 0,cdp_count = 0,
					dcp_count = 0,sdpnstd_count = 0,otg_count = 0;
	struct chg_type_record sgm41516d_chg_type_record[] = {
			{SGM41516D_CHG_TYPE_SDP, sdp_count},
			{SGM41516D_CHG_TYPE_CDP, cdp_count},
			{SGM41516D_CHG_TYPE_DCP, dcp_count},
			{SGM41516D_CHG_TYPE_SDPNSTD, sdpnstd_count},
			{SGM41516D_CHG_TYPE_OTG, otg_count},
			{SGM41516D_CHG_TYPE_UNKNOWN, unknown_count},
	};

	for (i = 0; i < retry_count; i++){
		ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON8),
					     (&val),
					     (unsigned char) (CON8_VBUS_STAT_MASK),
					     (unsigned char) (CON8_VBUS_STAT_SHIFT)
					    );
		for (j = 0; j < GETARRAYNUM(sgm41516d_chg_type_record); j++) {
			if (val == sgm41516d_chg_type_record[j].chg_type){
				sgm41516d_chg_type_record[j].chg_type_count ++;
				if (sgm41516d_chg_type_record[j].chg_type_count >= retry_count-2)
					return sgm41516d_chg_type_record[j].chg_type;
				break;
			}
		}
	}
	return SGM41516D_CHG_TYPE_NOVBUS;
}

unsigned int sgm41516d_get_chrg_stat(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON8),
				     (&val),
				     (unsigned char) (CON8_CHRG_STAT_MASK),
				     (unsigned char) (CON8_CHRG_STAT_SHIFT)
				    );
	return val;
}

unsigned int sgm41516d_get_vsys_stat(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON8),
				     (&val),
				     (unsigned char) (CON8_VSYS_STAT_MASK),
				     (unsigned char) (CON8_VSYS_STAT_SHIFT)
				    );
	return val;
}

unsigned int sgm41516d_get_pg_stat(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON8),
				     (&val),
				     (unsigned char) (CON8_PG_STAT_MASK),
				     (unsigned char) (CON8_PG_STAT_SHIFT)
				    );
	return val;
}


/*CON10----------------------------------------------------------*/

void sgm41516d_set_int_mask(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON10),
				       (unsigned char) (val),
				       (unsigned char) (CON10_INT_MASK_MASK),
				       (unsigned char) (CON10_INT_MASK_SHIFT)
				      );
}

/*CON13 for SMG41516-----------------------------------------------------*/
void sgm41516d_set_dp(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON13),
		(unsigned char) (val),
		(unsigned char) (CON13_REG_DP_VSET_MASK),
		(unsigned char) (CON13_REG_DP_VSET_SHIFT)
	);
}

void sgm41516d_set_dm(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON13),
		(unsigned char) (val),
		(unsigned char) (CON13_REG_DM_VSET_MASK),
		(unsigned char) (CON13_REG_DM_VSET_SHIFT)
	);
}

/*CON15 for SMG41516-----------------------------------------------------*/
void sgm41516d_set_vindpm_os(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON15),
		(unsigned char) (val),
		(unsigned char) (CON15_REG_VINDPM_OS_MASK),
		(unsigned char) (CON15_REG_VINDPM_OS_SHIFT)
	);
}

unsigned int sgm41516d_get_vindpm_os(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON15),
				     (&val),
				     (unsigned char) (CON15_REG_VINDPM_OS_MASK),
				     (unsigned char) (CON15_REG_VINDPM_OS_SHIFT)
				    );
	return val;
}

void sgm41516d_set_cv_fine_tuning(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON15),
		(unsigned char) (val),
		(unsigned char) (CON15_REG_FINE_TUNING_MASK),
		(unsigned char) (CON15_REG_FINE_TUNING_SHIFT)
	);
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm41516d_dump_register(struct charger_device *chg_dev)
{
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned char sgm41516d_reg[sgm41516d_REG_NUM] = { 0 };

	pr_info("[sgm41516d] ");
	for (i = 0; i < sgm41516d_REG_NUM; i++) {
		ret = sgm41516d_read_byte(info, i, &sgm41516d_reg[i]);
		if (ret == 0) {
			pr_info("[sgm41516d] i2c transfor error\n");
			return 1;
		}
		pr_info("[0x%x]=0x%x ", i, sgm41516d_reg[i]);
	}
	pr_debug("\n");

	return 0;
}


/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/


static int sgm41516d_enable_charging(struct charger_device *chg_dev,
				   bool en)
{
	int status = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", en);
	if (en) {
		/* enable charging */
		sgm41516d_set_en_hiz(info, false);
		sgm41516d_set_chg_config(info, en);
	} else {
		/* disable charging */
		sgm41516d_set_chg_config(info, en);
		pr_info("[charging_enable] under test mode: disable charging\n");
	}

	return status;
}

static int sgm41516d_get_current(struct charger_device *chg_dev,
			       u32 *ichg)
{
	unsigned char ret_val = 0;
	unsigned int ret = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", *ichg);
	/* Get current level */
	ret=sgm41516d_read_interface(info, sgm41516d_CON2, &ret_val, CON2_ICHG_MASK,
			       CON2_ICHG_SHIFT);

	/* Parsing */
	*ichg = CS_VTH[ret_val]*10;

	return ret;
}

static int sgm41516d_set_current(struct charger_device *chg_dev,
			       u32 current_value)
{
	unsigned int status = true;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned int register_value;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("&&&& charge_current_value = %d\n", current_value);
	current_value /= 10;
	array_size = GETARRAYNUM(CS_VTH);
	set_chr_current = bmt_find_closest_level(CS_VTH, array_size,
			  current_value);
	register_value = charging_parameter_to_value(CS_VTH, array_size,
			 set_chr_current);
	//pr_info("&&&& charge_register_value = %d\n",register_value);
	pr_info("&&&& %s register_value = %d\n", __func__,
		register_value);
	sgm41516d_set_ichg(info, register_value);

	return status;
}

static int sgm41516d_get_input_current(struct charger_device *chg_dev,
				     u32 *aicr)
{
	int ret = 0;
	unsigned char val = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	ret=sgm41516d_read_interface(info, sgm41516d_CON0, &val, CON0_IINLIM_MASK,
			       CON0_IINLIM_SHIFT);
	*aicr = INPUT_CS_VTH[val]*10;
	pr_info("read value = %d\n", *aicr);
	return ret;
}


static int sgm41516d_set_input_current(struct charger_device *chg_dev,
				     u32 current_value)
{
	unsigned int status = true;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned int register_value;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", current_value);
	current_value /= 10;
	pr_info("&&&& current_value = %d\n", current_value);
	array_size = GETARRAYNUM(INPUT_CS_VTH);
	set_chr_current = bmt_find_closest_level(INPUT_CS_VTH, array_size,
			  current_value);
	register_value = charging_parameter_to_value(INPUT_CS_VTH, array_size,
			 set_chr_current);
	pr_info("&&&& %s register_value = %d\n", __func__,
		register_value);
	sgm41516d_set_iinlim(info, register_value);

	return status;
}

static int sgm41516d_set_cv_voltage(struct charger_device *chg_dev,
				  u32 cv)
{
	unsigned int status = true;
	unsigned int array_size;
	unsigned int set_cv_voltage;
	unsigned short register_value;
	unsigned int smg41516_fine=0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", cv);

	array_size = GETARRAYNUM(VBAT_CV_VTH);
	set_cv_voltage = bmt_find_closest_level(VBAT_CV_VTH, array_size, cv);
	register_value = charging_parameter_to_value(VBAT_CV_VTH, array_size,set_cv_voltage);

	if (g_chg_info == SGM41516 || g_chg_info == SGM41516D) {
		if (cv == SPECIAL_CV_VAL) {
			register_value=SPECIAL_CV_BIT;
			smg41516_fine=CV_NORMAL;
			goto out;
		}
		smg41516_fine=((cv-CV_OFFSET)%VBAT_CV_FINE[0])/VBAT_CV_FINE[1];
		switch (smg41516_fine) {
		case 0:
			smg41516_fine=CV_NORMAL;
			break;
		case 1:
			smg41516_fine=CV_POST_8MV;
			break;
		case 2:
			register_value+=1;
			smg41516_fine=CV_NEG_16MV;
			break;
		case 3:
			register_value+=1;
			smg41516_fine=CV_NEG_8MV;
			break;
		default:
			break;
		}
	}

out:
	sgm41516d_set_vreg(info, register_value);

	if (g_chg_info == SGM41516 || g_chg_info == SGM41516D)
		sgm41516d_set_cv_fine_tuning(info, smg41516_fine);
	pr_info("cv reg value = %d %d %d smg41516_fine=%d\n", register_value,
			cv, set_cv_voltage, smg41516_fine);

	return status;
}

static int sgm41516d_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	unsigned int status = true;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("charging_reset_watch_dog_timer\n");

	sgm41516d_set_wdt_rst(info, true);	/* Kick watchdog */

	//sgm41516d_set_watchdog(info, WDT_160S);

	return status;
}

static int sgm41516d_set_vindpm_voltage(struct charger_device *chg_dev,
				      u32 vindpm)
{
	int status = 0;
	unsigned int vindpm_os=0;
	unsigned int set_value=0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", vindpm);

	if(g_chg_info!=SGM41516 && g_chg_info!=SGM41516D)
		return status;

	vindpm /= 1000;
	if(vindpm<=VINDPM_OS0_MIVR_MIN)
		return status;
	if(vindpm>=VINDPM_OS3_MIVR_MAX)
		vindpm=VINDPM_OS3_MIVR_MAX;

	switch (vindpm) {
	case 3900 ... 5400:
		vindpm_os=0;
		set_value=sgm41516d_closest_reg(VINDPM_OS0_MIVR_MIN,
										VINDPM_OS0_MIVR_MAX,
										VINDPM_OS_MIVR_STEP,
										vindpm);
		break;
	case 5500 ... 5800:
		vindpm_os=0;
		vindpm=VINDPM_OS0_MIVR_MAX;
		set_value=sgm41516d_closest_reg(VINDPM_OS0_MIVR_MIN,
										VINDPM_OS0_MIVR_MAX,
										VINDPM_OS_MIVR_STEP,
										vindpm);
		break;
	case 5900 ... 7400:
		vindpm_os=1;
		set_value=sgm41516d_closest_reg(VINDPM_OS1_MIVR_MIN,
										VINDPM_OS1_MIVR_MAX,
										VINDPM_OS_MIVR_STEP,
										vindpm);
		break;
	case 7500 ... 9000:
		vindpm_os=2;
		set_value=sgm41516d_closest_reg(VINDPM_OS2_MIVR_MIN,
										VINDPM_OS2_MIVR_MAX,
										VINDPM_OS_MIVR_STEP,
										vindpm);
		break;
	case 10000 ... 10400:
		vindpm_os=2;
		vindpm=VINDPM_OS2_MIVR_MAX;
		set_value=sgm41516d_closest_reg(VINDPM_OS2_MIVR_MIN,
										VINDPM_OS2_MIVR_MAX,
										VINDPM_OS_MIVR_STEP,
										vindpm);
		break;
	case 10500 ... 12000:
		vindpm_os=3;
		set_value=sgm41516d_closest_reg(VINDPM_OS3_MIVR_MIN,
										VINDPM_OS3_MIVR_MAX,
										VINDPM_OS_MIVR_STEP,
										vindpm);
		break;
	default:
		return false;
	}
	pr_info("%s vindpm =%d vindpm_os=%d set_value=%d\r\n", __func__,
			vindpm, vindpm_os, set_value);

	sgm41516d_set_vindpm_os(info, vindpm_os);
	sgm41516d_set_vindpm(info, set_value);
	return status;
}

static int sgm41516d_get_mivr_state(struct charger_device *chg_dev, bool *in_loop)
{
	unsigned int ret = 0,is_in_vindpm = 0, i = 0;
	unsigned char val = 0;
	const int retry_count = 5;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);

	for (i = 0;i < retry_count; i++){
		ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON10),
			(&val),
			(unsigned char) (CON10_VINDPM_STAT_MASK),
			(unsigned char) (CON10_VINDPM_STAT_SHIFT)
			);
		if (val == 0x1)
			is_in_vindpm++;
	}
	pr_info("pass value = 0x%x\n", val);
	if (is_in_vindpm >= retry_count-1)
		return 1;
	else
		return 0;

}

static int sgm41516d_get_mivr(struct charger_device *chg_dev, u32 *uV)
{
	unsigned char val = 0;
	unsigned char vindpm_bit = 0;
	unsigned int vindpm_offset = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	if(g_chg_info!=SGM41516 && g_chg_info!=SGM41516D)
		return -1;

	val=sgm41516d_get_vindpm_os(info);
	vindpm_bit=sgm41516d_get_vindpm(info);
	switch (val) {
	case 0:
		vindpm_offset=VINDPM_OS0_MIVR_MIN;
		break;
	case 1:
		vindpm_offset=VINDPM_OS1_MIVR_MIN;
		break;
	case 2:
		vindpm_offset=VINDPM_OS2_MIVR_MIN;
		break;
	case 3:
		vindpm_offset=VINDPM_OS3_MIVR_MIN;
		break;
	default:
		return false;
	};

	*uV=(vindpm_offset+VINDPM_OS_MIVR_STEP*vindpm_bit)*1000;
	pr_info("%s val =%d vindpm_bit=%d vindpm_offset=%d *uV=%d\r\n",
			__func__, val,vindpm_bit,vindpm_offset,*uV);
	return true;
}

static int sgm41516d_get_charging_status(struct charger_device *chg_dev,
				       bool *is_done)
{
	unsigned int status = true;
	unsigned int ret_val, i;
	unsigned int is_done_count = 0;
	const int retry_count = 5;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	for (i = 0;i < retry_count; i++){
		ret_val = sgm41516d_get_chrg_stat(info);
		if (ret_val == 0x3)
			is_done_count++;
	}
	if (is_done_count >= retry_count-1)
		*is_done = true;
	else
		*is_done = false;

	return status;
}

static int sgm41516d_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s en = %d\n", __func__, en);
	if (en) {
		sgm41516d_set_chg_config(info, false);
		sgm41516d_set_otg_config(info, en);
		sgm41516d_set_watchdog(info, DISABLE_WDT);	/*default WDT_160S WDT 160s*/
		sgm41516d_set_pfm(info, 0x0);//enable pfm
	} else {
		sgm41516d_set_otg_config(info, en);
		sgm41516d_set_chg_config(info, true);
		sgm41516d_set_watchdog(info, DISABLE_WDT);
		sgm41516d_set_pfm(info, 0x1);//disable pfm
	}
	return ret;
}

static int sgm41516d_set_boost_current_limit(struct charger_device
		*chg_dev, u32 uA)
{
	int ret = 0;
	u32 array_size = 0;
	u32 boost_ilimit = 0;
	u8 boost_reg = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", uA);
	uA /= 1000;
	array_size = ARRAY_SIZE(BOOST_CURRENT_LIMIT);
	boost_ilimit = bmt_find_closest_level(BOOST_CURRENT_LIMIT, array_size,
					      uA);
	boost_reg = charging_parameter_to_value(BOOST_CURRENT_LIMIT,
						array_size, boost_ilimit);
	sgm41516d_set_boost_lim(info, boost_reg);

	return ret;
}

static int sgm41516d_enable_safetytimer(struct charger_device *chg_dev,
				      bool en)
{
	int status = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", en);
	if (en)
		sgm41516d_set_en_timer(info, 0x1);
	else
		sgm41516d_set_en_timer(info, 0x0);
	return status;
}

static int sgm41516d_get_is_safetytimer_enable(struct charger_device
		*chg_dev, bool *en)
{
	unsigned char val = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	sgm41516d_read_interface(info, sgm41516d_CON5, &val, CON5_EN_TIMER_MASK,
							CON5_EN_TIMER_SHIFT);
	*en = (bool)val;
	pr_info("pass value = %d\n", val);
	return val;
}

void sgm41516d_force_dpdm_enable(struct sgm41516d_info *info, unsigned int val)
{
	unsigned int ret = 0;

	ret = sgm41516d_config_interface(info, (unsigned char) (sgm41516d_CON7),
		(unsigned char) (val),
		(unsigned char) (CON7_FORCE_DPDM_MASK),
		(unsigned char) (CON7_FORCE_DPDM_SHIFT)
	);
}

unsigned int sgm41516d_get_iidet_status(struct sgm41516d_info *info)
{
	unsigned char val = 0;
	unsigned int ret = 0;

	ret = sgm41516d_read_byte(info, sgm41516d_CON7, &val);
	val &= (CON7_FORCE_DPDM_MASK << CON7_FORCE_DPDM_SHIFT);
	val = (val >> CON7_FORCE_DPDM_SHIFT);

	return val;
}

static int mtk_ext_chgdet(struct charger_device *chg_dev)
{
	unsigned int usb_status = 0,bq_detect_count=0;
	unsigned int iidet_bit=1;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	sgm41516d_dump_register(chg_dev);
	pr_info( "kernel_sgm41516d_chg_type detect]\n");
	Charger_Detect_Init();

	bq_chg_type = CHARGER_UNKNOWN;

	sgm41516d_force_dpdm_enable(info, 1);
	//usb_status = sgm41516d_get_vbus_stat();
	//pr_info("%s: usb_stats1 = 0x%X\n", __func__, usb_status);
	do{
		msleep(50);
		iidet_bit =sgm41516d_get_iidet_status(info);
		bq_detect_count++;
		pr_info("%s: count_max=%d,iidet_bit=%d,usb_status =%d\n",
				__func__,bq_detect_count,iidet_bit,
				sgm41516d_get_vbus_stat(info));
		if(bq_detect_count>BQ_DET_COUNT_MAX)
			iidet_bit = 0;
	}while (iidet_bit);
	usb_status = sgm41516d_get_vbus_stat(info);
	pr_info("%s: usb_stats2 = 0x%X\n", __func__, usb_status);

	switch (usb_status) {
	case SGM41516D_CHG_TYPE_SDP:
		bq_chg_type = STANDARD_HOST;
		break;
	case SGM41516D_CHG_TYPE_UNKNOWN:
		bq_chg_type = NONSTANDARD_CHARGER;
		break;
	case SGM41516D_CHG_TYPE_CDP:
		bq_chg_type = CHARGING_HOST;
		break;
	case SGM41516D_CHG_TYPE_DCP:
		bq_chg_type = STANDARD_CHARGER;
		break;
	case SGM41516D_CHG_TYPE_SDPNSTD:
		bq_chg_type = APPLE_2_1A_CHARGER;
		break;
	default:
		bq_chg_type = CHARGER_UNKNOWN;
		break;
	}
	pr_info("%s: bq_chg_type = %d\n", __func__, bq_chg_type);
	Charger_Detect_Release();
	return bq_chg_type;
}

static int sgm41516d_set_term_current(struct charger_device *chg_dev, u32 current_value)
{
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);
	unsigned int iterm_reg = 0x2;

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", current_value);
	if (current_value > 960000)
		iterm_reg = 0xF; /*Termination current = 960ma */
	else if (current_value < 60000)
		iterm_reg = 0x0;        /*Termination current = 60ma */
	else
		iterm_reg = (current_value/60000)-1;

	sgm41516d_set_iterm(info, iterm_reg);
	return 0;
}

static unsigned int charging_hw_init(struct sgm41516d_info *info)
{
	unsigned int status = 0;

	sgm41516d_set_en_hiz(info, false);
	sgm41516d_set_vindpm(info, 0x6);	/* VIN DPM check 4.5V */
	sgm41516d_set_wdt_rst(info, true);	/* Kick watchdog */
	sgm41516d_set_sys_min(info, 0x5);	/* Minimum system voltage 3.5V */
	sgm41516d_set_iprechg(info, 0x8);	/* Precharge current 540mA */
	sgm41516d_set_iterm(info, 0x2);	/* Termination current 180mA */
	sgm41516d_set_vreg(info, 0x11);	/* VREG 4.4V */
	sgm41516d_set_pfm(info, 0x1);//disable pfm
	sgm41516d_set_rdson(info, 0x0);     /*close rdson*/
	sgm41516d_set_batlowv(info, 0x1);	/* BATLOWV 3.0V */
	sgm41516d_set_vrechg(info, 0x0);	/* VRECHG 0.1V */
	sgm41516d_set_en_term(info, 0x1);	/* Enable termination */
	sgm41516d_set_watchdog(info, 0x3);	/* WDT 160s */
	sgm41516d_set_en_timer(info, 0x1);	/* Enable charge timer */
	sgm41516d_set_int_mask(info, 0x0);	/* Disable fault interrupt */
	sgm41516d_set_batfet_reset_enable(info, 0x0);/*disable batfet*/
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT) \
		|| defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT)
	sgm41516d_set_ovp(info, 0x2);
	sgm41516d_set_chg_thermal(info, 0x1);//SGM41516 thermal to 120
#endif
	sgm41516d_set_boostv(info,0x3);

	pr_info("%s: hw_init down!\n", __func__);
	return status;
}

static int sgm41516d_parse_dt(struct sgm41516d_info *info,
								struct device *dev)
{
	int ret = 0;
	struct device_node *np = dev->of_node;
	//int sgm41516d_en_pin = 0;

	pr_info("%s\n", __func__);
	if (!np) {
		pr_info("%s: no of node\n", __func__);
		return -ENODEV;
	}

	if (of_property_read_string(np, "charger_name",
								&info->chg_dev_name) < 0) {
		info->chg_dev_name = "primary_chg";
		pr_info("%s: no charger name\n", __func__);
	}

	if (of_property_read_string(np, "alias_name",
				&(info->chg_props.alias_name)) < 0) {
		info->chg_props.alias_name = "sgm41516d";
		pr_info("%s: no alias name\n", __func__);
	}

	if (strcmp(info->chg_dev_name, "primary_chg") == 0) {
		led_ctl_gpio = of_get_named_gpio(np, "led_ctl_gpio", 0);
		if (led_ctl_gpio < 0) {
			led_ctl_gpio = U32_MAX;
			pr_info("%s led_ctl_gpio=%d get fail\n", __func__,led_ctl_gpio);
		}
		if(led_ctl_gpio != U32_MAX) {
			ret = gpio_request(led_ctl_gpio, "led_ctl_gpio");
			if (ret < 0)
				pr_info("[%s]:led gpio request failed!\n", __func__);
			gpio_direction_output(led_ctl_gpio, 1);
		}
	}

	return 0;
}

static int sgm41516d_do_event(struct charger_device *chg_dev, u32 event,
			    u32 args)
{
	if (chg_dev == NULL)
		return -EINVAL;

	pr_info("%s: event = %d\n", __func__, event);
	switch (event) {
	case EVENT_EOC:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
		break;
	case EVENT_RECHARGE:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
		break;
	default:
		break;
	}

	return 0;
}


static int sgm41516d_set_pep20_efficiency_table(struct charger_device *chg_dev)
{
	struct charger_manager *chg_mgr = NULL;

	chg_mgr = charger_dev_get_drvdata(chg_dev);
	if (!chg_mgr)
		return -EINVAL;

	chg_mgr->pe2.profile[0].vchr = 9000000;
	chg_mgr->pe2.profile[1].vchr = 9000000;
	chg_mgr->pe2.profile[2].vchr = 9000000;
	chg_mgr->pe2.profile[3].vchr = 9000000;
	chg_mgr->pe2.profile[4].vchr = 9000000;
	chg_mgr->pe2.profile[5].vchr = 9000000;
	chg_mgr->pe2.profile[6].vchr = 9000000;
	chg_mgr->pe2.profile[7].vchr = 9000000;
	chg_mgr->pe2.profile[8].vchr = 9000000;
	chg_mgr->pe2.profile[9].vchr = 9000000;
	return 0;
}

struct timespec ptime[13];
static int cptime[13][2];

static int dtime(int i)
{
	struct timespec time;

	time = timespec_sub(ptime[i], ptime[i-1]);
	return time.tv_nsec/1000000;
}

#define PEOFFTIME 40
#define PEONTIME 90

static int sgm41516d_set_pep20_current_pattern(struct charger_device *chg_dev,
				u32 uV)
{
	int value;
	int i, j = 0;
	int flag;
	int errcnt = 0;

	sgm41516d_set_current(chg_dev,2000000);
	// bq25892_set_ico_en_start(0);

	mdelay(20);
	value = (uV - 5500000) / 500000;

	sgm41516d_set_input_current(chg_dev,0);
	mdelay(150);

	get_monotonic_boottime(&ptime[j++]);
	for (i = 4; i >= 0; i--) {
		flag = value & (1 << i);
		if (flag == 0) {
			sgm41516d_set_input_current(chg_dev,800000);
			mdelay(PEOFFTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				errcnt = 1;
				return -1;
			}
			j++;
			sgm41516d_set_input_current(chg_dev,0);
			mdelay(PEONTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				errcnt = 1;
				return -1;
			}
			j++;
		} else {
			sgm41516d_set_input_current(chg_dev,800000);
			mdelay(PEONTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				errcnt = 1;
				return -1;
			}
			j++;
			sgm41516d_set_input_current(chg_dev,0);
			mdelay(PEOFFTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				errcnt = 1;
				return -1;
			}
			j++;
		}
	}

	sgm41516d_set_input_current(chg_dev,800000);
	mdelay(200);
	get_monotonic_boottime(&ptime[j]);
	cptime[j][0] = 200;
	cptime[j][1] = dtime(j);
	if (cptime[j][1] < 180 || cptime[j][1] > 240) {
		errcnt = 1;
		return -1;
	}
	j++;

	sgm41516d_set_input_current(chg_dev,0);
	mdelay(150);
	sgm41516d_set_input_current(chg_dev,800000);

	//sgm41516d_set_input_current(chg_dev,2000000);

	mdelay(300);

	if (errcnt == 0)
		return 0;
	return -1;
}

int sgm41516d_enable_hiz_mode(struct charger_device *chg_dev, bool enable)
{
	int status = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	pr_info("pass value = %d\n", enable);
	sgm41516d_set_en_hiz(info, enable);
	return status;
}

int sgm41516d_get_dev_id(struct sgm41516d_info *info)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sgm41516d_read_interface(info, (unsigned char) (sgm41516d_CON11),
				     (&val),
				     (unsigned char) (CON11_PN_MASK),
				     (unsigned char) (CON11_PN_SHIFT)
	    );
	return val;
}

static int sgm41516d_reset_ta(struct charger_device *chg_dev)
{
	pr_info("%s enter!\n", __func__);
	sgm41516d_set_current(chg_dev,800000); //512mA

	sgm41516d_set_input_current(chg_dev,0);
	msleep(250);//250ms
	sgm41516d_set_input_current(chg_dev,2000000);

	return 1;
}

static int sgm41516d_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	sgm41516d_set_watchdog(info, WDT_160S);

	ret = sgm41516d_enable_charging(chg_dev, true);
	if (ret < 0) {
		pr_info( "%s en fail(%d)\n", __func__, ret);
		return ret;
	}
	return 0;
}

static int sgm41516d_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sgm41516d_info *info = dev_get_drvdata(&chg_dev->dev);

	pr_info("%s enter!\n", __func__);
	/* Disable charging */
	ret = sgm41516d_enable_charging(chg_dev, false);
	if (ret < 0) {
		pr_info("%s en chg fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = sgm41516d_enable_hiz_mode(chg_dev, false);
	if (ret < 0)
		pr_info("%s en hz fail(%d)\n", __func__, ret);

	/* Disable WDT */
	sgm41516d_set_watchdog(info, DISABLE_WDT);

#if defined(CONFIG_MTK_HVDCP20_SUPPORT)||defined(CONFIG_MTK_TC30_SUPPORT)
	sgm41516d_set_dp_dm_recover(chg_dev);
#endif

	return 0;
}

#if defined(CONFIG_MTK_PUMP_EXPRESS_50_SUPPORT)
static int sgm41516d_get_pwr_rdy_status(struct charger_device *chg_dev)
{
	pr_info("%s\n", __func__);
	return true;
}
#endif

static struct charger_ops sgm41516d_chg_ops = {
	.plug_in = sgm41516d_plug_in,
	.plug_out = sgm41516d_plug_out,
	/* Normal charging */
	.set_eoc_current = sgm41516d_set_term_current,
	.dump_registers = sgm41516d_dump_register,
	.enable = sgm41516d_enable_charging,
	.get_charging_current = sgm41516d_get_current,
	.set_charging_current = sgm41516d_set_current,
	.get_input_current = sgm41516d_get_input_current,
	.set_input_current = sgm41516d_set_input_current,
	/*.get_constant_voltage = sgm41516d_get_battery_voreg,*/
	.set_constant_voltage = sgm41516d_set_cv_voltage,
	.kick_wdt = sgm41516d_reset_watch_dog_timer,
	.set_mivr = sgm41516d_set_vindpm_voltage,
	.get_mivr = sgm41516d_get_mivr,
	.get_mivr_state = sgm41516d_get_mivr_state,
	.is_charging_done = sgm41516d_get_charging_status,

	/* Safety timer */
	.enable_safety_timer = sgm41516d_enable_safetytimer,
	.is_safety_timer_enabled = sgm41516d_get_is_safetytimer_enable,

	/* PE+20/HVDCP */
	.set_pe20_efficiency_table = sgm41516d_set_pep20_efficiency_table,
	.send_ta20_current_pattern = sgm41516d_set_pep20_current_pattern,
	.reset_ta = sgm41516d_reset_ta,
#if defined(CONFIG_MTK_HVDCP20_SUPPORT)
	/*.set_dm_0_6_v = sgm41516d_hvdcp20_set_dm_0_6_v,*/
	/*.set_dp_0_6_v = sgm41516d_hvdcp20_set_dp_0_6_v,*/
	/*.set_dp_3_0_v = sgm41516d_hvdcp20_set_dp_3_0_v,*/
	/*.set_dp_dm_recover = sgm41516d_set_dp_dm_recover,*/
#endif

#if defined(CONFIG_MTK_TC30_SUPPORT)
	/*.set_tc30_gpio_enable = sgm41516d_tc30_enable,*/
#endif

	.enable_hz = sgm41516d_enable_hiz_mode,
	.enable_powerpath = sgm41516d_enable_hiz_mode,
	.get_ext_chgtyp = mtk_ext_chgdet,
#if defined(CONFIG_MTK_PUMP_EXPRESS_50_SUPPORT)
	.get_pwr_rdy_status = sgm41516d_get_pwr_rdy_status,
#endif
	/* OTG */
	.enable_otg = sgm41516d_enable_otg,
	.set_boost_current_limit = sgm41516d_set_boost_current_limit,
	.event = sgm41516d_do_event,
	/*LED*/
	/*.set_stat_pin = sgm41516d_set_stat_pin_val,*/
};


static int sgm41516d_hw_component_detect(struct sgm41516d_info *info)
{
	unsigned char vendor_id = 0;

	vendor_id = sgm41516d_get_dev_id(info);
	pr_info("sgm41516d vendor_id=%d!!!\r\n",vendor_id);
	switch (vendor_id) {
	case bq25600d_VENDOR_ID:
		g_chg_info=BQ25601D;
		break;
	case bq25601_VENDOR_ID:
		g_chg_info=BQ25601;
		break;
	case SGM41516_VENDOR_ID:
		g_chg_info=SGM41516;
		break;
	case SGM41516D_VENDOR_ID:
		g_chg_info=SGM41516D;
		break;
	default:
			return false;
	}
	pr_info("sgm41516d g_chg_info=%d!!!\r\n",g_chg_info);
	return true;
}
static int sgm41516d_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct sgm41516d_info *info = NULL;

	pr_info("[%s]\n", __func__);

	info = devm_kzalloc(&client->dev, sizeof(struct sgm41516d_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &client->dev;
	info->client = client;
	i2c_set_clientdata(client, info);
	mutex_init(&info->sgm41516d_i2c_access);
	mutex_init(&info->sgm41516d_access_lock);

	ret = sgm41516d_parse_dt(info, &client->dev);
	if (ret < 0)
		goto err_parse_dt;

	if(!sgm41516d_hw_component_detect(info)){
		pr_info("sgm41516d component not exist!!!\r\n");
		goto err_parse_dt;
	}

	ret = charging_hw_init(info);
	if (ret < 0)
		goto err_parse_dt;

	/* Register charger device */
	info->chg_dev = charger_device_register(info->chg_dev_name,
						&client->dev, info,
						&sgm41516d_chg_ops,
						&info->chg_props);
	if (IS_ERR_OR_NULL(info->chg_dev)) {
		pr_info("%s: register charger device  failed\n", __func__);
		ret = PTR_ERR(info->chg_dev);
		goto err_register_chg_dev;
	}
	ret = device_create_file(&client->dev, &dev_attr_sgm41516d_access);
	if (ret < 0) {
		pr_info( "%s create file fail(%d)\n",
				      __func__, ret);
		goto err_create_file;
	}
	sgm41516d_dump_register(info->chg_dev);

	return 0;

err_create_file:
err_register_chg_dev:
	charger_device_unregister(info->chg_dev);
err_parse_dt:
	mutex_destroy(&info->sgm41516d_i2c_access);
	mutex_destroy(&info->sgm41516d_access_lock);
	devm_kfree(info->dev, info);
	return ret;
}


#ifdef CONFIG_OF
static const struct of_device_id sgm41516d_of_match[] = {
	{.compatible = "mediatek,sgm41516d"},
	{},
};
#else
static struct i2c_board_info i2c_sgm41516d __initdata = {
	I2C_BOARD_INFO("sgm41516d", (sgm41516d_SLAVE_ADDR_WRITE >> 1))
};
#endif

static struct i2c_driver sgm41516d_driver = {
	.driver = {
		.name = "sgm41516d",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sgm41516d_of_match,
#endif
	},
	.probe = sgm41516d_driver_probe,
	.id_table = sgm41516d_i2c_id,
};

static int __init sgm41516d_init(void)
{
	//int ret = 0;

	/* i2c registeration using DTS instead of boardinfo*/
#ifdef CONFIG_OF
	pr_info("[%s] init start with i2c DTS", __func__);
#else
	pr_info("[%s] init start. ch=%d\n", __func__, sgm41516d_BUSNUM);
	i2c_register_board_info(sgm41516d_BUSNUM, &i2c_sgm41516d, 1);
#endif
	if (i2c_add_driver(&sgm41516d_driver) != 0) {
		pr_info(
			"[%s] failed to register sgm41516d i2c driver.\n",
			__func__);
	} else {
		pr_info(
			"[%s] Success to register sgm41516d i2c driver.\n",
			__func__);
	}

	return 0;
}

static void __exit sgm41516d_exit(void)
{
	i2c_del_driver(&sgm41516d_driver);
}
module_init(sgm41516d_init);
module_exit(sgm41516d_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C sgm41516d Driver");
MODULE_AUTHOR("will cai <will.cai@mediatek.com>");
