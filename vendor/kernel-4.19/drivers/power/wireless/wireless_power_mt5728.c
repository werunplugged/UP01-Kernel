/**
 * @file   wireless_power_mt5728.c
 * @author  <Yangwl@maxictech.com>
 * @date   Mon May 13 18:11:39 2019
 * @brief  Maxictech Proprietary and Confidential
 *         Copyright (c) 2017 Maxic Technology Corporation
 *         All Rights Reserved
 */
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/input.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <stdbool.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/pinctrl/consumer.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include "wireless_power_mt5728.h"
#ifdef MT5728_USE_WAKELOCK
// #include <linux/wakelock.h>
#include <linux/pm_wakeup.h>
#endif

#include <mt-plat/mtk_boot_common.h>

#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/mtk_battery.h>


#include <mt-plat/mtk_boot.h>
// #include <pmic.h>
#include <mtk_gauge_time_service.h>
#include "mtk_charger_intf.h"

#include "mtk_intf.h"

//#include <linux/stdlib.h>

#include "MT5728_mtp_array.h"
#include <linux/cust_include/cust_project_all_config.h>

extern bool otg_plugin;

#define DEVICE_NAME "mt5728"

#define DRIVER_FIRMWARE_VERSION "0.0.1"

#define FALSE 0
#define TRUE 1
#define UP_MT5728_WIRELESS_TRX_MODE_SWITCH 1
#define UP_MT5728_WIRELESS_ADD_OTG 1
#ifdef __CUST_REVERSE_TIMEOUT_CNT__
#define REVERSE_TIMEOUT_CNT __CUST_REVERSE_TIMEOUT_CNT__
#else
#define REVERSE_TIMEOUT_CNT 1200
#endif

static bool vbat_temp_hot = false;

int wls_work_online = 0;                 //wireless charge working
int reserse_charge_online = 0;
int usb_otg_online = 0;
int usbchip_otg_detect = 0;
// int MT5728_rt_mode_n = 0;                //GPOD5,rxmode - 0
volatile unsigned int AfcSendTimeout; //afc vout set time out
volatile unsigned char AfcIntFlag;
volatile int rx_vout_max = 5000;
volatile int rx_iout_max = 1000;
volatile int rx_iout_limit = 100;
#ifdef MT5728_USE_WAKELOCK
// static struct wake_lock mt5728_wls_wake_lock;
static struct wakeup_source mt5728_wls_wake_lock;
#endif
static int cur_last = 0;
static int last_ichg = 0;
static int powergood_err_cnt = 0;
volatile int setup_iout_start_cnt = 0;
volatile int reverse_timeout_cnt = 0;
volatile int current_change_interval_cnt = 0;
static int mt5728_charging_otgin_flag = 0;
static int input_current_limit = 0;
#ifndef MT5728_CHIP_AUTO_AFC9V
static volatile int vout_change_timecnt = 0;
#endif
static volatile int current_reduce_flag = 0;
static int mt5728_vout_old;
static int mt5728_vrect_old;
static int mt5728_mtp_write_flag;
static volatile int mt5728_epp_ctrl_vout_flag;
static int mt5728_ldo_on_flag;
static bool first_boot = true;
static struct charger_device *primary_charger;
static volatile int mt5728_epp_ptpower;
static unsigned int boot_mode;
#ifdef CONFIG_FG_SD77561
extern struct charger_manager *pinfo;
#endif

static volatile int mt5728_tx_ping_cnt = 0;
static volatile int mt5728_tx_cep_cnt_old = 0;
static volatile int mt5728_tx_cep_cnt_timeout = 0;

typedef struct
{
    unsigned char G;
    s8 Offs;
} FodType;

FodType mt5728fod[8]={{250,127},{250,127},{250,127},{250,127},{250,127},{250,127},{250,127},{250,127}};

typedef union {
    u16 value;
    u8 ptr[2];
} vuc;

struct mt5728_dev *mte = NULL, *chip = NULL;
//struct delayed_work MT5728_int_delayed_work;
typedef struct Pgm_Type {
    u16 status;
    u16 addr;
    u16 length;
    u16 cs;
    u8 data[MTP_BLOCK_SIZE];
} Pgm_Type_t;
struct pinctrl* mt5728_pinctrl;

struct mt5728_func {
    int (*read)(struct mt5728_dev* di, u16 reg, u8* val);
    int (*write)(struct mt5728_dev* di, u16 reg, u8 val);
    int (*read_buf)(struct mt5728_dev* di, u16 reg, u8* buf, unsig32 size);
    int (*write_buf)(struct mt5728_dev* di, u16 reg, u8* buf, unsig32 size);
};

struct mt5728_dev {
    char  *name;
    struct i2c_client* client;
    struct device* dev;
    struct regmap* regmap;
    struct mt5728_func bus;

    unsigned int state;
    int    resume_vbatt;
    int    powergood_gpio;
    int    online_back;
    int    irq_gpio;
    struct device_node	*irq_nd;     /* node */
    int    charger_en_gpio;
    //int    otg_ctrl_gpio;
    int    ldo_ctrl_gpio;
    int    trx_mode_gpio;
    int    chip_en_gpio;
    int    chip_en;
    int    rxdetect_flag;
    int    rxremove_flag;
    int    wls_afc_cmd;
    struct mutex slock;
    struct delayed_work eint_work;
    struct delayed_work charger_work;
    struct delayed_work reverse_charge_work;
    //struct delayed_work mt5728_fwcheck_work;
    int    ldo_status;
    int    is_samsung_charge;
    struct device_node* irq_node1;
    int fsk_status;
    int otg_flag;
    int otp_flag;
    int reverse_charger;
    //power_supply
    struct power_supply *wl_psy;
    struct power_supply *batt_psy;
    struct power_supply *usb_psy;
    struct power_supply *dc_psy;
    struct power_supply_desc wl_psd;
    struct power_supply_config wl_psc; 
    struct power_supply_desc batt_psd;    
    struct power_supply_desc usb_psd;
    struct power_supply_desc dc_psd;
};

int wls_work_int_pin = 0;
#define REG_NONE_ACCESS 0
#define REG_RD_ACCESS (1 << 0)
#define REG_WR_ACCESS (1 << 1)
#define REG_BIT_ACCESS (1 << 2)
#define REG_MAX 0x0F

struct reg_attr {
    const char* name;
    u16 addr;
    u8 flag;
};

enum REG_INDEX {
    CHIPID = 0,
    FWVERSION,
    VOUT,
    INT_FLAG,
    INTCTLR,
    VOUTSET,
    VFC,
    CMD,
    RXPERI,
    TXPERI,
    INDEX_MAX,
};

static struct reg_attr reg_access[INDEX_MAX]={
    [CHIPID] = { "CHIPID", REG_CHIPID, REG_RD_ACCESS },
    [FWVERSION] = { "FWVERSION", REG_FW_VER, REG_RD_ACCESS },
    [VOUT] = { "VOUT", REG_VOUT, REG_RD_ACCESS },
    [INT_FLAG] = { "INT_FLAG", REG_INTFLAG, REG_RD_ACCESS },
    [INTCTLR] = { "INTCLR", REG_INTCLR, REG_WR_ACCESS },
    [VOUTSET] = { "VOUTSET", REG_VOUTSET, REG_RD_ACCESS | REG_WR_ACCESS },
    [VFC] = { "VFC", REG_VFC, REG_RD_ACCESS | REG_WR_ACCESS },
    [CMD] = { "CMD", REG_CMD, REG_RD_ACCESS | REG_WR_ACCESS | REG_BIT_ACCESS },
    [RXPERI] = { "RXPERI", REG_RXD_PERI, REG_RD_ACCESS | REG_WR_ACCESS | REG_BIT_ACCESS },
    [TXPERI] = { "TXPERI", RED_TXD_PERI, REG_RD_ACCESS | REG_WR_ACCESS | REG_BIT_ACCESS },
};

#define SET_GPIO_WIRELESS_EN_H()    gpio_set_value(chip->chip_en_gpio, 1)
#define SET_GPIO_WIRELESS_EN_L()    gpio_set_value(chip->chip_en_gpio, 0)
#define GPIO_WIRELESS_EN_INPUT()    gpio_direction_input(chip->chip_en_gpio)
#define GPIO_WIRELESS_EN_OUTPUT()   gpio_direction_output(chip->chip_en_gpio, 0)
#define GET_GPIO_WIRELESS_EN()      gpio_get_value(chip->chip_en_gpio)

#if 0
#define SET_GPIO_OTG_CTRL_H()       gpio_set_value(chip->otg_ctrl_gpio, 1)
#define SET_GPIO_OTG_CTRL_L()       gpio_set_value(chip->otg_ctrl_gpio, 0)
#define GPIO_OTG_CTRL_INPUT()       gpio_direction_input(chip->otg_ctrl_gpio)
#define GPIO_OTG_CTRL_OUTPUT()      gpio_direction_output(chip->otg_ctrl_gpio, 0)
#endif

#define SET_GPIO_WPC_RTMODE_H()     gpio_set_value(chip->trx_mode_gpio, 1)
#define SET_GPIO_WPC_RTMODE_L()     gpio_set_value(chip->trx_mode_gpio, 0)
#define GPIO_WPC_RTMODE_OUTPUT()    gpio_direction_output(chip->trx_mode_gpio, 0)

#define SET_GPIO_LDO_CTRL_H()       /*gpio_set_value(mte->ldo_ctrl_gpio, 1)*/
#define SET_GPIO_LDO_CTRL_L()       /*gpio_set_value(mte->ldo_ctrl_gpio, 0) */
#define GPIO_LDO_CTRL_OUTPUT()      gpio_direction_output(chip->ldo_ctrl_gpio, 0)

#define GET_GPIO_POWERGOOD()        gpio_get_value(chip->powergood_gpio)
#define GPIO_POWERGOOD_INPUT()      gpio_direction_input(chip->powergood_gpio)
#if 0
void mt5728_otgen_gpio_ctrl(bool en) {
    if (en){
        GPIO_OTG_CTRL_OUTPUT();
        SET_GPIO_OTG_CTRL_L();
    } else{
         GPIO_OTG_CTRL_INPUT();
    }
}
#endif
static ssize_t Mt5728_set_vout(struct device* cd, struct device_attribute *attr, const char* buf, size_t len);

