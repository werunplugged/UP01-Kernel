#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <mtk_charger_intf.h>

/* NU2105 Registers */
#define NU2105_REG_BAT_OVP			(0x00)
#define NU2105_REG_BAT_OVP_ALM			(0x01)
#define NU2105_REG_BAT_OCP			(0x02)
#define NU2105_REG_BAT_OCP_ALM			(0x03)
#define NU2105_REG_BAT_UCP_ALM			(0x04)
#define NU2105_REG_AC_PROTECTION		(0x05)
#define NU2105_REG_BUS_OVP			(0x06)
#define NU2105_REG_BUS_OVP_ALM			(0x07)
#define NU2105_REG_BUS_OCP_UCP			(0x08)
#define NU2105_REG_BUS_OCP_ALM			(0x09)
#define NU2105_REG_CONVERTER_STATE		(0x0A)
#define NU2105_REG_CONTROL			(0x0B)
#define NU2105_REG_CHRG_CTRL			(0x0C)
#define NU2105_REG_INT_STAT			(0x0D)
#define NU2105_REG_INT_FLAG			(0x0E)
#define NU2105_REG_INT_MASK			(0x0F)
#define NU2105_REG_FLT_STAT			(0x10)
#define NU2105_REG_FLT_FLAG			(0x11)
#define NU2105_REG_FLT_MASK			(0x12)
#define NU2105_REG_PART_INFO			(0x13)
#define NU2105_REG_ADC_CTRL			(0x14)
#define NU2105_REG_ADC_FN_DISABLE		(0x15)
#define NU2105_REG_IBUS_ADC1			(0x16)
#define NU2105_REG_IBUS_ADC0			(0x17)
#define NU2105_REG_VBUS_ADC1			(0x18)
#define NU2105_REG_VBUS_ADC0			(0x19)
#define NU2105_REG_VAC_ADC1			(0x1A)
#define NU2105_REG_VAC_ADC0			(0x1B)
#define NU2105_REG_VOUT_ADC1			(0x1C)
#define NU2105_REG_VOUT_ADC0			(0x1D)
#define NU2105_REG_VBAT_ADC1			(0x1E)
#define NU2105_REG_VBAT_ADC0			(0x1F)
#define NU2105_REG_IBAT_ADC1			(0x20)
#define NU2105_REG_IBAT_ADC0			(0x21)
#define NU2105_REG_TSBUS_ADC1			(0x22)
#define NU2105_REG_TSBUS_ADC0			(0x23)
#define NU2105_REG_TSBAT_ADC1			(0x24)
#define NU2105_REG_TSBAT_ADC0			(0x25)
#define NU2105_REG_TDIE_ADC1			(0x26)
#define NU2105_REG_TDIE_ADC0			(0x27)
#define NU2105_REG_TSBUS_FLT1			(0x28)
#define NU2105_REG_TSBAT_FLT0			(0x29)
#define NU2105_REG_TDIE_ALM			(0x2A)
#define NU2105_REG_REG_CTRL			(0x2B)
#define NU2105_REG_REG_THRESHOLD		(0x2C)
#define NU2105_REG_REG_FLAG_MASK		(0x2D)
#define NU2105_REG_DEGLITCH			(0x2E)
#define NU2105_REG_CHGMODE			    (0x2F)

/* NU2105_REG_BAT_OVP */
#define NU2105_BAT_OVP_DIS_MASK		BIT(7)
#define NU2105_BAT_OVP_DIS_SHIFT		(7)
#define NU2105_BAT_OVP_MASK			(0x3F)

/* NU2105_REG_BAT_OVP_ALM */
#define NU2105_BAT_OVP_ALM_DIS_MASK		BIT(7)
#define NU2105_BAT_OVP_ALM_DIS_SHIFT		(7)
#define NU2105_BAT_OVP_ALM_MASK		(0x3F)

/* NU2105_REG_BAT_OCP */
#define NU2105_BAT_OCP_DIS_MASK		BIT(7)
#define NU2105_BAT_OCP_DIS_SHIFT		(7)
#define NU2105_BAT_OCP_MASK			(0x7F)

/* NU2105_REG_BAT_OCP_ALM */
#define NU2105_BAT_OCP_ALM_DIS_MASK		BIT(7)
#define NU2105_BAT_OCP_ALM_DIS_SHIFT		(7)
#define NU2105_BAT_OCP_ALM_MASK		(0x7F)

/* NU2105_REG_BAT_UCP_ALM */
#define NU2105_BAT_UCP_ALM_DIS_MASK		BIT(7)
#define NU2105_BAT_UCP_ALM_DIS_SHIFT		(7)
#define NU2105_BAT_UCP_ALM_MASK		(0x7F)

/* NU2105_REG_AC_PROTECTION */
#define NU2105_AC_OVP_STAT_MASK		BIT(7)
#define NU2105_AC_OVP_FLAG_MASK		BIT(6)
#define NU2105_AC_OVP_MASK			(0x07)

/* NU2105_REG_BUS_OVP */
#define NU2105_BUS_OVP_MASK			(0x7F)

/* NU2105_REG_BUS_OVP_ALM */
#define NU2105_BUS_OVP_ALM_MASK		(0x7F)
#define NU2105_BUS_OVP_ALM_DIS_MASK		BIT(7)
#define NU2105_BUS_OVP_ALM_DIS_SHIFT		(7)

/* NU2105_REG_BUS_OCP_UCP */
#define NU2105_IBUS_UCP_RISE_FLAG_MASK		BIT(6)
#define NU2105_IBUS_UCP_FALL_FLAG_MASK		BIT(4)
#define NU2105_BUS_OCP_MASK			(0x0F)

/* NU2105_REG_BUS_OCP_ALM */
#define NU2105_BUS_OCP_ALM_MASK		(0x7F)

/* NU2105_REG_CONVERTER_STATE */
#define NU2105_TSHUT_FLAG_MASK			BIT(7)
#define NU2105_TSHUT_STAT_MASK			BIT(6)
#define NU2105_VBUS_ERRORLO_STAT_MASK		BIT(5)
#define NU2105_VBUS_ERRORHI_STAT_MASK		BIT(4)
#define NU2105_SS_TIMEOUT_FLAG_MASK		BIT(3)
#define NU2105_CONV_SWITCHING_STAT_MASK	BIT(2)
#define NU2105_CONV_OCP_FLAG_MASK		BIT(1)
#define NU2105_PIN_DIAG_FAIL_FLAG_MASK		BIT(0)

/* NU2105_REG_CONTROL */
#define NU2105_FSW_SET_MASK			(0x70)
#define NU2105_FSW_SET_SHIFT			(4)
#define NU2105_WD_TIMEOUT_FLAG_MASK		BIT(3)
#define NU2105_WATCHDOG_DIS_MASK		BIT(2)
#define NU2105_WATCHDOG_MASK			(0x03)

/* NU2105_REG_CHRG_CTRL */
#define NU2105_CHG_EN_MASK			BIT(7)
#define NU2105_TSBUS_DIS_MASK			BIT(2)
#define NU2105_TSBAT_DIS_MASK			BIT(1)
#define NU2105_TDIE_DIS_MASK			BIT(0)

/* NU2105_REG_INT_STAT */
#define NU2105_BAT_OVP_ALM_STAT_MASK		BIT(7)
#define NU2105_BAT_OCP_ALM_STAT_MASK		BIT(6)
#define NU2105_BUS_OVP_ALM_STAT_MASK		BIT(5)
#define NU2105_BUS_OCP_ALM_STAT_MASK		BIT(4)
#define NU2105_BAT_UCP_ALM_STAT_MASK		BIT(3)
#define NU2105_ADAPTER_INSERT_STAT_MASK	BIT(2)
#define NU2105_VBAT_INSERT_STAT_MASK		BIT(1)
#define NU2105_ADC_DONE_STAT_MASK		BIT(0)

/* NU2105_REG_INT_FLAG */
#define NU2105_BAT_OVP_ALM_FLAG_MASK		BIT(7)
#define NU2105_BAT_OCP_ALM_FLAG_MASK		BIT(6)
#define NU2105_BUS_OVP_ALM_FLAG_MASK		BIT(5)
#define NU2105_BUS_OCP_ALM_FLAG_MASK		BIT(4)
#define NU2105_BAT_UCP_ALM_FLAG_MASK		BIT(3)
#define NU2105_ADAPTER_INSERT_FLAG_MASK	BIT(2)
#define NU2105_VBAT_INSERT_FLAG_MASK		BIT(1)
#define NU2105_ADC_DONE_FLAG_MASK		BIT(0)

/* NU2105_REG_FLT_STAT */
#define NU2105_BAT_OVP_FLT_STAT_MASK		BIT(7)
#define NU2105_BAT_OCP_FLT_STAT_MASK		BIT(6)
#define NU2105_BUS_OVP_FLT_STAT_MASK		BIT(5)
#define NU2105_BUS_OCP_FLT_STAT_MASK		BIT(4)
#define NU2105_TSBUS_TSBAT_ALM_STAT_MASK	BIT(3)
#define NU2105_TSBAT_FLT_STAT_MASK		BIT(2)
#define NU2105_TSBUS_FLT_STAT_MASK		BIT(1)
#define NU2105_TDIE_ALM_STAT_MASK		BIT(0)

/* NU2105_REG_FLT_FLAG */
#define NU2105_BAT_OVP_FLT_FLAG_MASK		BIT(7)
#define NU2105_BAT_OCP_FLT_FLAG_MASK		BIT(6)
#define NU2105_BUS_OVP_FLT_FLAG_MASK		BIT(5)
#define NU2105_BUS_OCP_FLT_FLAG_MASK		BIT(4)
#define NU2105_TSBUS_TSBAT_ALM_FLAG_MASK	BIT(3)
#define NU2105_TSBAT_FLT_FLAG_MASK		BIT(2)
#define NU2105_TSBUS_FLT_FLAG_MASK		BIT(1)
#define NU2105_TDIE_ALM_FLAG_MASK		BIT(0)

/* NU2105_REG_ADC_CTRL */
#define NU2105_ADC_EN_MASK			BIT(7)
#define NU2105_ADC_RATE_MASK			BIT(6)
#define NU2105_ADC_RATE_SHIFT			(6)
#define NU2105_IBUS_ADC_DIS_MASK		BIT(0)

/* NU2105_REG_ADC_FN_DISABLE */
#define NU2105_VBUS_ADC_DIS_MASK		BIT(7)
#define NU2105_VAC_ADC_DIS_MASK		BIT(6)
#define NU2105_VOUT_ADC_DIS_MASK		BIT(5)
#define NU2105_VBAT_ADC_DIS_MASK		BIT(4)
#define NU2105_IBAT_ADC_DIS_MASK		BIT(3)
#define NU2105_TSBUS_ADC_DIS_MASK		BIT(2)
#define NU2105_TSBAT_ADC_DIS_MASK		BIT(1)
#define NU2105_TDIE_ADC_DIS_MASK		BIT(0)

/* NU2105_REG_REG_CTRL */
#define NU2105_SS_TIMEOUT_SET_MASK		(0xE0)
#define NU2105_SS_TIMEOUT_SET_SHIFT		(5)
#define NU2105_SET_IBAT_SNS_RES_MASK		BIT(1)

/* NU2105_REG_REG_THRESHOLD */
#define NU2105_VBATREG_ACTIVE_STAT_MASK	BIT(3)
#define NU2105_IBATREG_ACTIVE_STAT_MASK	BIT(2)
#define NU2105_VDROP_OVP_STAT_MASK		BIT(1)
#define NU2105_VOUT_OVP_STAT_MASK		BIT(0)

