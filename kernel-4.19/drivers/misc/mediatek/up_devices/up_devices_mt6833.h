
#include <asm/io.h>
#include <linux/list.h>
#include <linux/seq_file.h>

#include "lcm_drv.h"
#include <drm/drm_mipi_dsi.h>
#include "kd_imgsensor_define.h"
#include "hf_manager.h"

#define ID_LCM_TYPE       		 11 //	 MAX_ANDROID_SENSOR_NUM    	// 11
#define ID_CAMERA_TYPE       	12 //	(MAX_ANDROID_SENSOR_NUM + 1)   	// 12
#define ID_TOUCH_TYPE         	13 //	(MAX_ANDROID_SENSOR_NUM + 2)   	// 13
#define ID_ACCSENSOR_TYPE       14 //	(MAX_ANDROID_SENSOR_NUM + 3)   	// 14
#define ID_MSENSOR_TYPE         15 //	(MAX_ANDROID_SENSOR_NUM + 4)   	// 15
#define ID_ALSPSSENSOR_TYPE     16 //	(MAX_ANDROID_SENSOR_NUM + 5)   	// 16
#define ID_SARSENSOR_TYPE     17 //	(MAX_ANDROID_SENSOR_NUM + 6)   	// 17
#define ID_BAROSENSOR_TYPE     18 //	(MAX_ANDROID_SENSOR_NUM + 6)   	// 18
typedef enum 
{ 
    DEVICE_SUPPORTED = 0,        
    DEVICE_USED = 1,
}compatible_type;

char touch_fw_version[30] = {0};


extern int up_set_camera_device_used(char * module_name, int pdata);
extern int up_camera_device_add(struct IMGSENSOR_SENSOR_LIST* mCamera, compatible_type isUsed);
#if 1
extern int up_set_accsensor_device_used(char * module_name, int pdata);
extern int up_accsensor_device_add(struct sensor_info* maccsensor, compatible_type isUsed);

extern int up_set_msensor_device_used(char * module_name, int pdata);
extern int up_msensor_device_add(struct sensor_info* mmsensor, compatible_type isUsed);

extern int up_set_alspssensor_device_used(char * module_name, int pdata);
extern int up_alspssensor_device_add(struct sensor_info* malspssensor, compatible_type isUsed);

extern int up_set_sarsensor_device_used(char * module_name, int pdata);
extern int up_sarsensor_device_add(struct sensor_info* sarsensor, compatible_type isUsed);
extern int up_set_barosensor_device_used(char * module_name, int pdata);
extern int up_barosensor_device_add(struct sensor_info* mbarosensor, compatible_type isUsed);
static char up_sar_name_buf[20];
#endif
