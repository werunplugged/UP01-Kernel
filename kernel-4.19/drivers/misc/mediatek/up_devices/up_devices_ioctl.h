#ifndef  __UP_DEVICES_IOCTL_H__
#define  __UP_DEVICES_IOCTL_H__

#include <linux/ioctl.h>

#define UP_IOC_MAGIC 0xCC

#define UPIOC_GET_EXTAMP_SOUND_MODE        _IOR(UP_IOC_MAGIC, 0x01, uint32_t)
#define UPIOC_SET_EXTAMP_SOUND_MODE        _IOW(UP_IOC_MAGIC, 0x02, uint32_t)
#define UPIOC_GET_ACCDET_MIC_MODE          _IOR(UP_IOC_MAGIC, 0x03, uint32_t)
#define UPIOC_SET_ACCDET_MIC_MODE          _IOW(UP_IOC_MAGIC, 0x04, uint32_t)
#define UPIOC_GET_ADC_VALUE		    _IOR(UP_IOC_MAGIC, 0x05, uint32_t)

#define UP_IOCQDIR            _IOR(UP_IOC_MAGIC, 0x06, uint32_t)
#define UP_IOCSDIRIN          _IOW(UP_IOC_MAGIC, 0x07, uint32_t)
#define UP_IOCSDIROUT         _IOW(UP_IOC_MAGIC, 0x08, uint32_t)
#define UP_IOCQPULLEN         _IOR(UP_IOC_MAGIC, 0x09, uint32_t)
#define UP_IOCSPULLENABLE     _IOW(UP_IOC_MAGIC, 0x0A, uint32_t)
#define UP_IOCSPULLDISABLE    _IOW(UP_IOC_MAGIC, 0x0B, uint32_t)
#define UP_IOCQPULL           _IOR(UP_IOC_MAGIC, 0x0C, uint32_t)
#define UP_IOCSPULLDOWN       _IOW(UP_IOC_MAGIC, 0x0D, uint32_t)
#define UP_IOCSPULLUP         _IOW(UP_IOC_MAGIC, 0x0E, uint32_t)
#define UP_IOCQINV            _IOR(UP_IOC_MAGIC, 0x0F, uint32_t)
#define UP_IOCSINVENABLE      _IOW(UP_IOC_MAGIC, 0x10, uint32_t)
#define UP_IOCSINVDISABLE     _IOW(UP_IOC_MAGIC, 0x11, uint32_t)
#define UP_IOCQDATAIN         _IOR(UP_IOC_MAGIC, 0x12, uint32_t)
#define UP_IOCQDATAOUT        _IOR(UP_IOC_MAGIC, 0x13, uint32_t)
#define UP_IOCSDATALOW        _IOW(UP_IOC_MAGIC, 0x14, uint32_t)
#define UP_IOCSDATAHIGH       _IOW(UP_IOC_MAGIC, 0x15, uint32_t)



#endif