/* NU2105_REG_REG_FLAG_MASK */
#define NU2105_VBATREG_ACTIVE_FLAG_MASK	BIT(7)
#define NU2105_IBATREG_ACTIVE_FLAG_MASK	BIT(6)
#define NU2105_VDROP_OVP_FLAG_MASK		BIT(5)
#define NU2105_VOUT_OVP_FLAG_MASK		BIT(4)

/* NU2105_REG_DEGLITCH */
#define NU2105_VBUS_ERROR_LO_DG_SET_MASK	BIT(4)
#define NU2105_IBUS_LOW_DG_SET_MASK		BIT(3)

#define NU2105_BAT_OVP_MIN_UV			(3475000)
#define NU2105_BAT_OVP_MAX_UV			(5050000)
#define NU2105_BAT_OVP_STEP_UV			(25000)
#define NU2105_BAT_OVP_ALM_MIN_UV		(3500000)
#define NU2105_BAT_OVP_ALM_MAX_UV		(5075000)
#define NU2105_BAT_OVP_ALM_STEP_UV		(25000)
#define NU2105_BAT_OCP_MIN_UA			(2000000)
#define NU2105_BAT_OCP_MAX_UA			(10000000)
#define NU2105_BAT_OCP_STEP_UA			(100000)
#define NU2105_BAT_OCP_ALM_MIN_UA		(2000000)
#define NU2105_BAT_OCP_ALM_MAX_UA		(14700000)
#define NU2105_BAT_OCP_ALM_STEP_UA		(100000)
#define NU2105_BAT_UCP_ALM_MIN_UA		(2000000)
#define NU2105_BAT_UCP_ALM_MAX_UA		(8300000)
#define NU2105_BAT_UCP_ALM_STEP_UA		(50000)
#define NU2105_AC_OVP_MIN_UV			(11000000)
#define NU2105_AC_OVP_MAX_UV			(17000000)
#define NU2105_AC_OVP_STEP_UV			(1000000)
#define NU2105_BUS_OVP_MIN_UV			(5950000)
#define NU2105_BUS_OVP_MAX_UV			(12300000)
#define NU2105_BUS_OVP_STEP_UV			(50000)
#define NU2105_BUS_OVP_ALM_MIN_UV		(6000000)
#define NU2105_BUS_OVP_ALM_MAX_UV		(12350000)
#define NU2105_BUS_OVP_ALM_STEP_UV		(50000)
#define NU2105_BUS_OCP_MIN_UA			(1000000)
#define NU2105_BUS_OCP_MAX_UA			(4750000)
#define NU2105_BUS_OCP_STEP_UA			(250000)
#define NU2105_BUS_OCP_ALM_MIN_UA		(0)
#define NU2105_BUS_OCP_ALM_MAX_UA		(6350000)
#define NU2105_BUS_OCP_ALM_STEP_UA		(50000)

#define NU2105_DEVID		0x08

static bool adc_con = true;
static bool irq_log = false;

int nu2105_exist = 0;
static u8 chg_id = 0;

enum {
	NU2105_MASTER,
	NU2105_SLAVE,
	NU2105_STANDALONE,
};

enum {
	NU2105_ADC_CONTINUOUS,
	NU2105_ADC_ONESHOT,
};

enum {
	NU2105_ADC_IBUS = 0,
	NU2105_ADC_VBUS,
	NU2105_ADC_VAC,
	NU2105_ADC_VOUT,
	NU2105_ADC_VBAT,
	NU2105_ADC_IBAT,
	NU2105_ADC_TSBUS,
	NU2105_ADC_TSBAT,
	NU2105_ADC_TDIE,
};

static const char *nu2105_adc_name[] = {
	[NU2105_ADC_IBUS] = "ibus",
	[NU2105_ADC_VBUS] = "vbus",
	[NU2105_ADC_VAC] = "vac",
	[NU2105_ADC_VOUT] = "vout",
	[NU2105_ADC_VBAT] = "vbat",
	[NU2105_ADC_IBAT] = "ibat",
	[NU2105_ADC_TSBUS] = "tsbus",
	[NU2105_ADC_TSBAT] = "tsbat",
	[NU2105_ADC_TDIE] = "tdie",
};

#define NU2105_IRQFLAG(x) NU2105_IRQFLAG_##x
#define NU2105_STAT(x) NU2105_STAT_##x
#define NU2105_IRQFLAG_STAT(x) NU2105_IRQFLAG(x), \
		NU2105_STAT(x) = NU2105_IRQFLAG(x)

enum {
	NU2105_IRQFLAG_STAT(AC_OVP),		/* index  0 */ /* Byte 0 */
	NU2105_IRQFLAG(IBUS_UCP_RISE),		/* index  1 */
	NU2105_IRQFLAG(IBUS_UCP_FALL),		/* index  2 */
	NU2105_IRQFLAG_STAT(TSHUT),		/* index  3 */
	NU2105_STAT(VBUS_ERRORLO),		/* index  4 */ /* Byte 1 */
	NU2105_STAT(VBUS_ERRORHI),		/* index  5 */
	NU2105_IRQFLAG(SS_TIMEOUT),		/* index  6 */
	NU2105_STAT(CONV_SWITCHING),		/* index  7 */
	NU2105_IRQFLAG(CONV_OCP),		/* index  8 */ /* Byte 2 */
	NU2105_IRQFLAG(PIN_DIAG_FAIL),		/* index  9 */
	NU2105_IRQFLAG(WD_TIMEOUT),		/* index 10 */
	NU2105_IRQFLAG_STAT(BAT_OVP_ALM),	/* index 11 */
	NU2105_IRQFLAG_STAT(BAT_OCP_ALM),	/* index 12 */ /* Byte 3 */
	NU2105_IRQFLAG_STAT(BUS_OVP_ALM),	/* index 13 */
	NU2105_IRQFLAG_STAT(BUS_OCP_ALM),	/* index 14 */
	NU2105_IRQFLAG_STAT(BAT_UCP_ALM),	/* index 15 */
	NU2105_IRQFLAG_STAT(ADAPTER_INSERT),	/* index 16 */ /* Byte 4 */
	NU2105_IRQFLAG_STAT(VBAT_INSERT),	/* index 17 */
	NU2105_IRQFLAG_STAT(ADC_DONE),		/* index 18 */
	NU2105_IRQFLAG_STAT(BAT_OVP_FLT),	/* index 19 */
	NU2105_IRQFLAG_STAT(BAT_OCP_FLT),	/* index 20 */ /* Byte 5 */
	NU2105_IRQFLAG_STAT(BUS_OVP_FLT),	/* index 21 */
	NU2105_IRQFLAG_STAT(BUS_OCP_FLT),	/* index 22 */
	NU2105_IRQFLAG_STAT(TSBUS_TSBAT_ALM),	/* index 23 */
	NU2105_IRQFLAG_STAT(TSBAT_FLT),	/* index 24 */ /* Byte 6 */
	NU2105_IRQFLAG_STAT(TSBUS_FLT),	/* index 25 */
	NU2105_IRQFLAG_STAT(TDIE_ALM),		/* index 26 */
	NU2105_IRQFLAG_STAT(VBATREG_ACTIVE),	/* index 27 */
	NU2105_IRQFLAG_STAT(IBATREG_ACTIVE),	/* index 28 */ /* Byte 7 */
	NU2105_IRQFLAG_STAT(VDROP_OVP),	/* index 29 */
	NU2105_IRQFLAG_STAT(VOUT_OVP),		/* index 30 */
};

static const u32 irqmask_default = ~(u32)BIT(NU2105_IRQFLAG_ADC_DONE);

struct nu2105_cfg {
	/* household */
	const char *chg_name;
	unsigned int ac_ovp;
	unsigned int bat_ucp_alm;
	unsigned int fsw_set;
	unsigned int ibat_sns_res;
	unsigned int ss_timeout;
	unsigned int ibus_low_dg_set;
	/* watchdog */
	bool watchdog_dis;
	unsigned int watchdog;
	/* protection */
	bool bat_ovp_dis;
	bool bat_ovp_alm_dis;
	bool bat_ocp_dis;
	bool bat_ocp_alm_dis;
	bool bat_ucp_alm_dis;
	bool bus_ovp_alm_dis;
	bool bus_ocp_dis;
	bool bus_ocp_alm_dis;
	bool tsbus_dis;
	bool tsbat_dis;
	bool tdie_dis;
	/* adc */
	u16 adc_fn_dis;
	/* irq */
	u32 irqmask;
};

struct nu2105 {
	struct i2c_client *client;
	struct device *dev;
	struct nu2105_cfg cfg;

	struct mutex rw_lock;
	struct mutex ops_lock;
	struct mutex adc_lock;

	u32 irqmask;
	struct completion adc_done;

	struct charger_device *chg_dev;
	struct charger_properties chg_prop;

	struct dentry *debugfs;
	u8 debug_addr;
};

static int __nu2105_read(struct nu2105 *chip, u8 reg, u8 *val)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		return ret;

	*val = (ret & 0xFF);

	return ret < 0 ? ret : 0;
}

static int __nu2105_write(struct nu2105 *chip, u8 reg, u8 val)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);

	return ret < 0 ? ret : 0;
}

static int __nu2105_update_bits(struct nu2105 *chip, u8 reg, u8 mask, u8 val)
{
	u8 tmp;
	s32 ret = 0;

	mutex_lock(&chip->rw_lock);

	ret = __nu2105_read(chip, reg, &tmp);
	if (ret < 0)
		goto out;

	tmp = (val & mask) | (tmp & (~mask));
	ret = __nu2105_write(chip, reg, tmp);

out:
	mutex_unlock(&chip->rw_lock);

	return ret;
}

static int __nu2105_set_bits(struct nu2105 *chip, u8 reg, u8 bits)
{
	return __nu2105_update_bits(chip, reg, bits, 0xFF);
}

static int __nu2105_clr_bits(struct nu2105 *chip, u8 reg, u8 bits)
{
	return __nu2105_update_bits(chip, reg, bits, 0);
}

static bool __nu2105_check_bits(struct nu2105 *chip, u8 reg, u8 bits)
{
	u8 val;
	int ret;

	ret = __nu2105_read(chip, reg, &val);
	if (ret < 0)
		return false;

	return (val & bits) ? true : false;
}

static int __nu2105_read_word(struct nu2105 *chip, u8 reg, u16 *val)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	*val = (u16)ret;

	return 0;
}

static int __nu2105_write_word(struct nu2105 *chip, u8 reg, u16 val)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	ret = i2c_smbus_write_word_data(client, reg, val);

	return ret < 0 ? ret : 0;
}
#if 1
static void __maybe_unused __nu2105_dump_register(struct nu2105 *chip)
{
	u8 reg, val;
	int ret;

	for (reg = NU2105_REG_BAT_OVP; reg <= NU2105_REG_CHGMODE; reg++) {
		ret = __nu2105_read(chip, reg, &val);
		if (ret < 0) {
			dev_info(chip->dev, "[DUMP] 0x%02x = error\n", reg);
			continue;
		}
		dev_info(chip->dev, "[DUMP] 0x%02x = 0x%02x\n", reg, val);
	}
}
#endif
static int __nu2105_set_bat_ovp(struct nu2105 *chip, unsigned int uV)
{
	u8 val = (uV - NU2105_BAT_OVP_MIN_UV)
			/ NU2105_BAT_OVP_STEP_UV;

	if (uV < NU2105_BAT_OVP_MIN_UV)
		val = 0x0;
	if (uV > NU2105_BAT_OVP_MAX_UV)
		val = 0x3F;
    dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, val);
	return __nu2105_update_bits(chip, NU2105_REG_BAT_OVP,
				     NU2105_BAT_OVP_MASK, val);
}