static ssize_t brushfirmware(struct device* dev, struct device_attribute* attr, const char* buf, size_t count);
static ssize_t get_reg(struct device* cd, struct device_attribute* attr, char* buf);
static ssize_t set_reg(struct device* cd, struct device_attribute* attr, const char* buf, size_t len);
int Get_adaptertype(void);
static ssize_t get_adapter(struct device* cd, struct device_attribute* attr, char* buf);
static void mt5728_set_pmic_input_current_ichg(int input_current);
static ssize_t fast_charging_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t count);
static ssize_t updata_txFW(struct device* cd, struct device_attribute* attr, char* buf);
static ssize_t mt5728_get_reverse_charger(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t mt5728_set_reverse_charger(struct device* cd, struct device_attribute *attr, const char* buf, size_t len);
static ssize_t mt5728_get_otg(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t mt5728_set_otg(struct device* cd, struct device_attribute *attr, const char* buf, size_t len);
static ssize_t mt5728_get_hwen(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t mt5728_set_hwen(struct device* cd, struct device_attribute *attr, const char* buf, size_t len);
static ssize_t mt5728_show_flag(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t mt5728_set_flag(struct device* cd, struct device_attribute *attr, const char* buf, size_t len);
static ssize_t Mt5728_set_vout(struct device* cd, struct device_attribute *attr, const char* buf, size_t len);
static ssize_t mt5728_get_rxdetect(struct device* cd,struct device_attribute *attr, char* buf);
void fast_vfc(int vol);

static DEVICE_ATTR(fast_charging, S_IRUGO | S_IWUSR, NULL, fast_charging_store);
static DEVICE_ATTR(reverse_charger, 0660, mt5728_get_reverse_charger,  mt5728_set_reverse_charger);
static DEVICE_ATTR(otg, 0660, mt5728_get_otg,  mt5728_set_otg);
// static DEVICE_ATTR(otp, 0660, mt5728_get_otp,  mt5728_set_otp);
static DEVICE_ATTR(mt5728_en, 0660, mt5728_get_hwen,  mt5728_set_hwen);
static DEVICE_ATTR(mt5728_flag, 0660, mt5728_show_flag,  mt5728_set_flag);
static DEVICE_ATTR(epp_set_vout, 0660, NULL,  Mt5728_set_vout);
static DEVICE_ATTR(rxdetect, 0660, mt5728_get_rxdetect,  NULL);
static DEVICE_ATTR(reg, 0660, get_reg, set_reg);
static DEVICE_ATTR(adapter_type,S_IRUGO | S_IWUSR,get_adapter,NULL);
static DEVICE_ATTR(brushFW,S_IRUGO | S_IWUSR,NULL,brushfirmware);
static DEVICE_ATTR(TxFirmware,S_IRUGO | S_IWUSR,updata_txFW,NULL);

static ssize_t Mt5728_get_vout(void);
static u16 crc_ccitt(u16 crc, u8 const *buffer, size_t len);
static inline u16 crc_firmware(u16 poly1, u16 sed, u8 *buf, u32 n);
static void ech_wls_chrg_set_psy_info(struct power_supply *psy, enum power_supply_property psp, int data);
static int set_charger_type(void);
// static int ech_wls_chrg_get_psy_info(struct power_supply *psy, 
// 			enum power_supply_property psp, int *data);
static int ech_wls_chrg_set_property(struct power_supply *psy,
			enum power_supply_property psp, const union power_supply_propval *val);
extern void mt_usb_vbus_on(int delay);
extern void mt_usb_vbus_off(int delay);

static void mt5728_ap_open_otg_boost (bool en) {
	//mt_usb_vbus_on(10);
    // charger_dev_enable_otg(primary_charger, en);
	if(en){
		mt_usb_vbus_on(0);
	}
	else{
		mt_usb_vbus_off(0);
	}
}

static void wireless_charge_wake_lock(void)
{
    if (!mt5728_wls_wake_lock.active) {
        __pm_stay_awake(&mt5728_wls_wake_lock);
    }
}
static void wireless_charge_wake_unlock(void)
{
    if (mt5728_wls_wake_lock.active) {
        __pm_relax(&mt5728_wls_wake_lock);
    }
}

/**
 * [mt5728_display_slow_wls_icon android java app display slow wireless icon]
 * @param en [1:display ,0:put out]
 */
static void mt5728_display_slow_wls_icon(int en) {
  if(en) {
     set_charger_type();
     printk(KERN_ALERT "[%s] mt5728_display_slow_wls_icon on\n", __func__);
  } else {
     set_charger_type();
     printk(KERN_ALERT "[%s] mt5728_display_slow_wls_icon off\n", __func__);
  }
}

/**
 * [mt5728_display_qucik_wls_icon android java app display quick wireless icon]
 * @param en [1:display ,0:put out]
 */
static void mt5728_display_qucik_wls_icon(int en) {
  if(en) {
     printk(KERN_ALERT "[%s] mt5728_display_qucik_wls_icon on\n", __func__);
  } else {
     printk(KERN_ALERT "[%s] mt5728_display_qucik_wls_icon off\n", __func__);
  }

}

/**
 * [mt5728_is_otg_plugin]
 * @return  [1:otg in ,0:otg out]
 */
//extern up_otg_flag;
int mt5728_is_otg_plugin(bool otg_plugin) {
    return otg_plugin;//up_otg_flag;//usb_otg_online && rt1711_otg_detect;
}

void ech_wls_set_chrg_current(int input_current, int cur)
{
	// if(input_current == cur_last){
	// 	return;
	// }

	ech_wls_chrg_set_psy_info(chip->wl_psy, POWER_SUPPLY_PROP_CURRENT_MAX, input_current);
	ech_wls_chrg_set_psy_info(chip->wl_psy, POWER_SUPPLY_PROP_CURRENT_NOW, cur);
	//ech_wls_log(ECH_LOG_DEBG, "[%s] Set Input Current: %d mA.\n", __func__, cur);

	// cur_last = input_current;
}

/**
 * [mt5728_set_pmic_input_current_ichg description]
 * @param input_current [mA]
 */
static void mt5728_set_pmic_input_current_ichg(int input_current) {
    printk(KERN_ALERT "%s() input_current:%d, charger_current:%d\n", __func__,input_current, chrg_info.charger_current);
    if((input_current == cur_last)&&(chrg_info.charger_current == last_ichg)){
        return;
    }
    primary_charger = get_charger_by_name("primary_chg");
	
	if (!primary_charger) {
		pr_notice("%s: get primary charger device failed\n", __func__);
	}
    //customer realizes PMIC input current control here
    // ech_wls_chrg_set_psy_info(chip->wl_psy, POWER_SUPPLY_PROP_CURRENT_MAX, input_current);
    // ech_wls_chrg_set_psy_info(chip->wl_psy, POWER_SUPPLY_PROP_CURRENT_NOW, input_current);
    chrg_info.input_current_max = input_current;
    ech_wls_set_chrg_current(chrg_info.input_current_max,chrg_info.charger_current);
    charger_dev_set_input_current(primary_charger,input_current * 1000);
    charger_dev_set_charging_current(primary_charger,chrg_info.charger_current * 1000);
    printk(KERN_ALERT "%s() set input_current:%d, charger_current:%d\n", __func__,input_current, chrg_info.charger_current);
    cur_last = input_current;
    last_ichg = chrg_info.charger_current;
}

// static int cur_last = 0;


static void ech_wls_chrg_set_psy_info(struct power_supply *psy,
				     enum power_supply_property psp, int data)
{
	union power_supply_propval val;
	int ret;

	val.intval = data;

	ret = power_supply_set_property(psy, psp, &val);
	if(ret < 0){
		ech_wls_log(ECH_LOG_ERR, "set psy failed.\n");
	}
}

int is_wireless_chipen_pin_high(void) {
    return GET_GPIO_WIRELESS_EN();
}

static enum power_supply_property ech_wls_chrg_props[] = {
	POWER_SUPPLY_PROP_CURRENT_MAX,

//	POWER_SUPPLY_PROP_CHARGING_ENABLED,

	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
//	POWER_SUPPLY_PROP_VOUT_NOW,
//	POWER_SUPPLY_PROP_VRECT,
//	POWER_SUPPLY_PROP_IRECT,
//	POWER_SUPPLY_PROP_TEMP_NTC,
//	POWER_SUPPLY_PROP_TEMP_WARM_NTC,
//	POWER_SUPPLY_PROP_TEMP_HOT_NTC,
//	POWER_SUPPLY_PROP_TEMP_WARM_BATTERY,
//	POWER_SUPPLY_PROP_TEMP_HOT_BATTERY,
	POWER_SUPPLY_PROP_CALL_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};


static int ech_wls_chrg_set_property(struct power_supply *psy,
                    enum power_supply_property psp,
                    const union power_supply_propval *val) 
{       
	printk(KERN_ALERT, "[%s] psp = %d.\n", __func__, psp);

	switch(psp){
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		power_supply_changed(chip->wl_psy);
		break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
		power_supply_changed(chip->wl_psy);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		//enable_charging(val->intval);
		power_supply_changed(chip->wl_psy);
		break;
	case POWER_SUPPLY_PROP_CALL_STATUS:
		if(val->intval > 0)
			chrg_info.call_status = 1;
		else
			chrg_info.call_status = 0;
		break;
	default:
		return -EPERM;
	}

	return 0;
}

static int ech_wls_chrg_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	ech_wls_log(ECH_LOG_DEBG, "[%s] psp = %d.\n", __func__, psp);

//if (!psy || !(&psy->dev) || !val) {
//		ech_wls_log(ECH_LOG_ERR, "[%s] Not find any psy!\n", __func__);
//		return -ENODEV;
//	}

	val->intval = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chrg_info.input_current_max;
		break;
        case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = chrg_info.charger_current;
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = chrg_info.work_en;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		// val->intval = chrg_info.wls_online;
        	val->intval = wls_work_online;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		// val->intval = chrg_info.wls_online;
    	printk("wls_work_online =  %d\n",wls_work_online);
		val->intval = wls_work_online; //modify
		break;
		/*
	case POWER_SUPPLY_PROP_VOUT_NOW:
		val->intval = chrg_info.vout;
		break;
	case POWER_SUPPLY_PROP_VRECT:
		val->intval = chrg_info.vrect;
		break;
	case POWER_SUPPLY_PROP_IRECT:
		val->intval = chrg_info.irect;
		break;
        case POWER_SUPPLY_PROP_TEMP_NTC:
		val->intval = chrg_info.temp_ntc;
		break;
	case POWER_SUPPLY_PROP_TEMP_WARM_NTC:
		//val->intval = chip->temp_warm_ntc;
		break;
	case POWER_SUPPLY_PROP_TEMP_HOT_NTC:
		//val->intval = chip->temp_hot_ntc;
		break;
	case POWER_SUPPLY_PROP_TEMP_WARM_BATTERY:
		//val->intval = chip->temp_warm_battery;
		break;
	case POWER_SUPPLY_PROP_TEMP_HOT_BATTERY:
		//val->intval = chip->temp_hot_battery;
		break;
		*/
	case POWER_SUPPLY_PROP_CALL_STATUS:
		val->intval = chrg_info.call_status;
                break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void ech_wls_charger_external_power_changed(struct power_supply *psy)
{
	printk(KERN_ALERT "ech_wls_charger_external_power_changed.\n");	

	return;
}

static int ech_wls_chrg_property_is_writeable(struct power_supply *psy,
                                                enum power_supply_property psp)
{
	switch (psp){
	case POWER_SUPPLY_PROP_CURRENT_MAX:
//	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
//	case POWER_SUPPLY_PROP_TEMP_WARM_NTC:
//	case POWER_SUPPLY_PROP_TEMP_HOT_NTC:
//	case POWER_SUPPLY_PROP_TEMP_WARM_BATTERY:
//	case POWER_SUPPLY_PROP_TEMP_HOT_BATTERY:
	case POWER_SUPPLY_PROP_CALL_STATUS:
    	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return 1;

	default:
		break;
	}

	return 0;
}

static char *ech_supplicants[] = {
	"battery",
        "usb",
	"charger",
	//"dc",
};

static char *ech_supplied_from[] = {
	"battery",
	"usb",
};

static void ech_wls_chrg_get_psys(struct mt5728_dev *chip)
{

	if (!chip) {
		ech_wls_log(ECH_LOG_ERR, "[%s] Chip not ready.\n", __func__);
		return;
	}

	chip->batt_psy = power_supply_get_by_name("battery");
	if (!chip->batt_psy)
                ech_wls_log(ECH_LOG_ERR, "[%s] BATT PSY Not Found.\n", __func__);

	chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy)
                ech_wls_log(ECH_LOG_ERR, "[%s] USB PSY Not Found.\n", __func__);
}

void ech_wls_chrg_info_init(void)
{
    // return;
	memset(&chrg_info, 0, sizeof(chrg_info));

	chip->state = ECH_WLS_CHRG_WAIT;

	chip->resume_vbatt = 4300;

	ech_wls_chrg_get_psys(chip);

	chrg_info.work_en = 1;
}

void updata_wireless_online(void)
{
    // wls_work_online = wls_work_online;
	// if(MT5725_wls_get_online() == 1)
	// {
	// 	wls_work_online = 1;
	// }
	// else
	// {
	// 	wls_work_online = 0;
	// }
}

// static void ech_wls_create_device_node(struct device *dev)
// {
	// device_create_file(dev, &dev_attr_ver_drv);
	// device_create_file(dev, &dev_attr_ver_fw);

	// device_create_file(dev, &dev_attr_reg_addr);
	// device_create_file(dev, &dev_attr_reg_data);

	// device_create_file(dev, &dev_attr_pwr_src);
	// device_create_file(dev, &dev_attr_protocol);
// }

static int set_charger_type(void)
{
	int ret;
	union power_supply_propval val;
	struct power_supply *psy = power_supply_get_by_name("charger");	
    	pr_info("mt5728 set_charger_type psy: %d.\n", psy);
    	if (psy == NULL)
    	{
       		pr_info("mt5728 power_supply_get_by_name error.\n");
        	return -1;
   	}
	val.intval = wls_work_online;
	pr_info("mt5728 set_charger_type: %d.\n", val.intval);
	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);

	if(val.intval)
	{
		val.intval = WIRELESS_CHARGER;
		ret = power_supply_set_property(psy,POWER_SUPPLY_PROP_CHARGE_TYPE, &val);
		if (!ret) {
			pr_info("mt5728 POWER_SUPPLY_PROP_CHARGE_TYPE: %d.\n", val.intval);
			return val.intval;
		} else {
			return 0;
		}
	}else if(GET_GPIO_WIRELESS_EN() == 0)
	{
		ret = power_supply_set_property(psy,POWER_SUPPLY_PROP_CHARGE_TYPE, &val);
	}else if(GET_GPIO_WIRELESS_EN() == 1)
	{
		val.intval = CHARGER_UNKNOWN;
		ret = power_supply_set_property(psy,POWER_SUPPLY_PROP_CHARGE_TYPE, &val);
	}
	return 0;
}

static int mt5728_gpio_init(void) {
    int ret = 0;
    //trx
    struct device_node *usb_node = NULL;
    usb_node = of_find_compatible_node(NULL, NULL, "mediatek,mt5728");
    if (usb_node != NULL) {
        //-------------------chip en------------------
        chip->chip_en_gpio = of_get_named_gpio(usb_node, "chip_en_gpio", 0);
        if (!gpio_is_valid(chip->chip_en_gpio))
        {
             printk(KERN_ALERT "gpio_is_valid error:%d",chip->chip_en_gpio);
        }

        if (chip->chip_en_gpio < 0)
        {
            printk(KERN_ALERT "%s get chip->chip_en_gpio failed:%d\n", __func__,chip->chip_en_gpio);
        }
        ret = gpio_request(chip->chip_en_gpio, "chip_en_gpio"); /*request gpio*/
        if (ret < 0) {
            printk(KERN_ALERT "%s chip->chip_en_gpio gpio_request failed, gpio=%d\n", __func__, chip->chip_en_gpio);
        }else {
            printk(KERN_ALERT "%s set chip->chip_en_gpio success, gpio=%d\n", __func__, chip->chip_en_gpio);
        }
        gpio_direction_input(chip->chip_en_gpio); /*set direction is input*/
        
        //-------------------eint_wpc------------------
        chip->irq_gpio= of_get_named_gpio(usb_node, "eint_wpc", 0);
        if (!gpio_is_valid(chip->irq_gpio))
        {
             printk(KERN_ALERT "gpio_is_valid error:%d",chip->irq_gpio);
        }

        if (chip->irq_gpio < 0)
        {
            printk(KERN_ALERT "%s get chip->irq_gpio failed:%d\n", __func__,chip->irq_gpio);
        }
        ret = gpio_request(chip->irq_gpio, "eint_wpc"); 
         if (ret < 0) {
             printk(KERN_ALERT "%s chip->irq_gpio_request failed, gpio=%d\n", __func__, chip->irq_gpio);
         } else {
            printk(KERN_ALERT "%s set chip->irq_gpio success, gpio=%d\n", __func__, chip->irq_gpio);
         }
        gpio_direction_input(chip->irq_gpio);
        //-----------------------powergood_gpio-------------------------
        chip->powergood_gpio = of_get_named_gpio(usb_node, "powergood_gpio", 0);
        if (!gpio_is_valid(chip->powergood_gpio))
        {
             printk(KERN_ALERT "powergood_gpio_is_valid error:%d",chip->powergood_gpio);
        }
        if (chip->powergood_gpio < 0)
        {
            printk(KERN_ALERT "%s get chip->powergood_gpio failed:%d\n", __func__,chip->powergood_gpio);
        }
        ret = gpio_request(chip->powergood_gpio, "powergood_gpio"); /*request gpio*/
        if (ret < 0) {
            printk(KERN_ALERT "%s chip->powergood_gpio gpio_request failed, gpio=%d\n", __func__, chip->powergood_gpio);
        } else {
            printk(KERN_ALERT "%s set chip->powergood_gpio success, gpio=%d\n", __func__, chip->powergood_gpio);
        }
        gpio_direction_input(chip->powergood_gpio); /*set direction is input*/
	gpio_set_value(chip->powergood_gpio, 1); //powergood input high
        //-------------------otg_ctrl_gpio------------------
        #if 0
        chip->otg_ctrl_gpio = of_get_named_gpio(usb_node, "otg_ctrl_gpio", 0);
        if (!gpio_is_valid(chip->otg_ctrl_gpio))
        {
            printk(KERN_ALERT "otg_ctrl_gpio_is_valid error:%d",chip->otg_ctrl_gpio);
        }
        if (chip->otg_ctrl_gpio < 0)
        {
            printk(KERN_ALERT "%s get chip->otg_ctrl_gpio failed:%d\n", __func__,chip->otg_ctrl_gpio);
        }
        ret = gpio_request(chip->otg_ctrl_gpio, "otg_ctrl_gpio"); /*request gpio*/
        if (ret < 0) {
            printk(KERN_ALERT "%s chip->otg_ctrl_gpio gpio_request failed, gpio=%d\n", __func__, chip->otg_ctrl_gpio);
        } else {
            printk(KERN_ALERT "%s set chip->otg_ctrl_gpio success, gpio=%d\n", __func__, chip->otg_ctrl_gpio);
        }
        gpio_direction_input(chip->otg_ctrl_gpio); /*set direction is input*/
        printk(KERN_ALERT "%s set chip->otg_ctrl_gpio input success, gpio=%d\n", __func__, chip->otg_ctrl_gpio);
	gpio_set_value(chip->otg_ctrl_gpio,0);
        printk(KERN_ALERT "%s get chip->otg_ctrl_gpio status =%d\n", __func__,gpio_get_value(chip->otg_ctrl_gpio));
        #endif
        //-------------------ldo_ctrl_gpio------------------  
        chip->ldo_ctrl_gpio = of_get_named_gpio(usb_node, "ldo_ctrl_gpio", 0);
        if (!gpio_is_valid(chip->ldo_ctrl_gpio))
        {
            printk(KERN_ALERT "ldo_ctrl_gpio_is_valid error:%d",chip->ldo_ctrl_gpio);
        }
        if (chip->ldo_ctrl_gpio < 0)
        {
            printk(KERN_ALERT "%s get chip->ldo_ctrl_gpio failed:%d\n", __func__,chip->ldo_ctrl_gpio);
        }
        ret = gpio_request(chip->ldo_ctrl_gpio, "ldo_ctrl_gpio"); /*request gpio*/
        if (ret < 0) {
            printk(KERN_ALERT "%s ldo_ctrl_gpio gpio_request failed, gpio=%d\n", __func__, chip->ldo_ctrl_gpio);
            return ret;
        }else{
            printk(KERN_ALERT "%s set chip->ldo_ctrl_gpio success, gpio=%d\n", __func__, chip->ldo_ctrl_gpio);
	}
        gpio_direction_input(chip->ldo_ctrl_gpio); /*set direction is input*/
        //-------------------trx_mode_gpio------------------  
        chip->trx_mode_gpio = of_get_named_gpio(usb_node, "trx_mode_gpio", 0);
        if (!gpio_is_valid(chip->trx_mode_gpio))
        {
            printk(KERN_ALERT "trx_mode_gpio_is_valid error:%d",chip->trx_mode_gpio);
        }
        if (chip->trx_mode_gpio < 0)
        {
            printk(KERN_ALERT "%s get chip->trx_mode_gpio failed:%d\n", __func__,chip->trx_mode_gpio);
        }
        ret = gpio_request(chip->trx_mode_gpio, "trx_mode_gpio"); /*request trx gpio*/
        if (ret < 0) {
            printk(KERN_ALERT "%s chip->trx_mode_gpio gpio_request failed, gpio=%d\n", __func__, chip->trx_mode_gpio);
        } else {
            printk(KERN_ALERT "%s set chip->trx_mode_gpio success, gpio=%d\n", __func__, chip->trx_mode_gpio);
        }
        gpio_direction_output(chip->trx_mode_gpio, 0); /*set direction is output*/

        printk(KERN_ALERT "%s set gpio done !\n",__func__);
    	} else {
        	printk(KERN_ALERT "error gpio\n");
    	}
    	return ret;
}

static unsig32 SizeofPkt(u8 hdr) {
    if (hdr < 0x20)
        return 1;

    if (hdr < 0x80)
        return (2 + ((hdr - 0x20) >> 4));

    if (hdr < 0xe0)
        return (8 + ((hdr - 0x80) >> 3));

    return (20 + ((hdr - 0xe0) >> 2));
}

static int mt5728_read(struct mt5728_dev* di, u16 reg, u8* val) {
    unsigned int temp;
    int rc;

    rc = regmap_read(di->regmap, reg, &temp);
    if (rc >= 0)
        *val = (u8)temp;

    return rc;
}

static int mt5728_write(struct mt5728_dev* di, u16 reg, u8 val) {
    int rc = 0;

    rc = regmap_write(di->regmap, reg, val);
    if (rc < 0)
        dev_err(di->dev, "%s error: %d\n",__func__, rc);

    return rc;
}

static int mt5728_read_buffer(struct mt5728_dev* di, u16 reg, u8* buf, unsig32 size) {
  
    return regmap_bulk_read(di->regmap, reg, buf, size);
}

static int mt5728_write_buffer(struct mt5728_dev* di, u16 reg, u8* buf, unsig32 size) {
    int rc = 0;
    
    rc = regmap_bulk_write(di->regmap, reg, buf, size);
    // while (size--) {
    //     rc = di->bus.write(di, reg++, *buf++);
    //     if (rc < 0) {
    //         dev_err(di->dev, "%s error: %d\n",__func__, rc);
    //         return rc;
    //     }
    // }
    return rc;
}

static void mt5728_removed_form_tx(void) {
    wls_work_online = 0;
    chrg_info.wls_online = 0;
    mt5728_ldo_on_flag = 0;
    // set_charger_type();
    mt5728_display_slow_wls_icon(0);
    mt5728_display_qucik_wls_icon(0);
    enable_vbus_ovp(true);
    printk(KERN_ALERT "mt5728_removed_form_tx\n");
}

static void mt5728_sram_write(unsig32 addr, u8 *data,unsig32 len) {
    unsig32 offset,length,size;
    offset = 0;
    length = 0;
    size = len;
    printk(KERN_ALERT "[%s] Length to write:%d\n",__func__, len);
    while(size > 0) {
        if(size > SRAM_PAGE_SIZE) {
            length = SRAM_PAGE_SIZE;
        } else {
            length = size;
        }
        printk(KERN_ALERT "[%s] Length of this write :%d\n",__func__,length);
		mt5728_write_buffer(mte, addr + offset ,data+offset, length);
        size -= length;
        offset += length;
        msleep(2);
    }
    printk(KERN_ALERT "[%s] Write completion\n",__func__);
}

static void mt5728_run_pgm_fw(void) {
    vuc val;
    val.value  = MT5728_WDG_DISABLE;
    mt5728_write_buffer(mte, MT5728_PMU_WDGEN_REG, val.ptr, 2);
    mt5728_write_buffer(mte, MT5728_PMU_WDGEN_REG, val.ptr, 2);
	mt5728_write_buffer(mte, MT5728_PMU_WDGEN_REG, val.ptr, 2);
    val.value  = MT5728_WDT_INTFALG;
    mt5728_write_buffer(mte, MT5728_PMU_FLAG_REG, val.ptr, 2);
    val.value = MT5728_KEY;
    mt5728_write_buffer(mte, MT5728_SYS_KEY_REG, val.ptr, 2);
	val.value = 0X08;
	mt5728_write_buffer(mte, MT5728_CODE_REMAP_REG, val.ptr, 2);
	val.value = 0x0FFF;
    mt5728_write_buffer(mte, MT5728_SRAM_REMAP_REG, val.ptr, 2);
    msleep(50);
    mt5728_sram_write(0x1800,(u8 *)mt5728_pgm_bin,sizeof(mt5728_pgm_bin));
    msleep(50);
    val.value = MT5728_KEY;
    mt5728_write_buffer(mte, MT5728_SYS_KEY_REG, val.ptr, 2);
    val.value = MT5728_M0_RESET;
    mt5728_write_buffer(mte, MT5728_M0_CTRL_REG, val.ptr, 2);
    msleep(50);
	printk(KERN_ALERT "[%s]  finish  \n",__func__);
}

static u8 mt5728_mtp_read(unsig32 addr, u8 * buf , unsig32 size, u8 mode) {
    unsig32 length ,i,status,times;
    vuc val;
    Pgm_Type_t pgm;
    printk(KERN_ALERT "[%s] parameter size :%d\n",__func__,size);
    length = (size+(MTP_BLOCK_SIZE-1))/MTP_BLOCK_SIZE*MTP_BLOCK_SIZE;
    mt5728_run_pgm_fw();
	printk(KERN_ALERT "[%s] Calculate the length to read:%d\n",__func__,length);
    for (i = 0; i < length/MTP_BLOCK_SIZE; ++i) {
        pgm.length = MTP_BLOCK_SIZE;
        pgm.addr = addr+i*MTP_BLOCK_SIZE;
        pgm.status = PGM_STATUS_READY;
        val.value = pgm.status;
        mt5728_write_buffer(mte,PGM_STATUS_ADDR,val.ptr,2);
        val.value = pgm.addr;
        mt5728_write_buffer(mte,PGM_ADDR_ADDR,val.ptr,2);
        val.value = pgm.length;
        mt5728_write_buffer(mte,PGM_LENGTH_ADDR,val.ptr,2);
        val.value = PGM_STATUS_READ;
        mt5728_write_buffer(mte,PGM_STATUS_ADDR,val.ptr,2);
        msleep(50);
        mt5728_read_buffer(mte, PGM_STATUS_ADDR, val.ptr, 2);
        status = val.ptr[0];
        times = 0;
        while(status == PGM_STATUS_READ) {
            msleep(50);
            mt5728_read_buffer(mte, PGM_STATUS_ADDR, val.ptr, 2);
			printk(KERN_ALERT "[%s] Program reading",__func__);
            status = val.ptr[0];
            times+=1;
            if (times>100) {
                printk(KERN_ALERT "[%s] error! Read OTP TImeout\n",__func__);
                return FALSE;
            }
        }
        if (status == PGM_STATUS_PROGOK) {
            printk(KERN_ALERT "[%s] PGM_STATUS_PROGOK\n",__func__);
			mt5728_read_buffer(mte, PGM_DATA_ADDR, &buf[MTP_BLOCK_SIZE*i], MTP_BLOCK_SIZE);
        } else {
		    printk(KERN_ALERT "[%s] OtpRead error , status = 0x%02x\n",__func__,status);
            return FALSE;
        }
        if (mode == TRUE) {
        }
    }
	return TRUE;
}

static u8 mt5728_mtp_write(unsig32 addr, u8 * buf , unsig32 len) {
    unsig32 offset;
    unsig32 write_size ,status,times;
    unsig32 write_retrycnt;
    s32 size;
    int i;
    vuc val;
    Pgm_Type_t pgm;
    size = len;
    offset = 0;
    write_size = 0;
    printk(KERN_ALERT "[%s] Size to write:%d\n",__func__,size);
    mt5728_run_pgm_fw();
    write_retrycnt = 8;
    while(size>0) {
        if (size>MTP_BLOCK_SIZE) {
            pgm.length = MTP_BLOCK_SIZE;
            write_size = MTP_BLOCK_SIZE;
        } else {
            pgm.length = size;
            write_size = size;
        }
        pgm.addr = addr+offset;
        pgm.cs = pgm.addr;
        pgm.status = PGM_STATUS_READY;
        for (i = 0; i < pgm.length; ++i) {
            pgm.data[i] = buf[offset + i];
            pgm.cs += pgm.data[i];
        }
        // zero_cnt = 0;
        // for (i = (pgm.length -1); i >= 0; --i) {
        //     if (pgm.data[i] != 0x00) {
        //         break;
        //     }
        //     zero_cnt+=1;
        // }
        // if(zero_cnt == pgm.length) {
        //     size-=write_size;
        //     offset+=write_size;
        //     continue;
        // }
        // pgm.length-=zero_cnt;
        pgm.cs+=pgm.length;
        val.value = pgm.status;
        mt5728_write_buffer(mte,PGM_STATUS_ADDR,val.ptr,2);
        val.value = pgm.addr;
        mt5728_write_buffer(mte,PGM_ADDR_ADDR,val.ptr,2);
        val.value = pgm.length;
        mt5728_write_buffer(mte,PGM_LENGTH_ADDR,val.ptr,2);
        val.value = pgm.cs;
        mt5728_write_buffer(mte,PGM_CHECKSUM_ADDR,val.ptr,2);
        mt5728_write_buffer(mte, PGM_DATA_ADDR, pgm.data, pgm.length);
        val.value = PGM_STATUS_WMTP;
        mt5728_write_buffer(mte,PGM_STATUS_ADDR,val.ptr,2);
        msleep(50);
        mt5728_read_buffer(mte, PGM_STATUS_ADDR, val.ptr, 2);
        status = val.ptr[0];
        times = 0;
        while(status == PGM_STATUS_WMTP) {
            msleep(50);
            mt5728_read_buffer(mte, PGM_STATUS_ADDR, val.ptr, 2);
            status = val.ptr[0];
            printk(KERN_ALERT "[%s] Program writing\n",__func__);
            times+=1;
            if (times > 100) {
                printk(KERN_ALERT "[%s] Program write timeout\n",__func__);
                return FALSE;
            }
        }
        if (status == PGM_STATUS_PROGOK) {
            size-=write_size;
            offset+=write_size;
            printk(KERN_ALERT "[%s] PGM_STATUS_PROGOK\n",__func__);
        } else if (status == PGM_STATUS_ERRCS) {
            if (write_retrycnt > 0) {
                write_retrycnt--;
                printk(KERN_ALERT "[%s]  ERRCS write_retrycnt:%d\n",__func__,write_retrycnt);
                continue;
            } else {
            printk(KERN_ALERT "[%s] PGM_STATUS_ERRCS\n",__func__);
            return FALSE;
            }
        } else if (status == PGM_STATUS_ERRPGM) {
            if (write_retrycnt > 0) {
                write_retrycnt--;
                printk(KERN_ALERT "[%s] ERRPGM write_retrycnt:%d\n",__func__,write_retrycnt);
                continue;
            } else {
            printk(KERN_ALERT "[%s] PGM_STATUS_ERRPGM\n",__func__);
            return FALSE;
            }
        } else {
            if (write_retrycnt > 0) {
                write_retrycnt--;
                printk(KERN_ALERT "[%s] NUKNOWN write_retrycnt:%d\n",__func__,write_retrycnt);
                continue;
            } else {
                printk(KERN_ALERT "[%s] PGM_STATUS_NUKNOWN \n",__func__);
                return FALSE;
            }
        }
    }
	return TRUE;
}

static u8 mt5728_mtp_write_check(unsig32 flagaddr,u16 crc) {
    u8 i;
    u8 otpwrite_flagdata[4];
    u8 *otpwrite_flagread;
	otpwrite_flagread = kmalloc(1064, GFP_KERNEL);
    otpwrite_flagdata[0] = crc % 256;
    otpwrite_flagdata[1] = crc / 256;
    otpwrite_flagdata[2] = crc % 256;
    otpwrite_flagdata[3] = crc / 256;
    mt5728_mtp_read(flagaddr, otpwrite_flagread,4,1);
    for (i = 0; i < 4; ++i) {
        if (otpwrite_flagread[i] != otpwrite_flagdata[i]) {
            printk(KERN_ALERT "[%s] MT5728 MTP  Flag not written: %d\n",__func__,i);
            return FALSE;
        }
    }
    printk(KERN_ALERT "[%s] MT5728 MTP Flag has been written",__func__);
	kfree(otpwrite_flagread);
    return TRUE;
}

void mt5728_write_mtpok_flag(unsig32 flagaddr,u16 crc) {
    u8 otpwrite_flagdata[4] ;
    otpwrite_flagdata[0] = crc % 256;
    otpwrite_flagdata[1] = crc / 256;
    otpwrite_flagdata[2] = crc % 256;
    otpwrite_flagdata[3] = crc / 256;
    if (flagaddr < 0x3700) {
        printk(KERN_ALERT "[%s] Address out of range\n",__func__);
        return;
    }
    mt5728_mtp_write(flagaddr,otpwrite_flagdata,4);
}

static u8 mt5728_mtp_verify(u32 addr,u8 * data,u32 len) {
#if 0
    // u8 *mtp_read_temp;
    // u32 i;
	// mtp_read_temp = kmalloc(1024*17, GFP_KERNEL);
	// if(mtp_read_temp == NULL){
	// 	printk(KERN_ALERT "[%s] devm_kzalloc Error\n",__func__);
	// 	return FALSE;
	// }
    // if (len > 1024*17) {
    //     printk(KERN_ALERT "[%s] Wrong parameter length\n",__func__);
#else
    vuc val;
    int waitTimeOutCnt;
    int status;
    u16 crcvlaue;
    u16 crcvlaue_chip; 
    Pgm_Type_t pgm;
    // crcvlaue = crc_ccitt(0xFFFF,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin));
    crcvlaue = crc_firmware(0x1021, 0xFFFF, (u8*)MT5728_mtp_bin, sizeof(MT5728_mtp_bin)) & 0xffff;
    val.value = 0;
    mt5728_write_buffer(mte,PGM_ADDR_ADDR,val.ptr,2);
    val.value = sizeof(MT5728_mtp_bin);
    mt5728_write_buffer(mte,PGM_LENGTH_ADDR,val.ptr,2);
    pgm.cs = crcvlaue;
    val.value = pgm.cs;
    mt5728_write_buffer(mte,PGM_CHECKSUM_ADDR,val.ptr,2);
    val.value = (1 << 6);
    mt5728_write_buffer(mte,PGM_STATUS_ADDR,val.ptr,2);
    waitTimeOutCnt = 100;
    while(waitTimeOutCnt--) {
        mt5728_read_buffer(mte, PGM_STATUS_ADDR, val.ptr, 2);
        status = val.ptr[0] | (val.ptr[1] << 8);
        if (status & (1 << 7)) {
        	mt5728_read_buffer(mte, PGM_DATA_ADDR, val.ptr, 2);
        	crcvlaue_chip = val.value;
            printk(KERN_ALERT "[%s]  mt5728_mtp_verify error,crcvlaue_chip:%x,crcvlaue:%x",__func__,crcvlaue_chip,crcvlaue);
            return FALSE;
        }
        //VERIFYOK
        if (status & (1 << 8)) {
            mt5728_read_buffer(mte, PGM_DATA_ADDR, val.ptr, 2);
        	crcvlaue_chip = val.value;
            printk(KERN_ALERT "[%s]  mt5728_mtp_verify success,crcvlaue_chip:%x,crcvlaue:%x",__func__,crcvlaue_chip,crcvlaue);
            return TRUE;
        }
        msleep(60);
    }/**/
    printk(KERN_ALERT "[%s] TimeOut cal_crc :0x%04x\n",__func__,crcvlaue);
    return FALSE;
#endif
}

static inline u16 crc_ccitt_byte(u16 crc, const u8 c)
{
	return (crc >> 8) ^ mt5728_crc_ccitt_table[(crc ^ c) & 0xff];
}

static u16 crc_ccitt(u16 crc, u8 const *buffer, size_t len)
{
	while (len--)
		crc = crc_ccitt_byte(crc, *buffer++);
    return crc;
}

static inline u16 crc_firmware(u16 poly1, u16 sed, u8 *buf, u32 n) {
    u32 i = 0;
    u32 j = 0;
    u16 poly = 0x1021;
    u16 crc  = 0xffff;
    // u32 addr = 0x20000070;
    for (j = 0; j < n; j+=2) {
        crc ^= (buf[j+1] << 8);
        for (i = 0;i < 8; i++) {
            crc = (crc & 0x8000) ? (((crc << 1) & 0xffff) ^ poly) : (crc << 1);
        }
        /* *(u8*)addr = buf[j+1]; */
        /* addr ++; */
        crc ^= (buf[j] << 8);
        for (i = 0;i < 8; i++) {
            crc = (crc & 0x8000) ? (((crc << 1) & 0xffff) ^ poly) : (crc << 1);
        }
        /* *(u8*)addr = buf[j]; */
        /* addr ++; */
        /* *(u32*)0x200000a0 = addr; */
    }
    return crc;
}

/*
  Send proprietary packet to Tx
  Step 1: write data into REG_PPP
  Step 2: write REG_CMD
*/
void mt5728_send_ppp(PktType *pkt) {
    vuc val;
    mt5728_write_buffer(mte, REG_PPP, (u8 *)pkt, SizeofPkt(pkt->header)+1);
    mte->fsk_status = FSK_WAITTING;
    val.value = SEND_PPP;
    mt5728_write_buffer(mte, REG_CMD, val.ptr, 2);
}

EXPORT_SYMBOL(mt5728_send_ppp);

int Get_adaptertype(void){
    PktType eptpkt;
    int count = 0;
    u8 fsk_msg[10];
    eptpkt.header  = PP18;
    eptpkt.cmd     = CMD_ADAPTER_TYPE;
    mt5728_send_ppp(&eptpkt);
    while(mte->fsk_status == FSK_WAITTING){
        msleep(20);
        if((count++) > 50 ){
            printk(KERN_ALERT "[%s] AP system judgement:FSK receive timeout \n",__func__);
            return (-1);
        }
    }
    if(mte->fsk_status == FSK_FAILED){
        printk(KERN_ALERT "[%s] Wireless charging system judgement:FSK receive timeout \n",__func__);
        return (-1);
    }
    if(mte->fsk_status == FSK_SUCCESS){
        mt5728_read_buffer(mte,REG_BC,fsk_msg,10);
        printk(KERN_ALERT "[%s] Information received : 0x%02x 0x%02x 0x%02x \n",__func__,fsk_msg[0],fsk_msg[1],fsk_msg[2]);
    }
    return fsk_msg[2];
}

static ssize_t get_reg(struct device* cd, struct device_attribute* attr, char* buf) {
    vuc val;
    ssize_t len = 0;
    int i = 0;

    for (i = 0; i < INDEX_MAX; i++) {
        if (reg_access[i].flag & REG_RD_ACCESS) {
            mt5728_read_buffer(mte, reg_access[i].addr, val.ptr, 2);
            len += snprintf(buf + len, PAGE_SIZE - len, "reg:%s 0x%04x=0x%04x,%d\n", reg_access[i].name, reg_access[i].addr, val.value,val.value);
        }
    }
    return len;
}

static ssize_t set_reg(struct device* cd, struct device_attribute* attr, const char* buf, size_t len) {
    unsigned int databuf[2];
    vuc val;
    u8 tmp[2];
    u16 regdata;
    int i = 0;
    int ret = 0;

    ret = sscanf(buf, "%x %x", &databuf[0], &databuf[1]);

    if (2 == ret) {
        for (i = 0; i < INDEX_MAX; i++) {
            if (databuf[0] == reg_access[i].addr) {
                if (reg_access[i].flag & REG_WR_ACCESS) {
                    // val.ptr[0] = (databuf[1] & 0xff00) >> 8;
                    val.value = databuf[1];
                    // val.ptr[1] = databuf[1] & 0x00ff; //big endian
                    if (reg_access[i].flag & REG_BIT_ACCESS) {
                        mt5728_read_buffer(mte, databuf[0], tmp, 2);
                        regdata = tmp[0] << 8 | tmp[1];
                        val.value |= regdata;
                        printk(KERN_ALERT "get reg: 0x%04x  set reg: 0x%04x \n", regdata, val.value);
                        mt5728_write_buffer(mte, databuf[0], val.ptr, 2);
                    } else {
                        printk(KERN_ALERT "Set reg : [0x%04x]  0x%x \n", databuf[0], val.value);
                        mt5728_write_buffer(mte, databuf[0], val.ptr, 2);
                    }
                }
                break;
            }
        }
    }else{
        printk(KERN_ALERT "Error \n");
    }
    return len;
}

static ssize_t fast_charging_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t count) {
    vuc val;
    int error;
    unsigned int a;
    error = kstrtouint(buf, 10, &a);
    val.value = (unsigned short)a;

    if (error)
        return error;
    if ((val.value < 0) || (val.value > 20000)) {
        printk(KERN_ALERT "[%s] MT5728 Parameter error\n",__func__);
        return count;
    }
    // get_charger_by_name("primary_chg");
    // charger_dev_set_input_current(info->chg1_dev, pdata->input_current_limit);
	// charger_dev_set_charging_current(info->chg1_dev,pdata->charging_current_limit);
    // fast_vfc(val.value);
    return count;
}

static ssize_t get_adapter(struct device* cd, struct device_attribute* attr, char* buf) {
    ssize_t len = 0;
    int rc;
    rc = Get_adaptertype();
    if(rc == (-1)){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Failed to read adapter type\n", __func__);
    }else if(rc == ADAPTER_NONE ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : Unknown\n", __func__);
    }else if(rc == ADAPTER_SDP ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : SDP\n", __func__);
    }else if(rc == ADAPTER_CDP ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : CDP\n", __func__);
    }else if(rc == ADAPTER_DCP ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : DCP\n", __func__);
    }else if(rc == ADAPTER_QC20 ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : QC2.0\n", __func__);
    }else if(rc == ADAPTER_QC30 ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : QC3.0\n", __func__);
    }else if(rc == ADAPTER_PD ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : PD\n", __func__);
    }else if(rc == ADAPTER_FCP ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : FCP\n", __func__);
    }else if(rc == ADAPTER_SCP ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : SCP\n", __func__);
    }else if(rc == ADAPTER_DCS ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : DC source\n", __func__);
    }else if(rc == ADAPTER_AFC ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : AFC\n", __func__);
    }else if(rc == ADAPTER_PDPPS ){
        len += snprintf(buf + len, PAGE_SIZE - len, "[%s] Adapter type : PD PPS\n", __func__);
    }
    return len;
}

static ssize_t brushfirmware(struct device* dev, struct device_attribute* attr, const char* buf, size_t count) {
    int error;
    int mt5728VoutTemp;
    unsigned int pter;
    error = kstrtouint(buf, 10, &pter);
    mt5728_mtp_write_flag = 1;
    mt5728VoutTemp = Mt5728_get_vout();
    if (mt5728VoutTemp < 0) {
        mt5728_ap_open_otg_boost(true);
    }
    msleep(200);        //Wait for Vout voltage to stabilize
    if(pter == 1){
        printk(KERN_ALERT "[%s]  brush MTP program\n",__func__);
        if(mt5728_mtp_write(0x0000,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin))){
            printk(KERN_ALERT "[%s] Write complete, start verification \n",__func__);
            if (mt5728_mtp_verify(0x0000,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin))) {
                printk(KERN_ALERT "[%s] mt5728_mtp_verify OK \n",__func__);
            } else {
                printk(KERN_ALERT "[%s] mt5728_mtp_verify check program failed \n",__func__);
            }
        }
    }else if(pter == 2){
        u16 crcvlaue;
        crcvlaue = crc_ccitt(0xFFFF,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin));
        printk(KERN_ALERT "[%s] cal_crc :0x%04x\n",__func__,crcvlaue);
        if(mt5728_mtp_write_check(MTP_WRITE_FLAG_ADDR,crcvlaue)==TRUE){
            printk(KERN_ALERT "[%s] mt5728_mtp_write exit \n",__func__);
        }else{
            if(mt5728_mtp_write(0x0000,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin))){
                printk(KERN_ALERT "[%s] Write complete, start verification \n",__func__);
                if (mt5728_mtp_verify(0x0000,(u8 *)MT5728_mtp_bin,sizeof (MT5728_mtp_bin))) {
                    printk(KERN_ALERT "[%s] mt5728_mtp_verify OK \n",__func__);
                    mt5728_write_mtpok_flag(MTP_WRITE_FLAG_ADDR,crcvlaue);
                    printk(KERN_ALERT "[%s] MT5728_write_mtp_flag \n",__func__);
                } else {
                    printk(KERN_ALERT "[%s] mt5728_mtp_verify check program failed \n",__func__);
                }
            }
        }
    }
    if (mt5728VoutTemp < 0) {
        mt5728_ap_open_otg_boost(false);
    }
    printk(KERN_ALERT "[%s] Exit this operation \n",__func__);
    mt5728_mtp_write_flag = 0;
    return count;
}

