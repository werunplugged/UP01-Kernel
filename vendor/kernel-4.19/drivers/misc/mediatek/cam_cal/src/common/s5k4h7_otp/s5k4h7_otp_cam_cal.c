#include "s5k4h7_otp_cam_cal.h"
//read from imgsensor.c

extern u8 s5k4h7_step_data[1500];
extern u8 s5k4h7_eeprom_data[2048];
unsigned int  s5k4h7_selective_read_region(struct i2c_client *client, unsigned int addr,
	unsigned char *data, unsigned int size)
{
	if(addr == 2048)
	{
		memcpy((void *)data,(void *)&s5k4h7_step_data[0],size);
	}
	else
	{
    	memcpy((void *)data,(void *)&s5k4h7_eeprom_data[addr],size);
    }
	printk("s5k4h7_selective_read_region addr:%d,size %d data read = 0x%x\n",addr,size, *data);
    return size;
}