static int __nu2105_set_bat_ovp_alm(struct nu2105 *chip, unsigned int uV)
{
	u8 val = (uV - NU2105_BAT_OVP_ALM_MIN_UV)
			/ NU2105_BAT_OVP_ALM_STEP_UV;

	if (uV < NU2105_BAT_OVP_ALM_MIN_UV)
		val = 0x0;
	if (uV > NU2105_BAT_OVP_ALM_MAX_UV)
		val = 0x3F;
    	dev_dbg(chip->dev, "%s %d(0x%02X)\n", __func__, uV, val);
	return __nu2105_update_bits(chip, NU2105_REG_BAT_OVP_ALM,
				     NU2105_BAT_OVP_ALM_MASK, val);
}

static int __nu2105_set_bat_ocp(struct nu2105 *chip, unsigned int uA)
{
	u8 val = (uA - NU2105_BAT_OCP_MIN_UA)
			/ NU2105_BAT_OCP_STEP_UA;
	val += 500000;

	if (uA < NU2105_BAT_OCP_MIN_UA)
		val = 0x0;
	if (uA > NU2105_BAT_OCP_MAX_UA)
		val = 0x7F;
    	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, val);
	return __nu2105_update_bits(chip, NU2105_REG_BAT_OCP,
				     NU2105_BAT_OCP_MASK, val);
}

static int __nu2105_set_bat_ocp_alm(struct nu2105 *chip, unsigned int uA)
{
	u8 val = (uA - NU2105_BAT_OCP_ALM_MIN_UA)
			/ NU2105_BAT_OCP_ALM_STEP_UA;

	if (uA < NU2105_BAT_OCP_ALM_MIN_UA)
		val = 0x0;
	if (uA > NU2105_BAT_OCP_ALM_MAX_UA)
		val = 0x7F;
    	dev_dbg(chip->dev, "%s %d(0x%02X)\n", __func__, uA, val);
	return __nu2105_update_bits(chip, NU2105_REG_BAT_OCP_ALM,
				     NU2105_BAT_OCP_ALM_MASK, val);
}

static int __nu2105_set_bat_ucp_alm(struct nu2105 *chip, unsigned int uA)
{
	u8 val = (uA - NU2105_BAT_UCP_ALM_MIN_UA)
			/ NU2105_BAT_UCP_ALM_STEP_UA;

	if (uA < NU2105_BAT_UCP_ALM_MIN_UA)
		val = 0x0;
	if (uA > NU2105_BAT_UCP_ALM_MAX_UA)
		val = 0x7F;

	return __nu2105_update_bits(chip, NU2105_REG_BAT_UCP_ALM,
				     NU2105_BAT_UCP_ALM_MASK, val);
}

static int __nu2105_set_ac_ovp(struct nu2105 *chip, unsigned int uV)
{
	u8 val = (uV - NU2105_AC_OVP_MIN_UV) / NU2105_AC_OVP_STEP_UV;

	if (uV < NU2105_AC_OVP_MIN_UV)
		val = 0x7;
	if (uV > NU2105_AC_OVP_MAX_UV)
		val = 0x6;

	return __nu2105_update_bits(chip, NU2105_REG_AC_PROTECTION,
				     NU2105_AC_OVP_MASK, val);
}


static int __nu2105_set_bus_ovp(struct nu2105 *chip, unsigned int uV)
{
	u8 val = (uV - NU2105_BUS_OVP_MIN_UV)
			/ NU2105_BUS_OVP_STEP_UV;

	if (uV < NU2105_BUS_OVP_MIN_UV)
		val = 0x0;
	if (uV > NU2105_BUS_OVP_MAX_UV)
		val = 0x7F;

    	dev_dbg(chip->dev, "%s %d(0x%02X)\n", __func__, uV, val);
	return __nu2105_update_bits(chip, NU2105_REG_BUS_OVP,
				     NU2105_BUS_OVP_MASK, val);
}


static int __nu2105_set_bus_ovp_alm(struct nu2105 *chip, unsigned int uV)
{
	u8 val = (uV - NU2105_BUS_OVP_ALM_MIN_UV)
			/ NU2105_BUS_OVP_ALM_STEP_UV;

	if (uV < NU2105_BUS_OVP_ALM_MIN_UV)
		val = 0x0;
	if (uV > NU2105_BUS_OVP_ALM_MAX_UV)
		val = 0x7F;
    	dev_dbg(chip->dev, "%s %d(0x%02X)\n", __func__, uV, val);
	return __nu2105_update_bits(chip, NU2105_REG_BUS_OVP_ALM,
				     NU2105_BUS_OVP_ALM_MASK, val);
}

static int __nu2105_set_bus_ocp(struct nu2105 *chip, unsigned int uA)
{
	u8 val = (uA - NU2105_BUS_OCP_MIN_UA)
			/ NU2105_BUS_OCP_STEP_UA;

	if (uA < NU2105_BUS_OCP_MIN_UA)
		val = 0x0;
	if (uA > NU2105_BUS_OCP_MAX_UA)
		val = 0x0F;

	dev_dbg(chip->dev, "%s %d(0x%02X)\n", __func__, uA, val);

	return __nu2105_update_bits(chip, NU2105_REG_BUS_OCP_UCP,
				     NU2105_BUS_OCP_MASK, val);
}

static int __nu2105_set_bus_ocp_alm(struct nu2105 *chip, unsigned int uA)
{
	u8 val = (uA - NU2105_BUS_OCP_ALM_MIN_UA)
			/ NU2105_BUS_OCP_ALM_STEP_UA;

	if (uA < NU2105_BUS_OCP_ALM_MIN_UA)
		val = 0x0;
	if (uA > NU2105_BUS_OCP_ALM_MAX_UA)
		val = 0x7F;

    	dev_dbg(chip->dev, "%s %d(0x%02X)\n", __func__, uA, val);
	return __nu2105_update_bits(chip, NU2105_REG_BUS_OCP_ALM,
				     NU2105_BUS_OCP_ALM_MASK, val);
}

static int __nu2105_set_fsw(struct nu2105 *chip, unsigned int hz)
{
	const unsigned int fsw_set[] = {
		187500, 250000, 300000, 375000, 500000, 750000
	};
	u8 val;

	for (val = 0; val < ARRAY_SIZE(fsw_set) - 1; val++) {
		if (hz <= fsw_set[val])
			break;
	}

	return __nu2105_update_bits(chip, NU2105_REG_CONTROL,
				     NU2105_FSW_SET_MASK,
				     val << NU2105_FSW_SET_SHIFT);
}

static int __nu2105_enable_watchdog(struct nu2105 *chip, bool en)
{
	return (en ? __nu2105_clr_bits : __nu2105_set_bits)
			(chip, NU2105_REG_CONTROL, NU2105_WATCHDOG_DIS_MASK);
}

static int __nu2105_set_watchdog(struct nu2105 *chip, unsigned int usec)
{
	const unsigned int watchdog[] = {
		500000, 1000000, 5000000, 30000000
	};
	u8 val;

	for (val = 0; val < ARRAY_SIZE(watchdog) - 1; val++) {
		if (usec <= watchdog[val])
			break;
	}

	return __nu2105_update_bits(chip, NU2105_REG_CONTROL,
				     NU2105_WATCHDOG_MASK, val);
}

static bool __nu2105_is_chg_enabled(struct nu2105 *chip)
{
	return __nu2105_check_bits(chip, NU2105_REG_CHRG_CTRL,
				    NU2105_CHG_EN_MASK);
}

static int __nu2105_enable_chg(struct nu2105 *chip, bool en)
{
	return (en ? __nu2105_set_bits : __nu2105_clr_bits)
			(chip, NU2105_REG_CHRG_CTRL, NU2105_CHG_EN_MASK);
}

static bool __nu2105_is_adc_done(struct nu2105 *chip)
{
	return __nu2105_check_bits(chip, NU2105_REG_INT_STAT,
				    NU2105_ADC_DONE_STAT_MASK);
}

static bool __nu2105_is_adc_oneshot(struct nu2105 *chip)
{
	return __nu2105_check_bits(chip, NU2105_REG_ADC_CTRL,
				    NU2105_ADC_RATE_MASK);
}

static int __nu2105_set_adc_oneshot(struct nu2105 *chip, bool oneshot)
{
	return (oneshot ? __nu2105_set_bits : __nu2105_clr_bits)
			(chip, NU2105_REG_ADC_CTRL, NU2105_ADC_RATE_MASK);
}

static bool __nu2105_is_adc_enabled(struct nu2105 *chip)
{
	return __nu2105_check_bits(chip, NU2105_REG_ADC_CTRL,
				    NU2105_ADC_EN_MASK);
}

static int __nu2105_enable_adc(struct nu2105 *chip, bool en)
{
	return (en ? __nu2105_set_bits : __nu2105_clr_bits)
			(chip, NU2105_REG_ADC_CTRL, NU2105_ADC_EN_MASK);
}

static int __nu2105_set_adc_fn_dis(struct nu2105 *chip, u16 adc_fn_dis)
{
	u16 val = ((adc_fn_dis & 0xFF) << 8) | ((adc_fn_dis >> 8) & 0xFF);

	return __nu2105_write_word(chip, NU2105_REG_ADC_CTRL, val);
}

static int __nu2105_set_ss_timeout(struct nu2105 *chip, unsigned int us)
{
	const unsigned int timeout[] = {
		0, 12500, 25000, 50000, 100000, 400000, 1500000, 100000000
	};
	u8 val;

	for (val = 0; val < ARRAY_SIZE(timeout); val++) {
		if (us <= timeout[val])
			break;
	}

	return __nu2105_update_bits(chip, NU2105_REG_REG_CTRL,
				     NU2105_SS_TIMEOUT_SET_MASK,
				     val << NU2105_SS_TIMEOUT_SET_SHIFT);
}

static int __nu2105_set_ibat_sns_res(struct nu2105 *chip, unsigned int ohm)
{
	return ((ohm == 5) ? __nu2105_set_bits : __nu2105_clr_bits)
			(chip, NU2105_REG_REG_CTRL,
			 NU2105_SET_IBAT_SNS_RES_MASK);
}

static int __nu2105_set_ibus_low_dg_set(struct nu2105 *chip, unsigned int us)
{
	return ((us == 5000) ? __nu2105_set_bits : __nu2105_clr_bits)
			(chip, NU2105_REG_DEGLITCH,
			 NU2105_IBUS_LOW_DG_SET_MASK);
}

static int __nu2105_convert_adc(struct nu2105 *chip, int channel, u16 val)
{
	s16 sval = (s16)val;
	int conv_val = 0;

	switch (channel) {
	case NU2105_ADC_VBUS:
	case NU2105_ADC_VAC:
	case NU2105_ADC_VOUT:
	case NU2105_ADC_VBAT:
		/* in micro volt */
		conv_val = sval * 1000;
		dev_err(chip->dev, "%s adc: %duV (0x%04x)\n",
				nu2105_adc_name[channel], conv_val, val);
		break;
	case NU2105_ADC_IBUS:
	case NU2105_ADC_IBAT:
		/* in micro amp */
		conv_val = sval * 1000;
		dev_err(chip->dev, "%s adc: %duA (0x%04x)\n",
				nu2105_adc_name[channel], conv_val, val);
		break;
	case NU2105_ADC_TDIE:
		/* in degreeC */
		conv_val = (10 + sval) >> 1;
		//conv_val = sval >> 1;
		dev_err(chip->dev, "%s adc: %d.%cdegC (0x%04x)\n",
				nu2105_adc_name[channel],
				conv_val, (sval & 0x1 ? '5' : '0'), val);
		break;
	case NU2105_ADC_TSBAT:
	case NU2105_ADC_TSBUS:
		/* in percent */
		conv_val = sval * 100 / 1024;
		dev_err(chip->dev, "%s adc: %d%% (0x%04x)\n",
				nu2105_adc_name[channel], conv_val, val);
		break;
	}

	return conv_val;
}