static void download_txSRAM_code(void) {
    vuc val;
    val.value  = MT5728_WDG_DISABLE;
    mt5728_write_buffer(mte, MT5728_PMU_WDGEN_REG, val.ptr, 2);
    mt5728_write_buffer(mte, MT5728_PMU_WDGEN_REG, val.ptr, 2);
	mt5728_write_buffer(mte, MT5728_PMU_WDGEN_REG, val.ptr, 2);
    val.value = MT5728_WDT_INTFALG;
    mt5728_write_buffer(mte,MT5728_PMU_FLAG_REG,val.ptr,2);
    val.value = MT5728_KEY;
    mt5728_write_buffer(mte,MT5728_SYS_KEY_REG,val.ptr,2);
    val.value = MT5728_M0_HOLD | LBIT(9);
    mt5728_write_buffer(mte,MT5728_M0_CTRL_REG,val.ptr,2);
    msleep(50);
    mt5728_sram_write(0x800,(u8 *)MT572x_TxSRAM_bin,sizeof(MT572x_TxSRAM_bin));
    val.value = 0x02;
    mt5728_write_buffer(mte,MT5728_CODE_REMAP_REG,val.ptr,2);
    val.value = 0xff0f;
    mt5728_write_buffer(mte,MT5728_SRAM_REMAP_REG,val.ptr,2);
    val.value = MT5728_KEY;
    mt5728_write_buffer(mte,MT5728_SYS_KEY_REG,val.ptr,2);
    val.value = MT5728_M0_RESET ;
    mt5728_write_buffer(mte,MT5728_M0_CTRL_REG,val.ptr,2);
    msleep(50);
    printk(KERN_ALERT "[%s] finish\n",__func__);
}

