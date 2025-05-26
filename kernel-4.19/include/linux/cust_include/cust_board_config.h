#ifndef __CUST_BOARD_CONFIG_H__
#define __CUST_BOARD_CONFIG_H__


#undef CUST_YES
#define CUST_YES 1
#undef CUST_NO
#define CUST_NO 0

/*audio releated */
/* phone mic mode */
#define __CUST_PHONE_MIC_MODE__   2
/* accdet mic mode */ //acc 1, dcc 2
#define __CUST_ACCDET_MIC_MODE__  2

#define __CUST_MSDC_CD_HIGH__ CUST_YES

#define __CUST_OTG_GPIO_SELECT__  CUST_YES

#define __GFTK_TCPC_ACCDECT_SUPPORT__

 /*phone use exp audio pa*/
#define __CUST_USING_EXTAMP__  CUST_NO

/**###########################audio gpio define##################***/

#if  __CUST_USING_EXTAMP__  
    #define __CUST_EXTAMP_MODE__    3
    #define __CUST_EXTAMP_GPIO_NUM__    136
#endif

/*############### sensor releated config #####################*/
#define __CUST_STK_ALS_GAIN__ 0x01
#define __CUST_STK_GAINCTRL__ 0x00

#define S5KHM2SP_OTP
#define S5KHM6SP_OTP
#define S5KGD2SM_OTP

#define __CUST_VIBR_VALUE_SUPPORT__ 9

/*############### TCPC_POLARITY_DETECT #####################*/
#define __CUST_TCPC_POLARITY_DETECT__

#define __CUST_WIRELESS_CHARGE_SUPPORT__ CUST_YES
#define __CUST_VIBRATOR_BOOT_UP__                CUST_YES
#define __CUST_NUCHARGER_SUPPORT__  CUST_YES
#define __CUST_REVERSE_TIMEOUT_CNT__ 6000


#define __CUST_STK3A5XX_PS_THRELD_HIGH__  340
#define __CUST_STK3A5XX_PS_THRELD_LOW__   118

#define	__CUST_SAR_PH_MASK__	0xa5

/**########################### thermal ##################***/
#define __CUST_M156_BOARD__
#define __CUST_MAX_CHARGE_TEMPERATURE__ 56
#define __CUST_MAX_CHARGE_TEMPERATURE_PLUS_X_DEGREE__ 52
#define __CUST_POWER_OFF_CHARGING_CLOSE_THERMAL__ CUST_YES
#define __CUST_IBUSOCP_RATIO__  125
#define __CUST_BATTERY_VOLTAGE__  4450000
#define __CUST_LK_MAX_CHARGER_VOLTAGE__        12500000
#define __CUST_LK_USB_CHARGER_CURRENT__        500000
#define __CUST_AC_CHARGER_CURRENT__            2050000
#define __CUST_AC_CHARGER_INPUT_CURRENT__      2050000
#define __CUST_NON_STD_AC_CHARGER_CURRENT__    1000000
#define __CUST_USB_CHARGER_CURRENT__           500000
#define __CUST_TA_AC_9V_INPUT_CURRENT__        1700000
#define __CUST_CHARGING_IEOC_CURRENT__         200000
#define __CUST_MIN_CHARGER_VOLTAGE__           4600000
#define __CUST_MAX_CHARGER_VOLTAGE__           12500000
#define __CUST_LK_FAST_CHARGER_VOLTAGE__			2600000
#define __CUST_NON_STD_AC_LK_CHARGER_CURRENT__  1000000
#define __CUST_AC_LK_CHARGER_INPUT_CURRENT__   2050000
#endif