static inline int __nu2105_wait_for_adc_done(struct nu2105 *chip)
{
	unsigned long ret;

	if (!__nu2105_is_adc_oneshot(chip)) {
		msleep(300);

		if (!__nu2105_is_adc_enabled(chip))
			return -EIO;

		return 0;
	}

	ret = wait_for_completion_timeout(&chip->adc_done,
			msecs_to_jiffies(300));
	if (ret == 0) {
		if (__nu2105_is_adc_done(chip))
			return 0;

		return -ETIMEDOUT;
	}

	return 0;
}

static int nu2105_get_adc_data(struct nu2105 *bq, int channel,  int *result)
{
	int ret;
	u16 temp;
	u8 val_l, val_h;

	if (channel < NU2105_ADC_IBUS || channel > NU2105_ADC_TDIE)
		return -EINVAL;

	usleep_range(12000,15000);
	switch (channel) {
	case NU2105_ADC_IBUS:
		ret = __nu2105_read(bq, 0x16, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x17, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_VBUS:
		ret = __nu2105_read(bq, 0x18, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x19, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_VAC:
		ret = __nu2105_read(bq, 0x1A, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x1B, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_VOUT:
		ret = __nu2105_read(bq, 0x1C, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x1D, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_VBAT:
		ret = __nu2105_read(bq, 0x1E, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x1F, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_IBAT:
		ret = __nu2105_read(bq, 0x20, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x21, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_TSBUS:
		ret = __nu2105_read(bq, 0x22, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x23, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_TSBAT:
		ret = __nu2105_read(bq, 0x24, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x25, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	case NU2105_ADC_TDIE:
		ret = __nu2105_read(bq, 0x26, &val_h);
	    if (ret)
		    return ret;

	    ret = __nu2105_read(bq, 0x27, &val_l);
	    if (ret < 0)
		    return ret;
		break;
	default:
		dev_err(bq->dev, "failed to enable adc\n");
		break;
	}

	//temp = val_l + (val_h << 8);
	temp = val_h*256 + val_l;

	*result = __nu2105_convert_adc(bq, channel, temp);

	dev_dbg(bq->dev, "get %s adc, reg: %2x, channel:%d, val:%4x\n",
				nu2105_adc_name[channel], (NU2105_REG_IBUS_ADC1 + (channel << 1)),
				channel, temp);

	return 0;
}


static int __nu2105_get_adc(struct nu2105 *chip, int channel, int *data)
{
	u16 val = 0;
	bool oneshot = false, wait_for_adc_done = false;
	int try;
	int ret;

	if (channel < NU2105_ADC_IBUS || channel > NU2105_ADC_TDIE)
		return -EINVAL;
    if (!adc_con)
    {
	mutex_lock(&chip->adc_lock);

	if (!adc_con)
	{
	if (!__nu2105_is_chg_enabled(chip))
		oneshot = true;

	for (try = 0; try < 5; try++) {
		if (oneshot || !__nu2105_is_adc_enabled(chip)) {
			wait_for_adc_done = true;
			reinit_completion(&chip->adc_done);
		}

		ret = __nu2105_set_adc_oneshot(chip, oneshot);
		if (ret) {
			dev_err(chip->dev, "failed to set adc rate\n");
			continue;
		}

		ret = __nu2105_enable_adc(chip, true);
		if (ret) {
			dev_err(chip->dev, "failed to enable adc\n");
			continue;
		}

		if (wait_for_adc_done) {
			ret = __nu2105_wait_for_adc_done(chip);
			if (ret) {
				dev_err(chip->dev, "error waiting adc done\n");
				continue;
			}
		}

		break;
	}

	if (!ret) {
		ret = __nu2105_read_word(chip,
				NU2105_REG_IBUS_ADC1 + (channel << 1), &val);
		if (ret < 0)
			dev_err(chip->dev, "failed to read adc\n");
	}

	/* adc running in continuous mode. left adc enabled */
	if (oneshot) {
		if (__nu2105_enable_adc(chip, false))
			dev_err(chip->dev, "failed to disable adc\n");
	}
	}
	else
	{
	    if (__nu2105_is_chg_enabled(chip))
	    {
	        ret = __nu2105_read_word(chip,
				    NU2105_REG_IBUS_ADC1 + (channel << 1), &val);
			dev_dbg(chip->dev, "get %s adc, reg: %2x, channel:%d, val:%4x\n",
				nu2105_adc_name[channel], (NU2105_REG_IBUS_ADC1 + (channel << 1)),
				channel,val);
	    }
		else
			dev_err(chip->dev, "charger is disable\n");
	}

	mutex_unlock(&chip->adc_lock);

	if (ret < 0) {
		dev_err(chip->dev, "failed to get %s adc (%d)\n",
				nu2105_adc_name[channel], ret);
		return ret;
	}

	val = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
	*data = __nu2105_convert_adc(chip, channel, val);
    }
	else
	{
	    mutex_lock(&chip->adc_lock);
	    nu2105_get_adc_data(chip, channel, data);
		mutex_unlock(&chip->adc_lock);
	}

	return 0;
}

static u32 __to_flag(u8 reg, u8 val)
{
	u32 flag = 0;

	switch (reg) {
	case NU2105_REG_AC_PROTECTION:
		if (val & NU2105_AC_OVP_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_AC_OVP);
		break;
	case NU2105_REG_BUS_OCP_UCP:
		if (val & NU2105_IBUS_UCP_RISE_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_IBUS_UCP_RISE);
		if (val & NU2105_IBUS_UCP_FALL_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_IBUS_UCP_FALL);
		break;
	case NU2105_REG_CONVERTER_STATE:
		if (val & NU2105_TSHUT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_TSHUT);
		if (val & NU2105_SS_TIMEOUT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_SS_TIMEOUT);
		if (val & NU2105_CONV_OCP_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_CONV_OCP);
		if (val & NU2105_PIN_DIAG_FAIL_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_PIN_DIAG_FAIL);
		break;
	case NU2105_REG_CONTROL:
		if (val & NU2105_WD_TIMEOUT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_WD_TIMEOUT);
		break;
	case NU2105_REG_INT_FLAG:
		if (val & NU2105_BAT_OVP_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BAT_OVP_ALM);
		if (val & NU2105_BAT_OCP_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BAT_OCP_ALM);
		if (val & NU2105_BUS_OVP_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BUS_OVP_ALM);
		if (val & NU2105_BUS_OCP_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BUS_OCP_ALM);
		if (val & NU2105_BAT_UCP_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BAT_UCP_ALM);
		if (val & NU2105_ADAPTER_INSERT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_ADAPTER_INSERT);
		if (val & NU2105_VBAT_INSERT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_VBAT_INSERT);
		if (val & NU2105_ADC_DONE_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_ADC_DONE);
		break;
	case NU2105_REG_FLT_FLAG:
		if (val & NU2105_BAT_OVP_FLT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BAT_OVP_FLT);
		if (val & NU2105_BAT_OCP_FLT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BAT_OCP_FLT);
		if (val & NU2105_BUS_OVP_FLT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BUS_OVP_FLT);
		if (val & NU2105_BUS_OCP_FLT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_BUS_OCP_FLT);
		if (val & NU2105_TSBUS_TSBAT_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_TSBUS_TSBAT_ALM);
		if (val & NU2105_TSBAT_FLT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_TSBAT_FLT);
		if (val & NU2105_TSBUS_FLT_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_TSBUS_FLT);
		if (val & NU2105_TDIE_ALM_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_TDIE_ALM);
		break;
	case NU2105_REG_REG_FLAG_MASK:
		if (val & NU2105_VBATREG_ACTIVE_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_VBATREG_ACTIVE);
		if (val & NU2105_IBATREG_ACTIVE_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_IBATREG_ACTIVE);
		if (val & NU2105_VDROP_OVP_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_VDROP_OVP);
		if (val & NU2105_VOUT_OVP_FLAG_MASK)
			flag |= BIT(NU2105_IRQFLAG_VOUT_OVP);
		break;
	}

	return flag;
}

static u32 __nu2105_get_flag(struct nu2105 *chip)
{
	const u8 reg[] = {
		NU2105_REG_AC_PROTECTION,
		NU2105_REG_BUS_OCP_UCP,
		NU2105_REG_CONVERTER_STATE,
		NU2105_REG_CONTROL,
		NU2105_REG_INT_FLAG,
		NU2105_REG_FLT_FLAG,
		NU2105_REG_REG_FLAG_MASK
	};
	u8 val;
	u32 flag = 0;
	unsigned int i;
	int ret;

	mutex_lock(&chip->rw_lock);

	for (i = 0; i < ARRAY_SIZE(reg); i++) {
		ret = __nu2105_read(chip, reg[i], &val);
		if (ret) {
			dev_err(chip->dev, "failed to read flag reg %02xh\n",
				reg[i]);
			continue;
		}
		flag |= __to_flag(reg[i], val);
	}

	mutex_unlock(&chip->rw_lock);

	return flag;
}

static u32 __to_stat(u8 reg, u8 val)
{
	u32 stat = 0;

	switch (reg) {
	case NU2105_REG_AC_PROTECTION:
		if (val & NU2105_AC_OVP_STAT_MASK)
			stat |= BIT(NU2105_STAT_AC_OVP);
		break;
	case NU2105_REG_CONVERTER_STATE:
		if (val & NU2105_TSHUT_STAT_MASK)
			stat |= BIT(NU2105_STAT_TSHUT);
		if (val & NU2105_VBUS_ERRORLO_STAT_MASK)
			stat |= BIT(NU2105_STAT_VBUS_ERRORLO);
		if (val & NU2105_VBUS_ERRORHI_STAT_MASK)
			stat |= BIT(NU2105_STAT_VBUS_ERRORHI);
		if (val & NU2105_CONV_SWITCHING_STAT_MASK)
			stat |= BIT(NU2105_STAT_CONV_SWITCHING);
		break;
	case NU2105_REG_INT_STAT:
		if (val & NU2105_BAT_OVP_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_BAT_OVP_ALM);
		if (val & NU2105_BAT_OCP_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_BAT_OCP_ALM);
		if (val & NU2105_BUS_OVP_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_BUS_OVP_ALM);
		if (val & NU2105_BUS_OCP_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_BUS_OCP_ALM);
		if (val & NU2105_BAT_UCP_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_BAT_UCP_ALM);
		if (val & NU2105_ADAPTER_INSERT_STAT_MASK)
			stat |= BIT(NU2105_STAT_ADAPTER_INSERT);
		if (val & NU2105_VBAT_INSERT_STAT_MASK)
			stat |= BIT(NU2105_STAT_VBAT_INSERT);
		if (val & NU2105_ADC_DONE_STAT_MASK)
			stat |= BIT(NU2105_STAT_ADC_DONE);
		break;
	case NU2105_REG_FLT_STAT:
		if (val & NU2105_BAT_OVP_FLT_STAT_MASK)
			stat |= BIT(NU2105_STAT_BAT_OVP_FLT);
		if (val & NU2105_BAT_OCP_FLT_STAT_MASK)
			stat |= BIT(NU2105_STAT_BAT_OCP_FLT);
		if (val & NU2105_BUS_OVP_FLT_STAT_MASK)
			stat |= BIT(NU2105_STAT_BUS_OVP_FLT);
		if (val & NU2105_BUS_OCP_FLT_STAT_MASK)
			stat |= BIT(NU2105_STAT_BUS_OCP_FLT);
		if (val & NU2105_TSBUS_TSBAT_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_TSBUS_TSBAT_ALM);
		if (val & NU2105_TSBAT_FLT_STAT_MASK)
			stat |= BIT(NU2105_STAT_TSBAT_FLT);
		if (val & NU2105_TSBUS_FLT_STAT_MASK)
			stat |= BIT(NU2105_STAT_TSBUS_FLT);
		if (val & NU2105_TDIE_ALM_STAT_MASK)
			stat |= BIT(NU2105_STAT_TDIE_ALM);
		break;
	case NU2105_REG_REG_THRESHOLD:
		if (val & NU2105_VBATREG_ACTIVE_STAT_MASK)
			stat |= BIT(NU2105_STAT_VBATREG_ACTIVE);
		if (val & NU2105_IBATREG_ACTIVE_STAT_MASK)
			stat |= BIT(NU2105_STAT_IBATREG_ACTIVE);
		if (val & NU2105_VDROP_OVP_STAT_MASK)
			stat |= BIT(NU2105_STAT_VDROP_OVP);
		if (val & NU2105_VOUT_OVP_STAT_MASK)
			stat |= BIT(NU2105_STAT_VOUT_OVP);
		break;
	}

	return stat;
}

static u32 __nu2105_get_stat(struct nu2105 *chip)
{
	const u8 reg[] = {
		NU2105_REG_AC_PROTECTION,
		NU2105_REG_CONVERTER_STATE,
		NU2105_REG_INT_STAT,
		NU2105_REG_FLT_STAT,
		NU2105_REG_REG_THRESHOLD
	};
	u8 val;
	u32 stat = 0;
	unsigned int i;
	int ret;

	mutex_lock(&chip->rw_lock);

	for (i = 0; i < ARRAY_SIZE(reg); i++) {
		ret = __nu2105_read(chip, reg[i], &val);
		if (ret) {
			dev_err(chip->dev, "failed to read stat "
				"reg 0x%02x\n", reg[i]);
			continue;
		}
		stat |= __to_stat(reg[i], val);
	}

	mutex_unlock(&chip->rw_lock);

	return stat;
}

static int __nu2105_set_irqmask(struct nu2105 *chip, u32 irqmask)
{
	const u8 reg[] = {
		NU2105_REG_AC_PROTECTION,
		NU2105_REG_BUS_OCP_UCP,
		NU2105_REG_INT_MASK,
		NU2105_REG_FLT_MASK,
		NU2105_REG_REG_FLAG_MASK,
	};
	u8 mask[] = {
		BIT(5),		/* NU2105_REG_AC_PROTECTION */
		BIT(5),		/* NU2105_REG_BUS_OCP_UCP */
		(0xFF),		/* NU2105_REG_INT_MASK */
		(0xFF),		/* NU2105_REG_FLT_MASK */
		(0x0F),		/* NU2105_REG_REG_FLAG_MASK */
	};
	u8 val[] = {
		/* NU2105_REG_AC_PROTECTION */
		(irqmask & BIT(NU2105_IRQFLAG_AC_OVP) ? BIT(5) : 0),
		/* NU2105_REG_BUS_OCP_UCP */
		(irqmask & BIT(NU2105_IRQFLAG_IBUS_UCP_RISE) ? BIT(5) : 0),
		/* NU2105_REG_INT_MASK */
		((irqmask & BIT(NU2105_IRQFLAG_BAT_OVP_ALM) ? BIT(7) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BAT_OCP_ALM) ? BIT(6) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BUS_OVP_ALM) ? BIT(5) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BUS_OCP_ALM) ? BIT(4) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BAT_UCP_ALM) ? BIT(3) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_ADAPTER_INSERT) ? BIT(2) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_VBAT_INSERT) ? BIT(1) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_ADC_DONE) ? BIT(0) : 0)),
		/* NU2105_REG_FLT_MASK */
		((irqmask & BIT(NU2105_IRQFLAG_BAT_OVP_FLT) ? BIT(7) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BAT_OCP_FLT) ? BIT(6) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BUS_OVP_FLT) ? BIT(5) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_BUS_OCP_FLT) ? BIT(4) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_TSBUS_TSBAT_ALM) ? BIT(3) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_TSBAT_FLT) ? BIT(2) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_TSBUS_FLT) ? BIT(1) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_TDIE_ALM) ? BIT(0) : 0)),
		/* NU2105_REG_REG_FLAG_MASK */
		((irqmask & BIT(NU2105_IRQFLAG_VBATREG_ACTIVE) ? BIT(3) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_IBATREG_ACTIVE) ? BIT(2) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_VDROP_OVP) ? BIT(1) : 0)
		| (irqmask & BIT(NU2105_IRQFLAG_VOUT_OVP) ? BIT(0) : 0)),
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(reg); i++) {
		ret = __nu2105_update_bits(chip, reg[i], mask[i], val[i]);
		if (ret) {
			dev_err(chip->dev, "failed to set irqmask "
				"reg 0x%02x\n", reg[i]);
		}
	}

	chip->irqmask = irqmask;

	return 0;
}

