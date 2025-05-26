#ifndef __CUST_CUSTOM_DTS_CONFIG_H__
#define __CUST_CUSTOM_DTS_CONFIG_H__

/** it's used for dts **/
#define STR_AND_STR(a,b)    	a##b

/** Example for dts: *******
#define __CUST_XXX_I2C_BUS_NUM__ 1
&I2C(__CUST_XXX_I2C_BUS_NUM__) {
--->output: &i2c1 {
******/
#define I2C(num)   		STR_AND_STR(i2c,num)


#define PUNMUX_GPIO_NONE_FUNC_NONE    0xFFFF


#define __CUST_LDO_WL2864C_LDO1__      1
#define __CUST_LDO_WL2864C_LDO2__      2
#define __CUST_LDO_WL2864C_LDO3__      3
#define __CUST_LDO_WL2864C_LDO4__      4
#define __CUST_LDO_WL2864C_LDO5__      5
#define __CUST_LDO_WL2864C_LDO6__      6
#define __CUST_LDO_WL2864C_LDO7__      7

#endif