static ssize_t updata_txFW(struct device* cd, struct device_attribute* attr, char* buf) {
    ssize_t len = 0;
    download_txSRAM_code();
    return len;
}

void fastcharge_afc(void) {
    vuc val;
    vuc temp, fclr, scmd;
    scmd.value = 0;

    mt5728_read_buffer(mte, REG_INTFLAG, val.ptr, 2);
    fclr.value = FAST_CHARGE;
    if (val.value & INT_AFC_SUPPORT) {
        printk(KERN_ALERT "MT5728 %s ,version 0.1 Tx support samsung_afc\n", __func__);
        temp.value = 9000;
        mt5728_write_buffer(mte, REG_VFC, temp.ptr, 2);
        scmd.value |= FAST_CHARGE;
        scmd.value |= CLEAR_INT;
        mt5728_write_buffer(mte, REG_INTCLR, fclr.ptr, 2);
        printk(KERN_ALERT "%s,version 0.1 write reg_clr : 0x%04x,\n", __func__, fclr.value);
        mt5728_write_buffer(mte, REG_CMD, scmd.ptr, 2);
        printk(KERN_ALERT "%s,version 0.1 write reg_cmd : 0x%04x,\n", __func__, scmd.value);
    }
}

static struct attribute* mt5728_sysfs_attrs[] = {
    &dev_attr_fast_charging.attr,
    &dev_attr_reg.attr,
    &dev_attr_adapter_type.attr,
    &dev_attr_brushFW.attr,
    &dev_attr_TxFirmware.attr,
    &dev_attr_otg.attr,
    // &dev_attr_otp.attr,
    &dev_attr_mt5728_en.attr,
    &dev_attr_mt5728_flag.attr,
    &dev_attr_epp_set_vout.attr,
    &dev_attr_rxdetect.attr,
    &dev_attr_reverse_charger.attr,
    NULL,
};