static int __nu2105_set_protection(struct nu2105 *chip)
{
	const u8 reg[] = {
		NU2105_REG_BAT_OVP,
		NU2105_REG_BAT_OVP_ALM,
		NU2105_REG_BAT_OCP,
		NU2105_REG_BAT_OCP_ALM,
		NU2105_REG_BAT_UCP_ALM,
		NU2105_REG_BUS_OVP_ALM,
		NU2105_REG_BUS_OCP_UCP,
		NU2105_REG_BUS_OCP_ALM,
		NU2105_REG_CHRG_CTRL,
	};
	u8 mask[] = {
		BIT(7),				/* NU2105_REG_BAT_OVP */
		BIT(7),				/* NU2105_REG_BAT_OVP_ALM */
		BIT(7),				/* NU2105_REG_BAT_OCP */
		BIT(7),				/* NU2105_REG_BAT_OCP_ALM */
		BIT(7),				/* NU2105_REG_BAT_UCP_ALM */
		BIT(7),				/* NU2105_REG_BUS_OVP_ALM */
		BIT(7),				/* NU2105_REG_BUS_OCP_UCP */
		BIT(7),				/* NU2105_REG_BUS_OCP_ALM */
		BIT(2) | BIT(1) | BIT(0),	/* NU2105_REG_CHRG_CTRL */
	};
	u8 val[] = {
		/* NU2105_REG_BAT_OVP */
		(chip->cfg.bat_ovp_dis ? BIT(7) : 0),
		/* NU2105_REG_BAT_OVP_ALM */
		(chip->cfg.bat_ovp_alm_dis ? BIT(7) : 0),
		/* NU2105_REG_BAT_OCP */
		(chip->cfg.bat_ocp_dis ? BIT(7) : 0),
		/* NU2105_REG_BAT_OCP_ALM */
		(chip->cfg.bat_ocp_alm_dis ? BIT(7) : 0),
		/* NU2105_REG_BAT_UCP_ALM */
		(chip->cfg.bat_ucp_alm_dis ? BIT(7) : 0),
		/* NU2105_REG_BUS_OVP_ALM */
		(chip->cfg.bus_ovp_alm_dis ? BIT(7) : 0),
		/* NU2105_REG_BUS_OCP_UCP */
		(chip->cfg.bus_ocp_dis ? BIT(7) : 0),
		/* NU2105_REG_BUS_OCP_ALM */
		(chip->cfg.bus_ocp_alm_dis ? BIT(7) : 0),
		/* NU2105_REG_CHRG_CTRL */
		((chip->cfg.tsbus_dis ? BIT(2) : 0)
		| (chip->cfg.tsbat_dis ? BIT(1) : 0)
		| (chip->cfg.tdie_dis ? BIT(0) : 0)),
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(reg); i++) {
		ret = __nu2105_update_bits(chip, reg[i], mask[i], val[i]);
		if (ret) {
			dev_err(chip->dev, "failed to set protection "
				"reg 0x%02x\n", reg[i]);
		}
	}

	return 0;
}

/* irq handlers */
static void nu2105_irq_ac_ovp(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_AC_OVP)))
		return;

	dev_err(chip->dev, "ac_ovp\n");
}

static void nu2105_irq_ibus_ucp_rise(struct nu2105 *chip, u32 stat)
{
	dev_info(chip->dev, "ibus_ucp_rise\n");
}

static void nu2105_irq_ibus_ucp_fall(struct nu2105 *chip, u32 stat)
{
	dev_warn(chip->dev, "ibus_ucp_fall\n");

	if (!chip->chg_dev)
		return;

	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_IBUSUCP_FALL);
}

static void nu2105_irq_tshut(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_TSHUT)))
		return;

	dev_err(chip->dev, "tshut\n");
}

static void nu2105_irq_ss_timeout(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "ss_timeout\n");
}

static void nu2105_irq_conv_ocp(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "conv_ocp\n");
}

static void nu2105_irq_pin_diag_fail(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "pin_diag_fial\n");
}

static void nu2105_irq_wd_timeout(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "wd_timeout\n");
}

static void nu2105_irq_bat_ovp_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_BAT_OVP_ALM)))
		return;

	dev_info(chip->dev, "bat_ovp_alm\n");

	if (!chip->chg_dev)
		return;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_VBATOVP_ALARM);
}

static void nu2105_irq_bat_ocp_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_BAT_OCP_ALM)))
		return;

	dev_info(chip->dev, "bat_ocp_alm\n");
}

static void nu2105_irq_bus_ovp_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_BUS_OVP_ALM)))
		return;

	dev_info(chip->dev, "bus_ovp_alm\n");

	if (!chip->chg_dev)
		return;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_VBUSOVP_ALARM);
}

static void nu2105_irq_bus_ocp_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_BUS_OCP_ALM)))
		return;

	dev_info(chip->dev, "bus_ocp_alm\n");
}

static void nu2105_irq_bat_ucp_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_BAT_UCP_ALM)))
		return;

	dev_info(chip->dev, "bat_ucp_alm\n");
}

static void nu2105_irq_adc_done(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_ADC_DONE)))
		return;

	complete(&chip->adc_done);
}

static void nu2105_irq_bat_ovp_flt(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "bat_ovp_flt\n");
}

static void nu2105_irq_bat_ocp_flt(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "bat_ocp_flt\n");

	if (!chip->chg_dev)
		return;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_IBATOCP);
}

static void nu2105_irq_bus_ovp_flt(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "bus_ovp_flt\n");
}

static void nu2105_irq_bus_ocp_flt(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "bus_ocp_flt\n");

	if (!chip->chg_dev)
		return;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_IBUSOCP);
}

static void nu2105_irq_tsbus_tsbat_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_TSBUS_TSBAT_ALM)))
		return;

	dev_info(chip->dev, "tsbus_tsbat_alm\n");
}

static void nu2105_irq_tsbat_flt(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "tsbat_flt\n");
}

static void nu2105_irq_tsbus_flt(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "tsbus_flt\n");
}

static void nu2105_irq_tdie_alm(struct nu2105 *chip, u32 stat)
{
	if (!(stat & BIT(NU2105_STAT_TDIE_ALM)))
		return;

	dev_info(chip->dev, "tdie_alm\n");
}

static void nu2105_irq_vdrop_ovp(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "vdrop_ovp\n");

	if (!chip->chg_dev)
		return;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_VOUTOVP);
}

