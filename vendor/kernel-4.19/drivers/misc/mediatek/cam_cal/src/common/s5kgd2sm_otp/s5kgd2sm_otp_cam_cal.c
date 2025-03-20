#include "s5kgd2sm_otp_cam_cal.h"
//read from imgsensor.c

//extern u8 s5kgd2sm_step_data[1500];
extern u8 s5kgd2sm_eeprom_data[2048];
unsigned int  s5kgd2sm_selective_read_region(struct i2c_client *client, unsigned int addr,
	unsigned char *data, unsigned int size)
{

    	memcpy((void *)data,(void *)&s5kgd2sm_eeprom_data[addr],size);

	printk("s5kgd2sm_selective_read_region addr:%d,size %d data read = 0x%x\n",addr,size, *data);
    return size;
}