static const struct attribute_group mt5728_sysfs_group = {
    .name  = "mt5728group",
    .attrs = mt5728_sysfs_attrs,
};

static const struct regmap_config mt5728_regmap_config = {
    .reg_bits     = 16,
    .val_bits     = 8,
    .max_register = 0xFFFF,
};

void mt5728_typec_host_otg_detect(int detect)
{
    if (detect) {
        usbchip_otg_detect = 1;
    } else {
        usbchip_otg_detect = 0;
    }
}

int is_reverse_charger_online(void)
{
    return (reserse_charge_online);
}

int is_maxic_wls_online(void)
{
    return wls_work_online;
}

static ssize_t mt5728_get_reverse_charger(struct device* cd,struct device_attribute *attr, char* buf)
{
    ssize_t len = 0;

    len += snprintf(buf+len, PAGE_SIZE-len, "reverse_charger en : %d\n", chip->reverse_charger);
    
    return len;
}

void mt5728_soft_reset(void) 
{
    vuc val;
    val.value = MT5728_KEY;
    mt5728_write_buffer(mte, MT5728_SYS_KEY_REG, val.ptr, 1);
    val.ptr[0] = MT5728_M0_RESET;
    mt5728_write_buffer(mte, MT5728_M0_CTRL_REG, val.ptr, 1);
    printk(KERN_ALERT "%s\n", __func__);
    msleep(20);
}

//extern int mt6360_set_vchg(void);
//extern int mt6360_get_vchg(void);
void mt5728_reverse_charge(bool en) 
{
#ifdef __GFTK_REVERSE_CHARGER_PERIOD__
    vuc val;
#endif
    if(en) {
        chip->reverse_charger = 1;
        reserse_charge_online = 1;
        reverse_timeout_cnt = 0;
    	//mt5728_otgen_gpio_ctrl(true);
        printk(KERN_ALERT "zzt[%s] start en =%d",__func__, en);
        // msleep(20000);   //debug,use adb shell on reverse charger
        SET_GPIO_WPC_RTMODE_H();
        printk(KERN_ALERT "zzt[%s] trx_mode_gpio is high",__func__);
        mt5728_ap_open_otg_boost(true);
        charger_dev_set_boost_current_limit(primary_charger, 2000000);
        printk("up drv add wiress start !\n");
        printk(KERN_ALERT "zzt[%s] open_otg_boost",__func__);
        msleep(500);        //Wait for Vout voltage to stabilize
        mt5728_soft_reset();



        #ifdef MT5728_USE_WAKELOCK
        // wake_lock(&mt5728_wls_wake_lock);
        wireless_charge_wake_lock();
        #endif
        schedule_delayed_work(&chip->reverse_charge_work, msecs_to_jiffies(100));
        chip->rxdetect_flag = 0;
        chip->rxremove_flag = 0;
	mt5728_tx_ping_cnt = 0;
        //val.value = (80000/125) - 2;
        //mt5728_write_buffer(mte, 0x0050, val.ptr, 2);   //ping 125k,20210517
        //msleep(200);
        // //val.value = 0x280;//80000/f  125Khz
        // //val.value = 0x29a;//80000/f  120Khz
        // val.value = 0x2d7;//80000/f  110Khz
        // mt5728_write_buffer(mte, 0x0040, val.ptr, 2);
        // //mt5728_read_buffer(mte, 0x0040, val.ptr, 2);
        // //printk(KERN_ALERT " 0x40 val = 0x%04x\n", val.value);
        // // val.value = 0x2f9; //105KHz
        // val.value = 0x378; //90KHz
        // mt5728_write_buffer(mte, 0x003e, val.ptr, 2);
        // val.value = 0x0;
        // mt5728_read_buffer(mte, 0x003e, val.ptr, 2);
        // printk(KERN_ALERT " 0x40 valw = 0x%04x\n", val.value);
#ifdef __GFTK_REVERSE_CHARGER_PERIOD__
	msleep(3000);
	mt5728_read_buffer(mte, REG_RXD_PERI, val.ptr, 2);
	printk("dz-drv: 0x004e old value is %d\n", val.value);
	val.value = 0x320;//f (period = 80000/f)
	mt5728_write_buffer(mte, REG_RXD_PERI, val.ptr, 2);
	mt5728_read_buffer(mte, REG_RXD_PERI, val.ptr, 2);
	printk("dz-drv: 0x004e new value is %d\n", val.value);
#endif
    } else {
    	//mt5728_otgen_gpio_ctrl(false);
        printk(KERN_ALERT "zzt[%s] reverse_charge close by driver\n",__func__);
        SET_GPIO_WPC_RTMODE_L();
        mt5728_ap_open_otg_boost(false);
        chip->reverse_charger = 0;
        reserse_charge_online = 0;
	mt5728_tx_cep_cnt_timeout = 0;
        #ifdef MT5728_USE_WAKELOCK
        // wake_unlock(&mt5728_wls_wake_lock);
        wireless_charge_wake_unlock();
        #endif
    }
}

// static ssize_t mt5728_get_reverse_charger{
//     static ssize_t mt5728_set_reverse_charger(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
// }