static void nu2105_irq_vout_ovp(struct nu2105 *chip, u32 stat)
{
	dev_err(chip->dev, "vout_ovp\n");

	if (!chip->chg_dev)
		return;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_VDROVP);
}
#if 1
static void nu2105_dump_important_regs(struct nu2105 *bq)
{

	int ret;
	u8 val;

	ret = __nu2105_read(bq, 0x0A, &val);
	if (!ret)
		dev_err(bq->dev,"dump converter state Reg [%02X] = 0x%02X\n",
				0x0A, val);

	ret = __nu2105_read(bq, 0x0D, &val);
	if (!ret)
		dev_err(bq->dev,"dump int stat Reg[%02X] = 0x%02X\n",
				0x0D, val);

	ret = __nu2105_read(bq, 0x0E, &val);
	if (!ret)
		dev_err(bq->dev,"dump int flag Reg[%02X] = 0x%02X\n",
				0x0E, val);

	ret = __nu2105_read(bq, 0x02, &val);
	if (!ret)
		dev_err(bq->dev,"dump fault stat Reg[%02X] = 0x%02X\n",
				0x02, val);

	ret = __nu2105_read(bq, 0x10, &val);
	if (!ret)
		dev_err(bq->dev,"dump fault stat Reg[%02X] = 0x%02X\n",
				0x10, val);

	ret = __nu2105_read(bq, 0x11, &val);
	if (!ret)
		dev_err(bq->dev,"dump fault flag Reg[%02X] = 0x%02X\n",
				0x11, val);

	ret = __nu2105_read(bq, 0x2B, &val);
	if (!ret)
		dev_err(bq->dev,"dump regulation flag Reg[%02X] = 0x%02X\n",
				0x2B, val);

	ret = __nu2105_read(bq, 0x2D, &val);
	if (!ret)
		dev_err(bq->dev,"dump regulation flag Reg[%02X] = 0x%02X\n",
				0x2D, val);
}
#endif
static void nu2105_check_alarm_status(struct nu2105 *bq)
{
	int ret;
	u8 flag = 0;
	u8 stat = 0;

	ret = __nu2105_read(bq, 0x08, &flag);
	if (!ret && (flag & 0x10))
		dev_err(bq->dev,"UCP_FLAG =0x%02X\n",
			!!(flag & 0x10));

	ret = __nu2105_read(bq, 0x2D, &flag);
	if (!ret && (flag & 0x20))
		dev_err(bq->dev,"VDROP_OVP_FLAG =0x%02X\n",
			!!(flag & 0x20));

	/*read to clear alarm flag*/
	ret = __nu2105_read(bq, 0x0E, &flag);
	if (!ret && flag)
		dev_err(bq->dev,"INT_FLAG =0x%02X\n", flag);

	ret = __nu2105_read(bq, 0x0D, &stat);
	if (!ret) {
		dev_err(bq->dev,"INT_STAT = 0X%02x\n", stat);
	}

	ret = __nu2105_read(bq, 0x08, &stat);
	if (!ret && (stat & 0x50))
		dev_err(bq->dev,"Reg[08]BUS_UCPOVP = 0x%02X\n", stat);

	ret = __nu2105_read(bq, 0x0A, &stat);
	if (!ret && (stat & 0x02))
		dev_err(bq->dev,"Reg[0A]CONV_OCP = 0x%02X\n", stat);

}

static void nu2105_check_fault_status(struct nu2105 *bq)
{
	int ret;
	u8 flag = 0;
	u8 stat = 0;

	ret = __nu2105_read(bq, 0x10, &stat);
	if (!ret && stat)
		dev_err(bq->dev,"FAULT_STAT = 0x%02X\n", stat);

	ret = __nu2105_read(bq, 0x11, &flag);
	if (!ret && flag)
		dev_err(bq->dev,"FAULT_FLAG = 0x%02X\n", flag);
}


static void (*nu2105_irq_handlers[])(struct nu2105 *chip, u32 stat) = {
	[NU2105_IRQFLAG_AC_OVP] = nu2105_irq_ac_ovp,
	[NU2105_IRQFLAG_IBUS_UCP_RISE] = nu2105_irq_ibus_ucp_rise,
	[NU2105_IRQFLAG_IBUS_UCP_FALL] = nu2105_irq_ibus_ucp_fall,
	[NU2105_IRQFLAG_TSHUT] = nu2105_irq_tshut,
	[NU2105_IRQFLAG_SS_TIMEOUT] = nu2105_irq_ss_timeout,
	[NU2105_IRQFLAG_CONV_OCP] = nu2105_irq_conv_ocp,
	[NU2105_IRQFLAG_PIN_DIAG_FAIL] = nu2105_irq_pin_diag_fail,
	[NU2105_IRQFLAG_WD_TIMEOUT] = nu2105_irq_wd_timeout,
	[NU2105_IRQFLAG_BAT_OVP_ALM] = nu2105_irq_bat_ovp_alm,
	[NU2105_IRQFLAG_BAT_OCP_ALM] = nu2105_irq_bat_ocp_alm,
	[NU2105_IRQFLAG_BUS_OVP_ALM] = nu2105_irq_bus_ovp_alm,
	[NU2105_IRQFLAG_BUS_OCP_ALM] = nu2105_irq_bus_ocp_alm,
	[NU2105_IRQFLAG_BAT_UCP_ALM] = nu2105_irq_bat_ucp_alm,
	[NU2105_IRQFLAG_ADAPTER_INSERT] = NULL,
	[NU2105_IRQFLAG_VBAT_INSERT] = NULL,
	[NU2105_IRQFLAG_ADC_DONE] = nu2105_irq_adc_done,
	[NU2105_IRQFLAG_BAT_OVP_FLT] = nu2105_irq_bat_ovp_flt,
	[NU2105_IRQFLAG_BAT_OCP_FLT] = nu2105_irq_bat_ocp_flt,
	[NU2105_IRQFLAG_BUS_OVP_FLT] = nu2105_irq_bus_ovp_flt,
	[NU2105_IRQFLAG_BUS_OCP_FLT] = nu2105_irq_bus_ocp_flt,
	[NU2105_IRQFLAG_TSBUS_TSBAT_ALM] = nu2105_irq_tsbus_tsbat_alm,
	[NU2105_IRQFLAG_TSBAT_FLT] = nu2105_irq_tsbat_flt,
	[NU2105_IRQFLAG_TSBUS_FLT] = nu2105_irq_tsbus_flt,
	[NU2105_IRQFLAG_TDIE_ALM] = nu2105_irq_tdie_alm,
	[NU2105_IRQFLAG_VBATREG_ACTIVE] = NULL,
	[NU2105_IRQFLAG_IBATREG_ACTIVE] = NULL,
	[NU2105_IRQFLAG_VDROP_OVP] = nu2105_irq_vdrop_ovp,
	[NU2105_IRQFLAG_VOUT_OVP] = nu2105_irq_vout_ovp,
};

static irqreturn_t nu2105_irq_handler(int irq, void *data)
{
	struct nu2105 *chip = data;
	u32 flag, mask, stat;
	unsigned int i;

    if (irq_log)
    {
	flag = __nu2105_get_flag(chip);
	stat = __nu2105_get_stat(chip);
	mask = ~chip->irqmask;

	dev_err(chip->dev, "flag: 0x%08x (0x%08x), stat: 0x%08x\n",
			flag, mask, stat);
	flag &= mask;

	for (i = 0; i < ARRAY_SIZE(nu2105_irq_handlers); i++) {
		if ((flag & BIT(i)) && nu2105_irq_handlers[i])
			nu2105_irq_handlers[i](chip, stat);
	}
    }else{
	nu2105_dump_important_regs(chip);
//	__nu2105_dump_register(chip);
	nu2105_check_alarm_status(chip);
	nu2105_check_fault_status(chip);
    }
	

	return IRQ_HANDLED;
}

/* charger interface */
static int nu2105_enable_chg(struct charger_device *chg_dev, bool en)
{
	static const u32 fault = BIT(NU2105_STAT_AC_OVP)
			| BIT(NU2105_STAT_BAT_OVP_FLT)
			| BIT(NU2105_STAT_BUS_OVP_FLT)
			| BIT(NU2105_STAT_TSBAT_FLT)
			| BIT(NU2105_STAT_TSBUS_FLT);

	struct nu2105 *chip = charger_get_data(chg_dev);
	u32 stat;
	int ret;

    dev_err(chip->dev, "nu2105 enable: %d\n", en);
	if (!en) {
		ret = __nu2105_set_irqmask(chip, irqmask_default);
		if (ret < 0)
			dev_err(chip->dev, "failed to set irqmask\n");

		ret = __nu2105_enable_chg(chip, false);
		if (ret < 0) {
			dev_err(chip->dev, "failed to disable chg\n");
			return ret;
		}

       if (!adc_con)
       {
		mutex_lock(&chip->adc_lock);

		ret = __nu2105_enable_adc(chip, false);
		if (ret < 0) {
			dev_err(chip->dev, "failed to disable adc\n");
			return ret;
		}

		mutex_unlock(&chip->adc_lock);
        }

		goto out;
	}

	stat = __nu2105_get_stat(chip);
	if (stat & fault) {
		dev_err(chip->dev, "cannot enable chg. fault = 0x%08x\n",
				stat & fault);
		return -EPERM;
	}

	/* clear irqs */
	__nu2105_get_flag(chip);

	ret = __nu2105_set_irqmask(chip, chip->cfg.irqmask);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set irqmask\n");
		return ret;
	}

	ret = __nu2105_enable_chg(chip, true);
	if (ret < 0) {
		dev_err(chip->dev, "failed to enable chg\n");
		return ret;
	}


	stat = __nu2105_get_stat(chip);
	if (stat & fault) {
		dev_err(chip->dev, "222 cannot enable chg. fault = 0x%08x\n",
				stat & fault);
		return -EPERM;
	}


	if (en && !__nu2105_is_chg_enabled(chip)) {
		dev_err(chip->dev, "chg not enabled\n");
		return -EIO;
	}

out:
	if (chip->cfg.watchdog_dis)
		return 0;

	ret = __nu2105_enable_watchdog(chip, false);
	if (ret < 0) {
		dev_err(chip->dev, "failed to enable watchdog\n");
		return ret;
	}
	dev_err(chip->dev, "nu2105 dump all register\n");
	__nu2105_dump_register(chip);

	return 0;
}

int nu2105_enable_adc(struct charger_device *chg_dev, bool en)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	dev_err(chip->dev,"%s en:%d\n",__func__, en);
	mutex_lock(&chip->adc_lock);
	if(en)
		__nu2105_write(chip, 0x14, 0xa8);
	else
		__nu2105_write(chip, 0x14, 0x0);
	mutex_unlock(&chip->adc_lock);
//	msleep(30);
//	__nu2105_dump_register(chip);

	return 0;

}

static int nu2105_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	*en = __nu2105_is_chg_enabled(chip);
	pr_info("%s() en = %d\n",__func__,*en);

	return 0;
}

static int nu2105_get_adc(struct charger_device *chg_dev,
			   enum adc_channel chan, int *min, int *max)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	int channel;
	int ret;
	dev_err(chip->dev,"%s() entry.\n",__func__);

	switch (chan) {
	case ADC_CHANNEL_VBUS:
		channel = NU2105_ADC_VBUS;
		break;
	case ADC_CHANNEL_VBAT:
		channel = NU2105_ADC_VBAT;
		break;
	case ADC_CHANNEL_IBUS:
		channel = NU2105_ADC_IBUS;
		break;
	case ADC_CHANNEL_IBAT:
		channel = NU2105_ADC_IBAT;
		break;
	case ADC_CHANNEL_TEMP_JC:
		channel = NU2105_ADC_TDIE;
		break;
	case ADC_CHANNEL_VOUT:
		channel = NU2105_ADC_VOUT;
		break;
	default:
		return -ENOTSUPP;
	}

	if (!min || !max)
		return -EINVAL;

	ret = __nu2105_get_adc(chip, channel, max);
    //dev_err(chip->dev, "%s------max=%d\n",__func__,*max);
 	if(*max < 0)
		*max = *max - 2*(*max);
	if (ret < 0)
		*max = 0;
