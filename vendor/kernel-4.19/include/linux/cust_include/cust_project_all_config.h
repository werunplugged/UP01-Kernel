
#ifndef __CUST_PROJECT_ALL_CONFIG_H__
#define __CUST_PROJECT_ALL_CONFIG_H__

#include "cust_common_config.h"
#include "cust_board_config.h"
#include "custom_config.h"



//when you add new macro for project , you need define default macro value here, so you need not define in code logic

#ifndef __CUST_URXDO_GPIO_SELECT__
#define __CUST_URXDO_GPIO_SELECT__  CUST_NO
#endif

#ifndef __CUST_TCPC_POLARITY_DETECT__
#define __CUST_TCPC_POLARITY_DETECT__ CUST_NO
#endif

#ifndef __CUST_OTG_GPIO_SELECT__
#define __CUST_OTG_GPIO_SELECT__ CUST_NO
#endif

#ifndef __CUST_DUAL_CAMERA_USEDBY_YUV_MODE__
#define __CUST_DUAL_CAMERA_USEDBY_YUV_MODE__  CUST_NO
#endif

#ifndef __CUST_DUAL_SUB_CAMERA__
#define __CUST_DUAL_SUB_CAMERA__  CUST_NO
#endif

#ifndef __CUST_MAIN_FAKE_I2C_USE_SUB__
#define __CUST_MAIN_FAKE_I2C_USE_SUB__  CUST_NO
#endif

#ifndef __CUST_SUB_FAKE_I2C_USE_MAIN__
#define __CUST_SUB_FAKE_I2C_USE_MAIN__  CUST_NO
#endif

#ifndef __CUST_CAM_OTP_PDAF_STATUS__
#define __CUST_CAM_OTP_PDAF_STATUS__  CUST_NO
#endif
#ifndef __CUST_MCAM_OTP_PDAF_STATUS__
#define __CUST_MCAM_OTP_PDAF_STATUS__  CUST_NO
#endif
#ifndef __CUST_SCAM_OTP_PDAF_STATUS__
#define __CUST_SCAM_OTP_PDAF_STATUS__  CUST_NO
#endif

#ifndef __CUST_TYPEC_ACCDET_MIC_SUPPORT__
#define __CUST_TYPEC_ACCDET_MIC_SUPPORT__  CUST_NO
#endif

#ifndef __CUST_DUAL_CAMERA_USEDBY_YUV_MODE__
#define __CUST_DUAL_CAMERA_USEDBY_YUV_MODE__		CUST_NO
#endif

///////////////////
/////  next pls do not modify
////////////////////

#include "cust_board_dts_config.h"

#ifndef __CUST_GPIO_LCM_POWER_DM_PINMUX__
#define __CUST_GPIO_LCM_POWER_DM_PINMUX__ PUNMUX_GPIO_NONE_FUNC_NONE
#endif

#ifndef __CUST_GPIO_LCM_POWER_DP_PINMUX__
#define __CUST_GPIO_LCM_POWER_DP_PINMUX__ PUNMUX_GPIO_NONE_FUNC_NONE
#endif

#ifndef __CUST_DUAL_CAMERA_USEDBY_YUV_MODE__
#define __CUST_DUAL_CAMERA_USEDBY_YUV_MODE__  CUST_NO
#endif

#ifndef __CUST_DUAL_SUB_CAMERA__
#define __CUST_DUAL_SUB_CAMERA__  CUST_NO
#endif

#ifndef __CUST_MAIN_FAKE_I2C_USE_SUB__
#define __CUST_MAIN_FAKE_I2C_USE_SUB__  CUST_NO
#endif

#ifndef __CUST_MAIN_FAKE_I2C_USE_SUB2__
#define __CUST_MAIN_FAKE_I2C_USE_SUB2__  CUST_NO
#endif

#ifndef __CUST_MAIN_FAKE_I2C_USE_MAIN2__
#define __CUST_MAIN_FAKE_I2C_USE_MAIN2__  CUST_NO
#endif

#ifndef __CUST_SUB_FAKE_I2C_USE_MAIN__
#define __CUST_SUB_FAKE_I2C_USE_MAIN__  CUST_NO
#endif

#ifndef __CUST_TP_GESTURE_SUPPORT__
#define __CUST_TP_GESTURE_SUPPORT__ CUST_NO
#endif


#endif