static ssize_t mt5728_set_reverse_charger(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
{
    unsigned int databuf[16];
    sscanf(buf,"%d",&databuf[0]);
    if(databuf[0] == 0) {        // OFF
        mt5728_reverse_charge(0);
    } else {                    // ON
        mt5728_reverse_charge(1);
    }
    return len;
}

static ssize_t mt5728_get_hwen(struct device* cd,struct device_attribute *attr, char* buf)
{
    return 0;
    //return sprintf(buf, "%u\n", !gpio_get_value(mte->chip_en));
}

ssize_t mt5728_force_chipen_disable(void) {
    GPIO_WIRELESS_EN_OUTPUT();
    SET_GPIO_WIRELESS_EN_H();
    return 0;
}

ssize_t mt5728_chipen_ctrl_by_hardware(void) {
    GPIO_WIRELESS_EN_INPUT();
    return 0;
}

static ssize_t mt5728_set_hwen(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
{
    unsigned int databuf[16];
    sscanf(buf,"%d",&databuf[0]);
    if(databuf[0] == 0) {        // OFF
        //gpio_set_value(mte->chip_en, 1);
        // chrg_info.work_en = 0;
    } else {                    // ON
       // gpio_set_value(mte->chip_en ,0);
        // chrg_info.work_en = 1;
    }

    return len;
}

static ssize_t Mt5728_set_vout(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
{
    vuc val;
    int error;
    unsigned int a;
    error = kstrtouint(buf, 10, &a);
    val.value = (unsigned short)a;
    
    printk(KERN_ALERT "%s,write reg_cmd : 0x%04x,\n", __func__, val.value);
    mt5728_write_buffer(mte, REG_VOUTSET, val.ptr, 2);
    val.value = VOUT_CHANGE;
    mt5728_write_buffer(mte, REG_CMD, val.ptr, 2);
    printk(KERN_ALERT "%s,write reg_cmd : 0x%04x,\n", __func__, val.value);
    return 0;
}

static ssize_t Mt5728_get_vout(void) {
    vuc vout;
    if(mt5728_read_buffer(mte,REG_VOUT,vout.ptr,2) < 0) {
       return -1;
    }
	//printk(KERN_ALERT "%s,Mt5728_get_vout_old : %d !\n", __func__,mt5728_vout_old);
    if (abs(vout.value - mt5728_vout_old) > 1000) {
       mt5728_vout_old = vout.value;
       printk(KERN_ALERT "%s,Mt5728_get_vout : %dmV !\n", __func__, vout.value);
    }
    return vout.value;
    // return battery_get_vbus();
}

static ssize_t Mt5728_get_vrect(void) {
    vuc vout;
    if(mt5728_read_buffer(mte,REG_VRECT,vout.ptr,2) < 0) {
       return -1;
    }
	//printk(KERN_ALERT "%s,Mt5728_get_vrect_old : %d !\n", __func__,mt5728_vout_old);
    if (abs(vout.value - mt5728_vrect_old) > 1000) {
       mt5728_vrect_old = vout.value;
       printk(KERN_ALERT "%s,Mt5728_get_vrect : %dmV !\n", __func__, vout.value);
    }
    return vout.value;
    // return battery_get_vbus();
}

static ssize_t mt5728_get_rxdetect(struct device* cd,struct device_attribute *attr, char* buf)
{
    return sprintf(buf, "%u\n", chip->rxdetect_flag);
}

static ssize_t mt5728_get_otg(struct device* cd,struct device_attribute *attr, char* buf) {
    return sprintf(buf, "%u\n", chip->otg_flag);
}

static ssize_t mt5728_set_otg(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
{
    unsigned int databuf[16];
    
    sscanf(buf,"%d",&databuf[0]);
    if(databuf[0] == 0) {        // OFF
        mt5728_ap_open_otg_boost(false);
        chip->otg_flag = 0;
    } else {                    // ON
        mt5728_ap_open_otg_boost(true);
        chip->otg_flag = 1;
    }
    return len;
}

ssize_t mt5728_show_flag(struct device* cd,struct device_attribute *attr, char* buf)
{
    int ret = 0;

    ret += sprintf(buf + ret, "%d%d\n", reserse_charge_online, wls_work_online);
    ret += sprintf(buf + ret, "powergood_gpio    : %d\n", gpio_get_value(chip->powergood_gpio));
    ret += sprintf(buf + ret, "wpc_int_gpio      : %d\n", gpio_get_value(chip->irq_gpio));
    ret += sprintf(buf + ret, "wireless_en       : %d\n", gpio_get_value(chip->chip_en_gpio));
    ret += sprintf(buf + ret, "en1_gpio          : %d\n", gpio_get_value(chip->charger_en_gpio));
    ret += sprintf(buf + ret, "\n");
    ret += sprintf(buf + ret, "reverse_charger   : %d\n", reserse_charge_online);
    ret += sprintf(buf + ret, "wireless_online   : %d\n", wls_work_online);
    ret += sprintf(buf + ret, "usb_otg_online    : %d\n", usb_otg_online);
    ret += sprintf(buf + ret, "usbchip_otg_detect     : %d\n", usbchip_otg_detect);
    
    return ret;
}

ssize_t mt5728_set_flag(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
{
    unsigned int value = 0;
    
    switch (value) {
        case 1:
            gpio_set_value(mte->ldo_ctrl_gpio, 1);
            break;
        case 2:
            gpio_set_value(mte->ldo_ctrl_gpio, 0);
            break;
        case 3:
            gpio_set_value(mte->chip_en_gpio, 1);
            break;
        case 4:
            gpio_set_value(mte->chip_en_gpio, 0);
            break;
        case 5:
            //gpio_set_value(mte->otg_ctrl_gpio, 1);
            break;
        case 6:
            //gpio_set_value(mte->otg_ctrl_gpio, 0);
            break;
        case 20:
            gpio_direction_input(chip->chip_en_gpio);
            break;
        case 21:
            gpio_direction_output(chip->chip_en_gpio, 0);
            break;
        case 22:
            //gpio_direction_input(chip->otg_ctrl_gpio);
            break;
        case 23:
            //gpio_direction_output(chip->otg_ctrl_gpio, 0);
            break;
        case 10:
            mt5728_reverse_charge(0);
            break;
        case 11:
            mt5728_reverse_charge(1);
            break;
    }
    return len;
}

//samsung tx send vout to 9v
void fast_vfc(int vol) {
    vuc val;
    val.value = vol;
    mt5728_write_buffer(mte, REG_VFC, val.ptr, 2);
    val.value = FAST_CHARGE;
    mt5728_write_buffer(mte, REG_CMD, val.ptr, 2);
    printk(KERN_ALERT "%s,write reg_cmd : 0x%04x,\n", __func__, val.value);
}

#if 1//def CONFIG_FG_SD77561
void mt5728_send_EndPowerPacket(u8 endreson) {
    PktType eptpkt;
    eptpkt.header  = ENDPOWERXFERPACKET;
    eptpkt.msg[0]  = endreson;
    mt5728_send_ppp(&eptpkt);
}
#endif
//wireless charge ,vout ,iout ,PMIC set,100ms
extern void wireless_chg_type_switch(bool otg_on);
#ifdef CONFIG_FG_SD77561
extern int sd77561_get_soc(void);
#endif
static void mt5728_charger_work_func(struct work_struct* work) {
    int vbus_now = 0;
    int vbus_read_temp = 0;
    vuc val;
    int vbat_temp;
    int ui_soc;
#if 1//def CONFIG_FG_SD77561
    bool chg_done = false;
#endif
    #ifndef MT5728_CHIP_AUTO_AFC9V
    vuc vout;
    #endif


#if UP_MT5728_WIRELESS_ADD_OTG
     if((mt5728_is_otg_plugin(otg_plugin) == 1) && (mt5728_charging_otgin_flag == 0)) {
         printk(KERN_ALERT "mt5728 ,when wireless charging,otg in 01 ");
	 mt5728_force_chipen_disable();
	 msleep(100);
         //wait Vout lower than 5V
         printk(KERN_ALERT "mt5728 ,when wireless charging,otg in 02 ");
         // while(Mt5728_get_vout() > 4500) {
         //    msleep(50);
         //}
         mt5728_removed_form_tx();
         mt5728_charging_otgin_flag = 1;
         //mt5728_ap_open_otg_boost(true);    //pmic boost otg power on
	
	 wireless_chg_type_switch(true);//true

         schedule_delayed_work(&chip->charger_work, msecs_to_jiffies(100));
	 printk(KERN_ALERT "mt5728 ,when wireless charging,otg in 04,,,wls_work_online:%d",wls_work_online);
         return;
     }

     if(mt5728_charging_otgin_flag == 1) {
         if(mt5728_is_otg_plugin(otg_plugin) == 0) {
	     printk(KERN_ALERT "mt5728 ,when wireless charging,otg in 05,,,wls_work_online:%d,otg_plugin:%d",wls_work_online,otg_plugin);
             mt5728_charging_otgin_flag = 0;
             mt5728_ap_open_otg_boost(false);    //pmic boost otg power off
             msleep(100);               //wait Vbus to 0V
             SET_GPIO_WIRELESS_EN_L();
	     wireless_chg_type_switch(false);//wwx
	     printk(KERN_ALERT "mt5728 ,when wireless charging,otg in 05,,,wls_work_online:%d",wls_work_online);
             return;
         } else {
             schedule_delayed_work(&chip->charger_work, msecs_to_jiffies(100));
             return;

         }
     }

#endif

    	//check if usb line charge plug in.
//        printk(KERN_ALERT "mt5728 get gpio wireless en !GET_GPIO_WIRELESS_EN:%d\n",GET_GPIO_WIRELESS_EN());
    if (GET_GPIO_WIRELESS_EN() == 1) {
        printk(KERN_ALERT "mt5728 get gpio wireless en !\n");
        //close mt5825 ldo
        SET_GPIO_LDO_CTRL_H();
        mt5728_removed_form_tx();
        #ifdef MT5728_USE_WAKELOCK
        // wake_unlock(&mt5728_wls_wake_lock);
        wireless_charge_wake_unlock();
        #endif
        return;
    }
    Mt5728_get_vrect();
    vbus_read_temp = Mt5728_get_vout();
    if ((vbus_read_temp >= 0) && (vbus_read_temp <= 20000)) {
        vbus_now = Mt5728_get_vout();
    }
    //check if rx put on tx surface,if 1 rx removed from tx
    //if(GET_GPIO_POWERGOOD() == 1) {
    if(((vbus_now < 1000) && (mt5728_ldo_on_flag == 1))|| (vbus_read_temp == -1)) {
        powergood_err_cnt++;
        if(powergood_err_cnt > 20) {                    //Cancel wireless charging icon display
            mt5728_removed_form_tx();
            #ifdef MT5728_USE_WAKELOCK
            // wake_unlock(&mt5728_wls_wake_lock);
            wireless_charge_wake_unlock();
            #endif
            return;
        } else {
            schedule_delayed_work(&chip->charger_work, msecs_to_jiffies(100));
            return;
        }
    } else {
        powergood_err_cnt = 0;
    }

#ifndef MT5728_CHIP_AUTO_AFC9V
    //rx , afc set up mt5728 vout to 9v
    if(vout_change_timecnt++ > 10) {
        vout_change_timecnt = 0;
        if(AfcSendTimeout > 0) {
            AfcSendTimeout--;
        } else {
            mt5728_read_buffer(mte,REG_VOUT,vout.ptr,2);
            //if tx support afc,set vout to 9v
            if((AfcIntFlag == 1) && (vout.value < 6000)) {
                vout.value = 9000;
                mt5728_write_buffer(mte, REG_VFC, vout.ptr, 2);
                vout.value = FAST_CHARGE;
                mt5728_write_buffer(mte, REG_CMD, vout.ptr, 2);
                printk(KERN_ALERT " mt5728_write_buffer(mte, REG_VFC, vout.ptr, 2)\n");                
            }
        }
    }
#endif



#ifdef REDUCE_CURRENT_ACCORD_VOUT
    if(rx_vout_max > (vbus_now + 500)) {
        current_reduce_flag |= VOUT_LOW;
        if(input_current_limit > 100) {
            input_current_limit -= 100;
        }
    } else {
        current_reduce_flag &= ~VOUT_LOW;
    }
#endif
	vbat_temp = battery_get_bat_temperature();
#ifdef CONFIG_FG_SD77561
	ui_soc = sd77561_get_soc();
#else
	ui_soc = battery_get_uisoc();
#endif
	if(vbus_now > 11000) {
		        
		printk("%d - vbat_temp = %d\n",
		                __LINE__, vbat_temp);
                 if(vbat_temp >= 46){
                    if(vbat_temp > 56){
                            chrg_info.charger_current = 0;
                    }else if(vbat_temp >= 49){
                            chrg_info.charger_current = 1000;
                            input_current_limit = 500;
                            rx_iout_max = 500;
                    }else{
                            chrg_info.charger_current = 2000;
                            input_current_limit = 1000;
                            rx_iout_max = 1000;
                    }

                    vbat_temp_hot = true;
                }else if(vbat_temp > 42 && vbat_temp_hot == true){
                    chrg_info.charger_current = 2000;
                    input_current_limit = 1000;
                    rx_iout_max = 1000;
                }else{
                    chrg_info.charger_current = 2900;
                    input_current_limit = 1100;
                    rx_iout_max = 1100;
                    vbat_temp_hot = false;
                }
	} else if(vbus_now > 8000) {
		if(vbat_temp >= 49 && vbat_temp <= 56){
			chrg_info.charger_current = 1000;
			rx_iout_max = 500;
		}else if(vbat_temp >= 46 && vbat_temp<49){
			chrg_info.charger_current = 2000;
			rx_iout_max = 1000;	
		}else if(vbat_temp > 56){
			chrg_info.charger_current = 0;
		}else{
			chrg_info.charger_current = 2900;
			rx_iout_max = 1100;
		}
	} else {
		rx_iout_max = 1000;
		chrg_info.charger_current = 1000;
	}
	
	//After waiting for 10 seconds, change the PMIC input current
	if(setup_iout_start_cnt < 100) {
		setup_iout_start_cnt++;
		if (setup_iout_start_cnt == 70) {
			if (mt5728_epp_ctrl_vout_flag) {
					val.value = rx_vout_max;
					printk(KERN_ALERT "mt5728 epp set Vout to:%d,ptpower%d\n",rx_vout_max,mt5728_epp_ptpower);
					mt5728_write_buffer(mte, REG_VOUTSET, val.ptr, 2);
					val.value = VOUT_CHANGE;
					mt5728_write_buffer(mte, REG_CMD, val.ptr, 2);
					mt5728_epp_ctrl_vout_flag = 0;
            }
        }
        
    } else {
        //Change one step every 500ms
        if(current_change_interval_cnt++ > 15) {
            current_change_interval_cnt = 0;
            if((input_current_limit < rx_iout_max) && (current_reduce_flag == 0)) {
               input_current_limit += 100;
            }
            mt5728_set_pmic_input_current_ichg(input_current_limit);
#if 1//def CONFIG_FG_SD77561
	    charger_dev_is_charging_done(primary_charger, &chg_done);
	    printk(KERN_ALERT "mt5728 chg_done=%d,ui_soc=%d\n",chg_done,ui_soc);
	    if (chg_done) {
	    	printk(KERN_ALERT "mt5728 charging full!\n");
	    	mt5728_send_EndPowerPacket(EPT_CHGCOMPLETE);
	    }

	    if(ui_soc >= 99){
		val.value = 5000;
		printk(KERN_ALERT "mt5728 epp set Vout to 5v.\n");
		mt5728_write_buffer(mte, REG_VOUTSET, val.ptr, 2);
		val.value = VOUT_CHANGE;
		mt5728_write_buffer(mte, REG_CMD, val.ptr, 2);
		mt5728_epp_ctrl_vout_flag = 0;
	    }
#endif
        }
    }
    schedule_delayed_work(&chip->charger_work, msecs_to_jiffies(100));
}

//mt5728_tx_cep_cnt_old
static ssize_t mt5728_get_tx_cep_cnt_close_tx(void)
{
    vuc cepcnt;
    mt5728_read_buffer(mte, 0x00c0, cepcnt.ptr, 2);
    if(cepcnt.value != mt5728_tx_cep_cnt_old) {
        mt5728_tx_cep_cnt_timeout = 0;
        mt5728_tx_cep_cnt_old = cepcnt.value;
    } else {
        mt5728_tx_cep_cnt_timeout++;
    }
    if(mt5728_tx_cep_cnt_timeout > (REVERSE_TIMEOUT_CNT)) {
        return 1;
    }
    return 0;
}


//reverse charge delay work
static void mt5728_reverse_work_func(struct work_struct* work) {
    if(chip->rxdetect_flag == 1) {
        if(chip->rxremove_flag == 1) {
            chip->rxdetect_flag = 0;
            chip->rxremove_flag = 0;
            reverse_timeout_cnt = 0;
            schedule_delayed_work(&chip->reverse_charge_work, msecs_to_jiffies(100));
            return;
        } else {
            //Check the remaining battery capacity
            //
            //Check battery temperature
        }
    } else {
        reverse_timeout_cnt++;
        if(reverse_timeout_cnt > REVERSE_TIMEOUT_CNT) { //RX put on timeout, active shutdown
            mt5728_reverse_charge(0);
            return;
        }
    }
    if (mt5728_tx_ping_cnt > (REVERSE_TIMEOUT_CNT*100 / 230)) {
    	printk(" %s: chip->rxdetect_flag=%d,mt5728_tx_ping_cnt=%d \n",__func__, chip->rxdetect_flag, mt5728_tx_ping_cnt);
        mt5728_reverse_charge(0);
        return;
    }
    if(mt5728_get_tx_cep_cnt_close_tx()) {
    	printk(" %s: chip->rxdetect_flag=%d,mt5728_tx_cep_cnt_timeout=%d \n",__func__, chip->rxdetect_flag, mt5728_tx_cep_cnt_timeout);
        mt5728_reverse_charge(0);
        return;        
    }
    schedule_delayed_work(&chip->reverse_charge_work, msecs_to_jiffies(100));
}

#if 0
static void mt5728_fwcheck_work_func(struct work_struct* work) {
   int mt5728VoutTemp;
   u8 fwver[2];
   
	if(wls_work_online ==1 )
	{
    mt5728_read_buffer(mte, REG_FW_VER, fwver, 2);	
    printk(KERN_ALERT "MT5728 fw_version_in_chip : 0x%x%x\n",fwver[0], fwver[1]);
	return;
	}
    mt5728VoutTemp = Mt5728_get_vout();
    if (mt5728VoutTemp < 0) {
		reserse_charge_online = 1;
        mt5728_ap_open_otg_boost(true);
   }
    msleep(200);        //Wait for Vout voltage to stabilize
    mt5728VoutTemp = Mt5728_get_vout();
    mt5728_read_buffer(mte, REG_FW_VER, fwver, 2);
		fwver[0] ^= fwver[1];
		fwver[1] ^= fwver[0];
		fwver[0] ^= fwver[1];
    printk(KERN_ALERT "MT5728 fw_version_in_chip : 0x%x%x\n",fwver[1], fwver[0]);
   if(((fwver[1] << 8)|fwver[0]) != MT5728_FWVERSION) 
		{
      mt5728_mtp_write_flag = 1;
      printk(KERN_ALERT "MT5728 fw_version not match : 0x%x\n",MT5728_FWVERSION);
        printk(KERN_ALERT "[%s]  brush MTP program\n",__func__);
        if(mt5728_mtp_write(0x0000,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin))){
            printk(KERN_ALERT "[%s] Write complete, start verification \n",__func__);
            if (mt5728_mtp_verify(0x0000,(u8 *)MT5728_mtp_bin,sizeof(MT5728_mtp_bin))) {
                printk(KERN_ALERT "[%s] mt5728_mtp_verify OK \n",__func__);
                    mt5728_read_buffer(mte, REG_FW_VER, fwver, 2);
    printk(KERN_ALERT "MT5728 fw_version_in_chip222 : 0x%x%x\n",fwver[1], fwver[0]);
            } else {
                printk(KERN_ALERT "[%s] mt5728_mtp_verify check program failed \n",__func__);
                    mt5728_read_buffer(mte, REG_FW_VER, fwver, 2);
    printk(KERN_ALERT "MT5728 fw_version_in_chip333 : 0x%x%x\n",fwver[1], fwver[0]);
            }
        }
        //if (mt5728VoutTemp < 0) {
          mt5728_ap_open_otg_boost(false);
       // }
        mt5728_mtp_write_flag = 0;
    }
}

#endif
/**
 * [mt5728_send_EPT End Power Transfer Packet]
 * @param endreson [end powr Reson]
 */
void mt5728_send_EPT(u8 endreson) {
    PktType eptpkt;
    eptpkt.header  = PP18;
    eptpkt.cmd     = ENDPOWERXFERPACKET;
    eptpkt.data[0] = endreson;
    mt5728_send_ppp(&eptpkt);
}

void mt5728_SetFodPara(void) {
    mt5728_write_buffer(mte, REG_FOD, (unsigned char *)mt5728fod, 16);
    mt5728_write_buffer(mte, REG_FOD_EPP, (unsigned char *)mt5728fod, 16);
}

void mt5728_irq_handle(void) {
    vuc val;
    vuc temp, fclr, scmd, ptpower;
    #ifdef MT5728_AFC9V_IN_IRQ
    vuc voutset;
    #endif
    int iic_rf = 0;
    scmd.value = 0;
	fclr.value = 0;
    printk(KERN_ALERT "----------------MT5728_delayed_work-----------------------\n");
    if (mt5728_mtp_write_flag == 1) {
        return;
    }
    temp.value = MT5728ID;
    iic_rf = mt5728_write_buffer(mte,REG_CHIPID,temp.ptr,2);
    if(iic_rf < 0){
        printk(KERN_ALERT "[%s] Chip may not be working\n",__func__);
        //20210330
        if(gpio_get_value(chip->irq_gpio)){
            printk(KERN_ALERT "[%s] irq_gpio high return irq handle\n",__func__);
            return;
        }
    }
    mt5728_read_buffer(mte,0x04,temp.ptr,2);
    //20210330
    if((temp.ptr[1] & RXMODE) && (chip->reverse_charger == 0) && (iic_rf >= 0) && (!is_wireless_chipen_pin_high())){
        printk(KERN_ALERT "[%s] The chip works in Rx mode\n",__func__);
        mt5728_read_buffer(mte, REG_INTFLAG, val.ptr, 2);
        fclr.value = val.value;
        printk(KERN_ALERT "[%s] REG_INTFLAG value:0x%04x\n", __func__, val.value);
        if(val.value == 0){
            printk(KERN_ALERT "[%s] There's no interruption here\n", __func__);
            cancel_delayed_work(&mte->eint_work);
            return;
        }
        if (val.value & INT_POWER_ON) {
       		printk(KERN_ALERT "[%s] val.value & INT_POWER_ON\n",__func__);
            AfcSendTimeout = 0;
            rx_vout_max = 5000;
            rx_iout_max = 1000;
            rx_iout_limit = 100;
            AfcIntFlag = 0;
            mt5728_ldo_on_flag = 0;
            mt5728_SetFodPara();
            if(mt5728_is_otg_plugin(otg_plugin) == 1) {
                SET_GPIO_LDO_CTRL_H();           //OTG is working. Put it on TX and LDO is close
            } else {
                #ifdef MT5728_USE_WAKELOCK
                // wake_lock(&mt5728_wls_wake_lock);
                wireless_charge_wake_lock();
                #endif
                SET_GPIO_LDO_CTRL_H();
            }
            powergood_err_cnt = 0;
            setup_iout_start_cnt = 0;
            mt5728_vout_old = 0;
            mt5728_vrect_old = 0;
            chrg_info.wls_online = 1;
            wls_work_online = 1;
            input_current_limit = 100;
	    cur_last = 0;
            mt5728_display_slow_wls_icon(1);
            mt5728_set_pmic_input_current_ichg(100);
	    enable_vbus_ovp(false);
            schedule_delayed_work(&chip->charger_work, msecs_to_jiffies(100));
            printk(KERN_ALERT "[%s] Interrupt signal: PowerON\n", __func__);
            // printk(KERN_ALERT, "[%s] PWR_SRC_CHG.\n", __func__);
            // pwr_src_chg = 1;
            // power_supply_changed(chip->wl_psy);
        }
        if (val.value & INT_LDO_ON) {
            mt5728_ldo_on_flag = 1;
            printk(KERN_ALERT "[%s] Interrupt signal:LDO ON\n", __func__);
        }
        if (val.value & INT_RX_READY) {
            if(mt5728_is_otg_plugin(otg_plugin) == 1) {
                SET_GPIO_LDO_CTRL_H();            //OTG is working. Put it on TX and LDO is close
            } else {
                powergood_err_cnt = 0;
                setup_iout_start_cnt = 0;
                wls_work_online = 1;
            }
            printk(KERN_ALERT "[%s] Interrupt signal:MT5728 is Ready\n", __func__);
        }
        if (val.value & INT_LDO_OFF) {
            printk(KERN_ALERT "[%s] Interrupt signal:MT5728 LDO_OFF\n", __func__);
        }
        if (val.value & INT_AFC_SUPPORT) {
            printk(KERN_ALERT "[%s] Interrupt signal:Tx support samsung_afc\n", __func__);
        #ifdef MT5728_AFC9V_IN_IRQ
            voutset.value = 9000;
            mt5728_write_buffer(mte, REG_VFC, voutset.ptr, 2);
            scmd.value |= FAST_CHARGE;
        #endif
            AfcIntFlag = 1;
            AfcSendTimeout = 6;
            rx_vout_max = 9000;
            rx_iout_max = 1200;
            mt5728_display_qucik_wls_icon(1);
        }
        if (val.value & INT_FSK_RECV) {
            printk(KERN_ALERT "[%s] Interrupt signal:FSK received successfully\n", __func__);
            mte->fsk_status = FSK_SUCCESS;
            //read REG_BC
        }
        if (val.value & INT_FSK_SUCCESS) {
            printk(KERN_ALERT "[%s] Interrupt signal:FSK received successfully\n", __func__);
            mte->fsk_status = FSK_SUCCESS;
            //read REG_BC
        }
        if (val.value & INT_FSK_TIMEOUT) {
            printk(KERN_ALERT "[%s] Interrupt signal:Failed to receive FSK\n", __func__);
            mte->fsk_status = FSK_FAILED;
            //read REG_BC
        }
        if (val.value & INT_EPP) {
            mt5728_read_buffer(mte,REG_POT_POWER,ptpower.ptr,2);
            printk(KERN_ALERT "[%s] REG_POT_POWER:%d\n", __func__ ,ptpower.value);
        #ifdef MT5728_EPP_AP_CTRL_VOUT
            mt5728_epp_ctrl_vout_flag = 1;
            mt5728_epp_ptpower = ptpower.value;
            if(ptpower.value < MAX_POWER(15)) {
                rx_vout_max = 9000;
                rx_iout_max = 1200;
            } else {
                rx_vout_max = 12000;
                rx_iout_max = 1000;
            }
            // voutset.value = rx_vout_max;
            // mt5728_write_buffer(mte, REG_VOUTSET, voutset.ptr, 2);
            // scmd.value |= VOUT_CHANGE;
        #endif
#ifdef  __GFTK_WIRELESS_REG_MAX_POWER_VALUE__
 	    mt5728_read_buffer(mte,REG_MAX_POWER,ptpower.ptr,2);
            printk(KERN_ALERT "[%s] OLD REG_MAX_POWER:%d\n", __func__ ,ptpower.value);
	    ptpower.value = __GFTK_WIRELESS_REG_MAX_POWER_VALUE__;
	    mt5728_write_buffer(mte,REG_MAX_POWER,ptpower.ptr,2);
            mt5728_read_buffer(mte,REG_MAX_POWER,ptpower.ptr,2);
            printk(KERN_ALERT "[%s] NEW REG_MAX_POWER:%d\n", __func__ ,ptpower.value);
#endif
            mt5728_display_qucik_wls_icon(1);
            printk(KERN_ALERT "[%s] Interrupt signal:Tx support EPP\n", __func__);
        }
    }
    if((temp.ptr[1] & TXMODE)  && (iic_rf >= 0)){
        printk(KERN_ALERT "[%s] The chip works in Tx mode\n",__func__);
        mt5728_read_buffer(mte, REG_INTFLAG, val.ptr, 2);
        fclr.value = val.value;
        printk(KERN_ALERT "[%s] REG_INTFLAG value:0x%04x\n", __func__, val.value);
        if(val.value == 0){
            printk(KERN_ALERT "[%s] There's no interruption here\n", __func__);
            cancel_delayed_work(&mte->eint_work);
            return;
        }
        if (val.value & BIT(14)) {  //INT_TX_PING,interval = 230ms
            if (reserse_charge_online == 1) {
                mt5728_tx_ping_cnt++;
                printk(KERN_ALERT "MT5728 %s ,INT_TX_PING\n", __func__); 
            }
        }
        if (val.value & INT_POWER_TRANS) {
            if (reserse_charge_online == 1) {
                chip->rxdetect_flag = 1;   
		reverse_timeout_cnt = 0;
		mt5728_tx_ping_cnt = 0;  
                printk(KERN_ALERT "MT5728 %s ,rxdetect: INT_POWER_TRANS\n", __func__); 
            }
        }
        if (val.value & INT_REMOVE_POWER) {
            if (reserse_charge_online == 1) {
                chip->rxdetect_flag = 0;
                printk(KERN_ALERT "MT5728 %s ,rxdetect: INT_REMOVE_POWER\n", __func__);
            }       
        }        
    }
    if(iic_rf >= 0) {
    scmd.value |= CLEAR_INT;
    //---clrintflag
    //mt5728_write_buffer(mte, REG_INTCLR, fclr.ptr, 2);
    mt5728_write_buffer(mte, REG_INTCLR, fclr.ptr, 2);
    printk(KERN_ALERT "[%s] write REG_INTCLR : 0x%04x,\n", __func__, fclr.value);

    mt5728_write_buffer(mte, REG_CMD, scmd.ptr, 2);
    printk(KERN_ALERT "[%s] write REG_CMD : 0x%04x,\n", __func__, scmd.value);
    }
    if(fclr.value){
    	schedule_delayed_work(&mte->eint_work,100);  //Callback check if the interrupt is cleared
    	printk(KERN_ALERT "MT5728 %s ,schedule_delayed_work\n", __func__);
    }
}

EXPORT_SYMBOL(mt5728_irq_handle);

static void mt5728_int_delayed_work_func(struct work_struct* work) {

    if(first_boot){
	    if(boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT || boot_mode == LOW_POWER_OFF_CHARGING_BOOT){
		printk(KERN_ALERT "MT5728 kernel power off chargin\n");
	    }else{
		printk(KERN_ALERT "MT5728 reset chip while normal boot\n");
	    	mt5728_force_chipen_disable();
		msleep(100);
	    	mt5728_chipen_ctrl_by_hardware();
	    }
	    first_boot = false;
    }

     mt5728_irq_handle();
}

static irqreturn_t mt5728_irq(int irq,void * data){
    struct  mt5728_dev * mt5728 = data;
    printk(KERN_ALERT "mt5728_gpio_irq\n");
    schedule_delayed_work(&mt5728->eint_work,300);
    return IRQ_HANDLED;
}

#if UP_MT5728_WIRELESS_TRX_MODE_SWITCH
int up_trx_mode_flag = 0;
static char wireless_trx_mode_value[5] = {};
static ssize_t show_wireless_trx_mode_value(struct device* dev, struct device_attribute *attr, char *buf)
{
        printk("show wireless_trx_mode status is %s \n",wireless_trx_mode_value);
        return scnprintf(buf, 64, "%s\n", wireless_trx_mode_value);
}
static ssize_t store_wireless_trx_mode_value(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
        printk("show wireless_trx_mode status before : %d  (1->tx / 0->rx) !\n",up_trx_mode_flag);
        if(!strncmp(buf, "tx", 2))
        {
            sprintf(wireless_trx_mode_value,"tx");//tx mode
            gpio_set_value(chip->trx_mode_gpio, 1);
            //gpio_direction_output(chip->powergood_gpio, 0);
            gpio_set_value(chip->powergood_gpio, 1);
            //gpio_set_value(chip->powergood_gpio, 0);
            //gpio_set_value(chip->powergood_gpio, 1);
            up_trx_mode_flag = 1;//status --- on
        }
        else
        {
            sprintf(wireless_trx_mode_value,"rx");//rx mode
            gpio_set_value(chip->trx_mode_gpio, 0);
            //gpio_direction_output(chip->powergood_gpio, 0);
            gpio_set_value(chip->powergood_gpio, 0);
            //gpio_set_value(chip->powergood_gpio, 1);
            //gpio_set_value(chip->powergood_gpio, 0);
            up_trx_mode_flag = 0;//status --- off
        }
        printk("show wireless_trx_mode status after : %d  1->tx / 0->rx !\n",up_trx_mode_flag);   
        return count;
}
static DEVICE_ATTR(wireless_trx_mode,  0664, show_wireless_trx_mode_value, store_wireless_trx_mode_value);

static struct attribute *wireless_trx_mode_attrs[] = {
       &dev_attr_wireless_trx_mode.attr,
       NULL,
};

static struct attribute_group wireless_trx_mode_attr_grp = {
       .attrs = wireless_trx_mode_attrs,
};
#endif

extern int battery_get_boot_mode(void);
static int mt5728_probe(struct i2c_client* client, const struct i2c_device_id* id) {
    int rc = 0;
    vuc chipid;
    int irq_num = 0;
    u8 fwver[2];
    int ret=0;
    //int i;
    // struct device_node *mt5728gpio_node;
    boot_mode = battery_get_boot_mode();
    printk(KERN_ALERT "MT5728 probe boot_mode=%d.\n",boot_mode);
    printk(KERN_ALERT DRIVER_FIRMWARE_VERSION);
    chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
    if (!chip)
        return -ENOMEM;
    primary_charger = get_charger_by_name("primary_chg");
    if (!primary_charger) {
	pr_notice("%s: get primary charger device failed\n", __func__);
	//return -1;
    }
    mt5728_gpio_init();
    printk(KERN_ALERT "mt5728_gpio_init done!\n");
   // GPIO_OTG_CTRL_INPUT(); 
   // mt5728_otgen_gpio_ctrl(false);
    GPIO_WIRELESS_EN_INPUT();   //chip en,hardware contrl //gpio26
    GPIO_WPC_RTMODE_OUTPUT();	//trx gpio output //gpio46
    SET_GPIO_WPC_RTMODE_L();    //default rx mode //low
    //GPIO_LDO_CTRL_OUTPUT();
    //SET_GPIO_LDO_CTRL_L();     //ldo on
    mutex_init(&chip->slock);
    #ifdef MT5728_USE_WAKELOCK
    //wake_lock_init(&mt5728_wls_wake_lock, WAKE_LOCK_SUSPEND, "mt5728_wls_wake_lock");
    //mt5728_wls_wake_lock = wakeup_source_register("mt5728_wls_wake_lock");
    wakeup_source_init(&mt5728_wls_wake_lock, "mt5728_wls_wake_lock");
    #endif
    printk(KERN_ALERT "MT5728 chip.\n");
    chip->regmap = regmap_init_i2c(client, &mt5728_regmap_config);
    if (!chip->regmap) {
        printk(KERN_ALERT "MT5728 parent regmap is missing\n");
        return -EINVAL;
    }
    printk(KERN_ALERT "MT5728 regmap.\n");

    chip->client           = client;
    chip->dev              = &client->dev;
    chip->bus.read         = mt5728_read;
    chip->bus.write        = mt5728_write;
    chip->bus.read_buf     = mt5728_read_buffer;
    chip->bus.write_buf    = mt5728_write_buffer;

    chip->wl_psd.name = "mt5728_wireless";
    chip->wl_psd.type = POWER_SUPPLY_TYPE_WIRELESS;
    chip->wl_psd.properties = ech_wls_chrg_props;
    chip->wl_psd.num_properties = ARRAY_SIZE(ech_wls_chrg_props);
    chip->wl_psd.get_property = ech_wls_chrg_get_property;
    chip->wl_psd.set_property = ech_wls_chrg_set_property;
    chip->wl_psd.external_power_changed = ech_wls_charger_external_power_changed;
    chip->wl_psd.property_is_writeable= ech_wls_chrg_property_is_writeable;

    chip->wl_psc.drv_data = chip;
    chip->wl_psc.of_node = chip->dev->of_node;
    chip->wl_psc.supplied_to = ech_supplicants;
    chip->wl_psc.num_supplicants = ARRAY_SIZE(ech_supplicants);
    chip->wl_psy = power_supply_register(chip->dev, &chip->wl_psd, NULL);
    if (IS_ERR(chip->wl_psy)) {
	 printk(KERN_ALERT "[%s] power_supply_register wireless failed, ret = %d.\n", __func__, ret);
    }

    chip->wl_psy->supplied_from = ech_supplied_from;
    chip->wl_psy->num_supplies = ARRAY_SIZE(ech_supplied_from);
    printk(KERN_ALERT "wireless charger power supply register successful.\n");
    
    ech_wls_chrg_info_init();
  //ech_wls_create_device_node(&(client->dev));

    INIT_DELAYED_WORK(&chip->eint_work,mt5728_int_delayed_work_func);
    INIT_DELAYED_WORK(&chip->charger_work, mt5728_charger_work_func);
    INIT_DELAYED_WORK(&chip->reverse_charge_work, mt5728_reverse_work_func);
  //INIT_DELAYED_WORK(&chip->mt5728_fwcheck_work, mt5728_fwcheck_work_func);
    

    irq_num = gpio_to_irq(chip->irq_gpio);

    rc = devm_request_threaded_irq(&client->dev,irq_num,  \
                                   NULL,mt5728_irq,IRQF_TRIGGER_FALLING|IRQF_ONESHOT,   \
                                   "mt5728",chip);
    if(rc != 0){
        printk(KERN_ALERT "%s:failed to request IRQ %d: %d\n",__func__,gpio_to_irq(chip->irq_gpio),rc);
        goto Err;
    }
    #ifdef MT5728_USE_WAKELOCK
    device_init_wakeup(chip->dev, true);
    #endif
    rc = sysfs_create_group(&client->dev.kobj, &mt5728_sysfs_group);
    
    printk(KERN_ALERT "MT5728 probed successfully\n");
    mte = chip;





#if UP_MT5728_WIRELESS_TRX_MODE_SWITCH
   if (sysfs_create_group(&client->dev.kobj, &wireless_trx_mode_attr_grp)) {
	pr_err("%s : sysfs_create_group wireless_trx_mode failed\n", __FILE__);
	goto Err;
   }
#endif

#if 0//test

	for(ret=0;ret++;ret<10)
	{
    	   mt5728_read_buffer(mte, REG_CHIPID, chipid.ptr, 2);
	}
#endif
    mt5728_read_buffer(mte, REG_CHIPID, chipid.ptr, 2);
    if ((chipid.ptr[1] << 8 | chipid.ptr[0]) == MT5728ID) {
        printk(KERN_ALERT "MT5728 ID 0x%x Correct query !\n",chipid.value);
    } else {
        printk(KERN_ALERT "MT5728 ID error : 0x%x \n ", chipid.value);
    }
    
  //  schedule_delayed_work(&chip->mt5728_fwcheck_work, msecs_to_jiffies(100));

    mt5728_read_buffer(mte, REG_FW_VER, fwver, 2);
    printk(KERN_ALERT "MT5728 fw_version : 0x%x%x\n",fwver[1], fwver[0]);
    schedule_delayed_work(&mte->eint_work,msecs_to_jiffies(5000));
Err:
    return rc;
}

static int mt5728_remove(struct i2c_client* client) {
    sysfs_remove_group(&client->dev.kobj, &mt5728_sysfs_group);

#if UP_MT5728_WIRELESS_TRX_MODE_SWITCH
    sysfs_remove_group(&client->dev.kobj, &wireless_trx_mode_attr_grp);
#endif

    return 0;
}

static void mt5728_shutdown(struct i2c_client *client)
{
    	mt5728_force_chipen_disable();
	msleep(100);
    	mt5728_chipen_ctrl_by_hardware();
	
	printk(KERN_ALERT "%S \n ", __func__);


}

static const struct i2c_device_id mt5728_dev_id[] = {
    {"mt5728", 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, mt5728_dev_id);

#ifdef CONFIG_OF
static const struct of_device_id mt5728_of_match[] = {
    {.compatible = "mediatek,mt5728"},
    {},
};
MODULE_DEVICE_TABLE(of, mt5728_of_match);
#endif

static struct i2c_driver mt5728_driver = {
    .driver = {
        .name           = "mt5728",
        .owner          = THIS_MODULE,
        .of_match_table = of_match_ptr(mt5728_of_match),
    },
    .probe    = mt5728_probe,
    .remove   = mt5728_remove,
    .id_table = mt5728_dev_id,
    .shutdown = mt5728_shutdown,
};


static int __init mt5728_driver_init(void) {
    #ifdef CONFIG_OF
    printk(KERN_ALERT "mt5728_driver_init start\n ");
    #endif
    return i2c_add_driver(&mt5728_driver);
}

late_initcall(mt5728_driver_init);

static void __exit mt5728_driver_exit(void) {
    printk(KERN_ALERT "mt5728_driver_exit\n");
    return i2c_del_driver(&mt5728_driver);
}

module_exit(mt5728_driver_exit);

MODULE_AUTHOR("Yangwl@maxictech.com");
MODULE_DESCRIPTION("MT5728 Wireless Power Receiver");
MODULE_LICENSE("GPL");