#if 0
	switch (chan) {
	case ADC_CHANNEL_VBAT:
		*max = battery_get_bat_voltage() * 1000;
		dev_err(chip->dev, "%s------vbat max=%d\n",__func__,*max);
		break;
	case ADC_CHANNEL_IBAT:
		*max = battery_get_bat_current() * 1000;
		dev_err(chip->dev, "%s------ibat max=%d\n",__func__,*max);
		break;
	default:
		return -ENOTSUPP;
	}
#endif

	*min = *max;

	return ret;
}

static int nu2105_get_adc_accuracy(struct charger_device *chg_dev,
				    enum adc_channel chan, int *min, int *max)
{
	pr_info("%s()\n",__func__);
	switch (chan) {
	case ADC_CHANNEL_VBUS:
		*min = 35000;
		*max = 35000;
		break;
	case ADC_CHANNEL_VBAT:
		*min = 20000;
		*max = 20000;
		break;
	case ADC_CHANNEL_IBUS:
		*min = 150000;
		*max = 150000;
		break;
	case ADC_CHANNEL_IBAT:
		*min = 200000;
		*max = 200000;
		break;
	case ADC_CHANNEL_TEMP_JC:
		*min = 4;
		*max = 4;
		break;
	case ADC_CHANNEL_VOUT:
		*min = 20000;
		*max = 20000;
		break;
	default:
		*min = 0;
		*max = 0;
		break;
	}

	return 0;
}

static int nu2105_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	pr_info("%s()\n",__func__);
	return __nu2105_set_bus_ovp(chip, uV);
}

static int nu2105_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	pr_info("%s()\n",__func__);
	/* uA will be 110% of target */
	__nu2105_set_bus_ocp_alm(chip, uA / 110 * 100);

	return __nu2105_set_bus_ocp(chip, uA);
}

static int nu2105_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	pr_info("%s()\n",__func__);
	return __nu2105_set_bat_ovp(chip, uV);
}

static int nu2105_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	int ret;
	pr_info("%s()\n",__func__);
	mutex_lock(&chip->ops_lock);

	ret = __nu2105_set_bat_ovp_alm(chip, uV);

	mutex_unlock(&chip->ops_lock);

	return ret;
}

static int nu2105_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	pr_info("%s()\n",__func__);
	mutex_lock(&chip->ops_lock);

	__nu2105_set_bits(chip, NU2105_REG_BAT_OVP_ALM,
			NU2105_BAT_OVP_ALM_DIS_MASK);
	__nu2105_clr_bits(chip, NU2105_REG_BAT_OVP_ALM,
			NU2105_BAT_OVP_ALM_DIS_MASK);

	mutex_unlock(&chip->ops_lock);

	return 0;
}

static int nu2105_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	int ret;
	pr_info("%s()\n",__func__);
	mutex_lock(&chip->ops_lock);

	ret = __nu2105_set_bus_ovp_alm(chip, uV);

	mutex_unlock(&chip->ops_lock);

	return ret;
}

#if 0
static int nu2105_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	u32 stat = __nu2105_get_stat(chip);

	*err = false;
	if (stat & BIT(NU2105_STAT_VBUS_ERRORLO))
		*err = true;

	return 0;
}
#else
#define VBUS_ERROR_LO		BIT(5)

static int nu2105_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	int ret;
	u8 stat = 0;
	*err = false;
	pr_info("%s()\n",__func__);
	ret = __nu2105_read(chip, 0x0A, &stat);
	if (!ret)
	{
		dev_err(chip->dev,"nu2105_is_vbuslowerr,NU2105_REG_0A: 0x%02X\n", stat);

		if (stat & VBUS_ERROR_LO)
			*err = true;
	}

	return 0;
}

#endif

static int nu2105_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	pr_info("%s()\n",__func__);
	mutex_lock(&chip->ops_lock);

	__nu2105_set_bits(chip, NU2105_REG_BUS_OVP_ALM,
			NU2105_BUS_OVP_ALM_DIS_MASK);
	__nu2105_clr_bits(chip, NU2105_REG_BUS_OVP_ALM,
			NU2105_BUS_OVP_ALM_DIS_MASK);

	mutex_unlock(&chip->ops_lock);

	return 0;
}

static int nu2105_init_chip(struct charger_device *chg_dev)
{
//    nu2105_enable_chg(chg_dev, true);
	pr_info("%s()\n",__func__);
	return 0;
}

static int nu2105_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct nu2105 *chip = charger_get_data(chg_dev);
	pr_info("%s()\n",__func__);
	/* uA will be 110% of target */
	__nu2105_set_bat_ocp_alm(chip, uA / 110 * 100);

	return __nu2105_set_bat_ocp(chip, uA);
}

static const struct charger_ops nu2105_chg_ops = {
	.enable = nu2105_enable_chg,
	.is_enabled = nu2105_is_chg_enabled,
	.get_adc = nu2105_get_adc,
	.set_vbusovp = nu2105_set_vbusovp,
	.set_ibusocp = nu2105_set_ibusocp,
	.set_vbatovp = nu2105_set_vbatovp,
	.set_ibatocp = nu2105_set_ibatocp,
	.init_chip = nu2105_init_chip,
	.set_vbatovp_alarm = nu2105_set_vbatovp_alarm,
	.reset_vbatovp_alarm = nu2105_reset_vbatovp_alarm,
	.set_vbusovp_alarm = nu2105_set_vbusovp_alarm,
	.reset_vbusovp_alarm = nu2105_reset_vbusovp_alarm,
	.is_vbuslowerr = nu2105_is_vbuslowerr,
	.get_adc_accuracy = nu2105_get_adc_accuracy,
};

/* debugfs interface */
static int debugfs_get_data(void *data, u64 *val)
{
	struct nu2105 *chip = data;
	int ret;
	u8 temp;

	ret = __nu2105_read(chip, chip->debug_addr, &temp);
	if (ret)
		return -EAGAIN;

	*val = temp;

	return 0;
}

static int debugfs_set_data(void *data, u64 val)
{
	struct nu2105 *chip = data;
	int ret;
	u8 temp;

	temp = (u8)val;
	ret = __nu2105_write(chip, chip->debug_addr, temp);
	if (ret)
		return -EAGAIN;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(data_debugfs_ops,
	debugfs_get_data, debugfs_set_data, "0x%02llx\n");

static int dump_debugfs_show(struct seq_file *m, void *start)
{
	struct nu2105 *chip = m->private;
	u8 reg, val;
	int ret;

	for (reg = NU2105_REG_BAT_OVP; reg <= NU2105_REG_CHGMODE; reg++) {
		ret = __nu2105_read(chip, reg, &val);
		if (ret) {
			seq_printf(m, "0x%02x = error\n", reg);
			continue;
		}

		seq_printf(m, "0x%02x = 0x%02x\n", reg, val);
	}

	return 0;
}

static int dump_debugfs_open(struct inode *inode, struct file *file)
{
	struct nu2105 *chip = inode->i_private;

	return single_open(file, dump_debugfs_show, chip);
}

static const struct file_operations dump_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= dump_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int create_debugfs_entries(struct nu2105 *chip)
{
	struct dentry *ent;

	chip->debugfs = debugfs_create_dir(chip->chg_prop.alias_name, NULL);
	if (!chip->debugfs) {
		dev_err(chip->dev, "failed to create debugfs\n");
		return -ENODEV;
	}

	ent = debugfs_create_x8("addr", S_IFREG | S_IWUSR | S_IRUGO,
		chip->debugfs, &chip->debug_addr);
	if (!ent)
		dev_err(chip->dev, "failed to create addr debugfs\n");

	ent = debugfs_create_file("data", S_IFREG | S_IWUSR | S_IRUGO,
		chip->debugfs, chip, &data_debugfs_ops);
	if (!ent)
		dev_err(chip->dev, "failed to create data debugfs\n");

	ent = debugfs_create_file("dump", S_IFREG | S_IRUGO,
		chip->debugfs, chip, &dump_debugfs_ops);
	if (!ent)
		dev_err(chip->dev, "failed to create dump debugfs\n");

	return 0;
}

static int nu2105_charger_device_register(struct nu2105 *chip)
{
#if 0
	chip->chg_prop.alias_name = chip->desc->chg_name;
	chip->chg_dev = charger_device_register(chip->desc->chg_name,
			chip->dev, chip, &nu2105_chg_ops, &chip->chg_prop);

	chip->chg_prop.alias_name = "nu2105_standalone";
	chip->chg_dev = charger_device_register("primary_divider_chg",
			chip->dev, chip, &nu2105_chg_ops, &chip->chg_prop);
#endif
	chip->chg_prop.alias_name = chip->cfg.chg_name;
	chip->chg_dev = charger_device_register(chip->cfg.chg_name,
			chip->dev, chip, &nu2105_chg_ops, &chip->chg_prop);



	if (!chip->chg_dev)
		return -EINVAL;

	return 0;
}

static int nu2105_irq_init(struct nu2105 *chip)
{
//	struct device_node *np = chip->dev->of_node;
	struct gpio_desc *irq_gpio;
	int irq;
	int ret = 0;
	irq_gpio = devm_gpiod_get(chip->dev, "nu2105,intr", GPIOD_IN);
	if (IS_ERR(irq_gpio))
		return PTR_ERR(irq_gpio);

	irq = gpiod_to_irq(irq_gpio);
	if (irq < 0) {
		dev_err(chip->dev, "%s irq mapping fail(%d)\n", __func__, irq);
		return ret;
	}
	dev_info(chip->dev, "%s irq = %d\n", __func__, irq);


	ret = devm_request_threaded_irq(chip->dev, irq, NULL,
					nu2105_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"nu2105_irq", chip);
	if (ret) {
		dev_err(chip->dev, "failed to request irq %d\n", irq);
		return ret;
	}

	return ret;
}


static int nu2105_hw_init(struct nu2105 *chip)
{
	int ret = 0;

	ret = __nu2105_set_bat_ucp_alm(chip, 2000000);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set bat_ucp_alm\n");
		return ret;
	}

	ret = __nu2105_set_ac_ovp(chip, chip->cfg.ac_ovp);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set ac_ovp\n");
		return ret;
	}

	ret = __nu2105_set_fsw(chip, chip->cfg.fsw_set);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set fsw\n");
		return ret;
	}

	ret = __nu2105_set_watchdog(chip, chip->cfg.watchdog);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set watchdog\n");
		return ret;
	}

	ret = __nu2105_enable_watchdog(chip, false);
	if (ret < 0) {
		dev_err(chip->dev, "failed to disable watchdog\n");
		return ret;
	}

	ret = __nu2105_set_adc_fn_dis(chip, chip->cfg.adc_fn_dis);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set adc channel\n");
		return ret;
	}

	ret = __nu2105_set_ibat_sns_res(chip, chip->cfg.ibat_sns_res);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set ibat_sns_res\n");
		return ret;
	}

	ret = __nu2105_set_ss_timeout(chip, chip->cfg.ss_timeout);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set ss_timeout\n");
		return ret;
	}

	ret = __nu2105_set_ibus_low_dg_set(chip, chip->cfg.ibus_low_dg_set);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set ibus_low_dg_set\n");
		return ret;
	}

	ret = __nu2105_set_irqmask(chip, irqmask_default);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set mask\n");
		return ret;
	}

	ret = __nu2105_set_protection(chip);
	if (ret < 0) {
		dev_err(chip->dev, "failed to set protection\n");
		return ret;
	}

	/* clear irqs */
	__nu2105_get_flag(chip);
	
	/********for nu2105********/			
	ret = __nu2105_write(chip, 0x00, 0x29);
	ret = __nu2105_write(chip, 0x01, 0x24);
	ret = __nu2105_write(chip, 0x02, 0x34);
	ret = __nu2105_write(chip, 0x03, 0x28);
	ret = __nu2105_write(chip, 0x04, 0x14);
	ret = __nu2105_write(chip, 0x05, 0x18);
	ret = __nu2105_write(chip, 0x06, 0x50);
	ret = __nu2105_write(chip, 0x07, 0x3C);
	ret = __nu2105_write(chip, 0x08, 0x0D);
	ret = __nu2105_write(chip, 0x09, 0x50);
	ret = __nu2105_write(chip, 0x0B, 0x44);
	ret = __nu2105_write(chip, 0x0C, 0x16);
	ret = __nu2105_write(chip, 0x0F, 0xC9);
	ret = __nu2105_write(chip, 0x12, 0x0e);
	ret = __nu2105_write(chip, 0x14, 0x0);
	ret = __nu2105_write(chip, 0x15, 0x06);
	ret = __nu2105_write(chip, 0x28, 0x28);
	ret = __nu2105_write(chip, 0x29, 0x28);
	ret = __nu2105_write(chip, 0x2A, 0xC8);
	ret = __nu2105_write(chip, 0x2B, 0xC2);
	ret = __nu2105_write(chip, 0x2C, 0x00);
	ret = __nu2105_write(chip, 0x2D, 0x00);
	ret = __nu2105_write(chip, 0x2E, 0x18);
	ret = __nu2105_write(chip, 0x2F, 0x00);
	/********for nu2105********/

	return ret;
}

static void nu2105_parse_dt_adc(struct nu2105 *chip, struct device_node *np)
{
	chip->cfg.adc_fn_dis = 0;

	if (of_property_read_bool(np, "ibus_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(8);
	if (of_property_read_bool(np, "vbus_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(7);
	if (of_property_read_bool(np, "vac_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(6);
	if (of_property_read_bool(np, "vout_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(5);
	if (of_property_read_bool(np, "vbat_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(4);
	if (of_property_read_bool(np, "ibat_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(3);
	if (of_property_read_bool(np, "tsbus_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(2);
	if (of_property_read_bool(np, "tsbat_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(1);
	if (of_property_read_bool(np, "tdie_adc_dis"))
		chip->cfg.adc_fn_dis |= BIT(0);
}

static void nu2105_parse_dt_irq(struct nu2105 *chip, struct device_node *np)
{
	chip->cfg.irqmask = 0;

	if (of_property_read_bool(np, "ac_ovp_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_AC_OVP);
	if (of_property_read_bool(np, "ibus_ucp_rise_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_IBUS_UCP_RISE);
	if (of_property_read_bool(np, "bat_ovp_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BAT_OVP_ALM);
	if (of_property_read_bool(np, "bat_ocp_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BAT_OCP_ALM);
	if (of_property_read_bool(np, "bus_ovp_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BUS_OVP_ALM);
	if (of_property_read_bool(np, "bus_ocp_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BUS_OCP_ALM);
	if (of_property_read_bool(np, "bat_ucp_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BAT_UCP_ALM);
	if (of_property_read_bool(np, "adapter_insert_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_ADAPTER_INSERT);
	if (of_property_read_bool(np, "vbat_insert_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_VBAT_INSERT);
	if (of_property_read_bool(np, "adc_done_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_ADC_DONE);
	if (of_property_read_bool(np, "bat_ovp_flt_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BAT_OVP_FLT);
	if (of_property_read_bool(np, "bat_ocp_flt_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BAT_OCP_FLT);
	if (of_property_read_bool(np, "bus_ovp_flt_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BUS_OVP_FLT);
	if (of_property_read_bool(np, "bus_ocp_flt_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_BUS_OCP_FLT);
	if (of_property_read_bool(np, "tsbus_tsbat_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_TSBUS_TSBAT_ALM);
	if (of_property_read_bool(np, "tsbat_flt_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_TSBAT_FLT);
	if (of_property_read_bool(np, "tsbus_flt_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_TSBUS_FLT);
	if (of_property_read_bool(np, "tdie_alm_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_TDIE_ALM);
	if (of_property_read_bool(np, "vbatreg_active_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_VBATREG_ACTIVE);
	if (of_property_read_bool(np, "ibatreg_active_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_IBATREG_ACTIVE);
	if (of_property_read_bool(np, "vdrop_ovp_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_VDROP_OVP);
	if (of_property_read_bool(np, "vout_ovp_mask"))
		chip->cfg.irqmask |= BIT(NU2105_IRQFLAG_VOUT_OVP);
}

static void nu2105_parse_dt_protection(struct nu2105 *chip,
					struct device_node *np)
{
	chip->cfg.bat_ovp_dis = of_property_read_bool(np, "bat_ovp_dis");
	chip->cfg.bat_ovp_alm_dis =
			of_property_read_bool(np, "bat_ovp_alm_dis");
	chip->cfg.bat_ocp_dis = of_property_read_bool(np, "bat_ocp_dis");
	chip->cfg.bat_ocp_alm_dis =
			of_property_read_bool(np, "bat_ocp_alm_dis");
	chip->cfg.bat_ucp_alm_dis =
			of_property_read_bool(np, "bat_ucp_alm_dis");
	chip->cfg.bus_ovp_alm_dis =
			of_property_read_bool(np, "bus_ovp_alm_dis");
	chip->cfg.bus_ocp_dis = of_property_read_bool(np, "bus_ocp_dis");
	chip->cfg.bus_ocp_alm_dis =
			of_property_read_bool(np, "bus_ocp_alm_dis");
	chip->cfg.tsbus_dis = of_property_read_bool(np, "tsbus_dis");
	chip->cfg.tsbat_dis = of_property_read_bool(np, "tsbat_dis");
	chip->cfg.tdie_dis = of_property_read_bool(np, "tdie_dis");
}

static int nu2105_parse_dt(struct nu2105 *chip)
{
	struct device_node *np = chip->dev->of_node;
	int ret;

	if (!np)
		return -ENODEV;

	ret = of_property_read_u32(np, "ac_ovp", &chip->cfg.ac_ovp);
	if (ret)
		chip->cfg.ac_ovp = 17000000;
	ret = of_property_read_u32(np, "bat_ucp_alm", &chip->cfg.bat_ucp_alm);
	if (ret)
		chip->cfg.bat_ucp_alm = 2000000;
	ret = of_property_read_u32(np, "ss_timeout", &chip->cfg.ss_timeout);
	if (ret)
		chip->cfg.ss_timeout = 0;
	ret = of_property_read_u32(np, "fsw_set", &chip->cfg.fsw_set);
	if (ret)
		chip->cfg.fsw_set = 500000;
	ret = of_property_read_u32(np, "ibat_sns_res", &chip->cfg.ibat_sns_res);
	if (ret)
		chip->cfg.ibat_sns_res = 5;
	ret = of_property_read_u32(np, "ibus_low_dg_set",
			&chip->cfg.ibus_low_dg_set);
	if (ret)
		chip->cfg.ibus_low_dg_set = 10;

	chip->cfg.watchdog_dis = of_property_read_bool(np, "watchdog_dis");
	ret = of_property_read_u32(np, "watchdog", &chip->cfg.watchdog);
	if (ret)
		chip->cfg.watchdog = 30000000;
	if (!chip->cfg.watchdog)
		chip->cfg.watchdog_dis = true;

	if (of_property_read_string(np, "chg_name", &chip->cfg.chg_name) < 0)
		dev_info(chip->dev, "%s no chg name\n", __func__);
	dev_info(chip->dev, "%s: chg_name1 = %s", __func__, chip->cfg.chg_name);

	nu2105_parse_dt_protection(chip, np);
	nu2105_parse_dt_irq(chip, np);
	nu2105_parse_dt_adc(chip, np);

	return 0;
}

static void determine_initial_status(struct nu2105 *chip)
{
	if (chip->client->irq)
		nu2105_irq_handler(chip->client->irq, chip);
}

static bool nu2105_detect_device(struct nu2105 *chip)
{
	int ret;
	u8 val;
	
	ret = __nu2105_read(chip, NU2105_REG_PART_INFO, &val);
	chg_id = val;
	dev_err(chip->dev, "nu2105_detect_device---val=0x%x\n",val);
	if (val == 0xc0)
		return true;
	else
		return false;
}

static int nu2105_disable_adc(struct nu2105 *chip)
{
	dev_err(chip->dev,"%s\n",__func__);
	return __nu2105_write(chip, 0x14, 0x0);
}
static void nu2105_shutdown(struct i2c_client *client)
{
	struct nu2105 *chip = i2c_get_clientdata(client);
	dev_err(chip->dev,"%s\n",__func__);
	nu2105_disable_adc(chip);
}


static ssize_t chgid_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", chg_id);
}

static DEVICE_ATTR(chgid, 0664, chgid_show, NULL);

static int nu2105_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct nu2105 *chip;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, chip);
	chip->client = client;
//	chip->client->addr = 0x66;
	chip->dev = &client->dev;
	mutex_init(&chip->rw_lock);
	mutex_init(&chip->ops_lock);
	mutex_init(&chip->adc_lock);
	init_completion(&chip->adc_done);
	
	if(!nu2105_detect_device(chip)){
		dev_err(&client->dev, "nu2105_detect_device failed.\n");
		return ret;
	}

	ret = nu2105_parse_dt(chip);
	if (ret){
		dev_err(&client->dev, "nu2105_parse_dt failed.\n");
		return ret;
	}

	ret = nu2105_hw_init(chip);
	if (ret){
		dev_err(&client->dev, "nu2105_hw_init failed.\n");
		return ret;
	}
	ret = nu2105_irq_init(chip);
	if (ret){
		dev_err(&client->dev, "nu2105_irq_init failed.\n");
		return ret;
	}

    determine_initial_status(chip);
	
	ret = nu2105_charger_device_register(chip);
	if (ret){
		dev_err(&client->dev, "nu2105_charger_device_register failed.\n");
		return ret;
	}

	create_debugfs_entries(chip);
	device_create_file(&client->dev, &dev_attr_chgid);
//	__nu2105_dump_register(chip);
	nu2105_exist = 1;
	
	return ret;
}

static int nu2105_remove(struct i2c_client *client)
{
	struct nu2105 *chip = i2c_get_clientdata(client);

	charger_device_unregister(chip->chg_dev);

	return 0;
}

static const struct of_device_id nu2105_of_match[] = {
	{ .compatible = "NuVolta,nu2105", },
	{ },
};

static const struct i2c_device_id nu2105_i2c_id[] = {
	{ .name = "nu2105", },
	{ },
};

static struct i2c_driver nu2105_driver = {
	.probe = nu2105_probe,
	.remove = nu2105_remove,
	.shutdown = nu2105_shutdown,
	.driver = {
		.name = "nu2105",
		.of_match_table = nu2105_of_match,
	},
	.id_table = nu2105_i2c_id,
};
module_i2c_driver(nu2105_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("bing");
MODULE_DESCRIPTION("NuVolta NU2105 Switched Cap Fast Charger");
MODULE_VERSION("1.0");
