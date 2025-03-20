#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>    
#include <linux/cust_include/cust_project_all_config.h>

#define WL2864C_MAX_CONFIG_NUM 	16
#define WL2864C_IO_REG_LIMIT 	20
#define WL2864C_IO_BUFFER_LIMIT 128
#define WL2864C_MISC_MAJOR 		250

//#define WL2864C_DUMP

#define WL2864C_DRIVER_NAME     "wl2864c"
//reg
#define WL2864C_REG_LDO1_VOUT	0x03
#define WL2864C_REG_LDO_EN		0x0E
/*!
 * reg_value struct
 */
struct reg_value {
    u8 u8Add;
    u8 u8Val;
};

/*!
 * wl2864c_data_t struct
 */
struct wl2864c_data_t {
    struct i2c_client *i2c_client;
    struct regulator *vin1_regulator;
    u32 vin1_vol;
    struct regulator *vin2_regulator;
    u32 vin2_vol;
    int en_gpio;
    int vin2_gpio;
    u8 chip_id;
    u8 id_reg;
    u8 id_val;
    u8 init_num;
    struct reg_value inits[WL2864C_MAX_CONFIG_NUM];
    u32 offset;
    bool on;
};

extern int up_board_id_read(void);


/*!
 * wl2864c_data
 */
static struct wl2864c_data_t wl2864c_data;

/*!
 * wl2864c write reg function
 *
 * @param reg u8
 * @param val u8
 * @return  Error code indicating success or failure
 */
static s32 wl2864c_write_reg(u8 reg, u8 val)
{
    u8 au8Buf[2] = {0};
    au8Buf[0] = reg;
    au8Buf[1] = val;
    if (i2c_master_send(wl2864c_data.i2c_client, au8Buf, 2) < 0)
    {
        pr_err("%s:write reg error:reg=%x,val=%x\n", __func__, reg, val);
        return -1;
    }
    return 0;
}

/*!
 * wl2864c read reg function
 *
 * @param reg u8
 * @param val u8 *
 * @return  Error code indicating success or failure
 */
static int wl2864c_read_reg(u8 reg, u8 *val)
{
    u8 au8RegBuf[1] = {0};
    u8 u8RdVal = 0;
    au8RegBuf[0] = reg;
    if (1 != i2c_master_send(wl2864c_data.i2c_client, au8RegBuf, 1))
    {
        pr_err("%s:write reg error:reg=%x\n", __func__, reg);
        return -1;
    }
    if (1 != i2c_master_recv(wl2864c_data.i2c_client, &u8RdVal, 1))
    {
        pr_err("%s:read reg error:reg=%x,val=%x\n", __func__, reg, u8RdVal);
        return -1;
    }
    *val = u8RdVal;
    return 0;
}

int wl2864c_vin2_power(int power)
{
    pr_err("%s: power=%d\n", __func__, power);
   
    if(power)
    	gpio_set_value(wl2864c_data.vin2_gpio, 1);
    else
    	gpio_set_value(wl2864c_data.vin2_gpio, 0);

    return 0;
}


int wl2864c_ldo_vout(int ldo_num, int ldo_vout)
{
    int ret = -1;
#if 1
    u8 ldo_val = 0;
	u8 val = 0;

    if(ldo_num < 1 || ldo_num > 7)
    {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return ret;
    }

	ldo_vout/=100;
	pr_err("%s: ldo_vout=%d\n", __func__, ldo_vout);

	if(ldo_vout==0)
		return 0;

    switch(ldo_num)
    {
    case 1: 
    case 2: 
		 if(ldo_vout<600)
			ldo_vout = 6000;
         ldo_val = (ldo_vout-6000)/125; //VOUT X = 0.6V + LDO X _VOUT[7:0] * 0.0125V
         break;
    case 3: 
    case 4: 
    case 5: 
    case 6: 
    case 7: 
	 	if(ldo_vout<1200)
			ldo_vout = 12000;
         ldo_val = (ldo_vout-12000)/125; //VOUT X = 1.2V + LDO X _VOUT[7:0] * 0.0125V.
         break;
    default:
	 pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
         return ret;
    }
#endif

    ret = wl2864c_write_reg(WL2864C_REG_LDO1_VOUT +ldo_num-1, ldo_val);
	wl2864c_read_reg(WL2864C_REG_LDO1_VOUT +ldo_num-1,&val);
    pr_err("%s: ldo_num=%d, ldo_vout(write_val:%x, read_val:%u), ret=%d\n", __func__, ldo_num, ldo_val, val, ret);
    if (ret < 0 )
    {
        pr_err("%s: ldo_num=%d ldo_vout write fail ret=%d\n", __func__, ldo_num, ret);
        return ret;
    }
    return ret;
}

int wl2864c_ldo_en(int ldo_num, int en)
{
    int ret = 0;
    u8 ldo_val = 0, val=0;
#ifdef WL2864C_DUMP
    u8 ii = 0;
#endif

    if(ldo_num < 1 || ldo_num > 7)
    {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return ret;
    }
#if 0
    gpio_set_value(wl2864c_data.en_gpio, 1);
#endif

    ret = wl2864c_read_reg(WL2864C_REG_LDO_EN, &ldo_val);
    pr_err("%s: ldo_num=%d, ldo_en read val=%x, ret=%d\n", __func__, ldo_num, ldo_val, ret);
    if (ret < 0) {
        pr_err("%s: ldo_en read fail ret=%d\n", __func__, ret);
        return -ENODEV;
    }

    val = 1 << (ldo_num-1);
    if(en)
		ldo_val |= val;
    else 
		ldo_val &= ~val;

    ret = wl2864c_write_reg(WL2864C_REG_LDO_EN, ldo_val);
    pr_err("%s: ldo_num=%d, ldo_en write val=%x, ret=%d\n", __func__, ldo_num, ldo_val, ret);
    if (ret < 0 )
    {
        pr_err("%s: ldo_en write fail ret=%d\n", __func__, ret);
        return ret;
    }

#ifdef WL2864C_DUMP
    for(ii=0; ii<0x10; ii++)
    {
    	ret = wl2864c_read_reg(ii, &val);
        pr_err("%s: read ii=0x%x, val=0x%x, ret=%d\n", __func__, ii, val, ret);
    }
#endif
    return ret;
}


int wl2868c_ldo_vout(int ldo_num, int ldo_vout)
{
    int ret = -1;
#if 1
    u8 ldo_val = 0;
	u8 val = 0;

    if(ldo_num < 1 || ldo_num > 7)
    {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return ret;
    }

	ldo_vout/=100;
	pr_err("%s: ldo_vout=%d\n", __func__, ldo_vout);

	if(ldo_vout==0)
		return 0;

    switch(ldo_num)
    {
    case 1: 
    case 2: 
		 if(ldo_vout<496)
			ldo_vout = 4960;
         ldo_val = (ldo_vout-4960)/80; //VOUT X = 0.496V + LDO X _VOUT[7:0] * 0.008V
         break;
    case 3: 
    case 4: 
    case 5: 
    case 6: 
    case 7: 
	 	if(ldo_vout<1504)
			ldo_vout = 15040;
         ldo_val = (ldo_vout-15040)/80; //VOUT X = 1.504V + LDO X _VOUT[7:0] * 0.008V.
         break;
    default:
	 pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
         return ret;
    }
#endif

    ret = wl2864c_write_reg(WL2864C_REG_LDO1_VOUT +ldo_num-1, ldo_val);
	wl2864c_read_reg(WL2864C_REG_LDO1_VOUT +ldo_num-1,&val);
    pr_err("%s: ldo_num=%d, ldo_vout(write_val:%x, read_val:%u), ret=%d\n", __func__, ldo_num, ldo_val, val, ret);
    if (ret < 0 )
    {
        pr_err("%s: ldo_num=%d ldo_vout write fail ret=%d\n", __func__, ldo_num, ret);
        return ret;
    }
    return ret;
}

int wl2868c_ldo_en(int ldo_num, int en)
{
    int ret = 0;
    u8 ldo_val = 0 , ldo_val_wr = 0, val=0;
#ifdef WL2864C_DUMP
    u8 ii = 0;
#endif

    if(ldo_num < 1 || ldo_num > 7)
    {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return ret;
    }
#if 0
    gpio_set_value(wl2864c_data.en_gpio, 1);
#endif

    ret = wl2864c_read_reg(WL2864C_REG_LDO_EN, &ldo_val);
    pr_err("%s: ldo_num=%d, ldo_en read val=%x, ret=%d\n", __func__, ldo_num, ldo_val, ret);
    if (ret < 0) {
        pr_err("%s: ldo_en read fail ret=%d\n", __func__, ret);
        return -ENODEV;
    }

    

    val = 1 << (ldo_num-1);
    if(en)
		ldo_val |= val;
    else 
		ldo_val &= ~val;

    ldo_val_wr =  ldo_val ? ldo_val|0x80: ldo_val&0x7F;

    ret = wl2864c_write_reg(WL2864C_REG_LDO_EN, ldo_val_wr);
    pr_err("%s: ldo_num=%d, ldo_en write val=%x %x, ret=%d\n", __func__, ldo_num, ldo_val , ldo_val_wr, ret);
    if (ret < 0 )
    {
        pr_err("%s: ldo_en write fail ret=%d\n", __func__, ret);
        return ret;
    }

#ifdef WL2864C_DUMP
    for(ii=0; ii<0x10; ii++)
    {
    	ret = wl2864c_read_reg(ii, &val);
        pr_err("%s: read ii=0x%x, val=0x%x, ret=%d\n", __func__, ii, val, ret);
    }
#endif
    return ret;
}


int will_ldo_vout(int ldo_num, int ldo_vout)
{
	pr_err("%s: wl2864c_data.chip_id%x\n", __func__, wl2864c_data.chip_id);
	if(wl2864c_data.chip_id == 0x01)
		wl2864c_ldo_vout(ldo_num,ldo_vout);
	else if(wl2864c_data.chip_id == 0x82)
		wl2868c_ldo_vout(ldo_num,ldo_vout);
	else 
		return -1;

	return 0;

}

int will_ldo_en(int ldo_num, int en)
{
	if(wl2864c_data.chip_id == 0x01)
		wl2864c_ldo_en(ldo_num,en);
	else if(wl2864c_data.chip_id == 0x82)
		wl2868c_ldo_en(ldo_num,en);
	else
		return -1;

	return 0;	
}


/*!
 * wl2864c power on function
 *
 * @param dev struct device *
 * @return  Error code indicating success or failure
 */
static int wl2864c_power_on(struct device *dev)
{
#ifdef __CUST_PMU_WL2864C_EN_PIN_NUM__
    int ret = 0;
#endif
    pr_err("%s: entry\n", __func__);

#if 0
    wl2864c_data.vin1_regulator = regulator_get(dev, "vin1");
    if (!IS_ERR(wl2864c_data.vin1_regulator))
    {
        ret = of_property_read_u32(dev->of_node, "vin1_vol", (u32 *) &(wl2864c_data.vin1_vol));
        if (ret)
        {
            pr_err("%s: vin1_vol missing or invalid\n", __func__);
            return ret;
        }
        regulator_set_voltage(wl2864c_data.vin1_regulator, 1500000, 1500000);
        ret = regulator_enable(wl2864c_data.vin1_regulator);
        if (ret)
        {
            pr_err("%s: vin1 set voltage error %d\n", __func__, ret);
            return ret;
        }
        else
        {
            pr_err("%s: vin1 set voltage %d ok\n", __func__, 
                wl2864c_data.vin1_vol);
        }
    }
    else
    {
        pr_err("%s: cannot get vin1 voltage error\n", __func__);
        wl2864c_data.vin1_regulator = NULL;
    }
#endif

#if 0
    wl2864c_data.vin2_regulator = devm_regulator_get(dev, "vin2");
    if (!IS_ERR(wl2864c_data.vin2_regulator))
    {
        ret = of_property_read_u32(dev->of_node, "vin2_vol",
                    (u32 *) &(wl2864c_data.vin2_vol));
        if (ret)
        {
            pr_err("%s: vin2_vol missing or invalid\n", __func__);
            return ret;
        }
        regulator_set_voltage(wl2864c_data.vin2_regulator, wl2864c_data.vin2_vol, wl2864c_data.vin2_vol);
        ret = regulator_enable(wl2864c_data.vin2_regulator);
        if (ret)
        {
            pr_err("%s: vin2 set voltage error\n", __func__);
            return ret;
        }
        else
        {
            pr_err("%s: vin2 set voltage %d ok\n", __func__, 
                wl2864c_data.vin2_vol);
        }
    }
    else
    {
        pr_err("%s: cannot get vin2 voltage error\n", __func__);
        wl2864c_data.vin2_regulator = NULL;
    }
#endif

#if 0
    wl2864c_data.vin2_gpio = of_get_named_gpio(dev->of_node, "vin2_gpio", 0);
    pr_err("%s: wl2864c_data.vin2_gpio %d\n", __func__, wl2864c_data.vin2_gpio);
    if (!gpio_is_valid(wl2864c_data.vin2_gpio))
    {
        pr_err("%s: no en pin available", __func__);
        return -EINVAL;
    }
    ret = devm_gpio_request_one(dev, wl2864c_data.vin2_gpio, GPIOF_OUT_INIT_HIGH, "wl2864c_vin2");
    if (ret < 0)
    {
        pr_err("%s: wl2864c_en request failed %d\n", __func__, ret);
        return ret;
    }
    else
    {
        pr_err("%s: vin2_gpio request ok\n", __func__);
    }
#endif
#ifdef __CUST_PMU_WL2864C_EN_PIN_NUM__
    wl2864c_data.en_gpio = of_get_named_gpio(dev->of_node, "en-gpios", 0);
    pr_err("%s: wl2864c_data.en_gpio %d\n", __func__, wl2864c_data.en_gpio);
    if (!gpio_is_valid(wl2864c_data.en_gpio))
    {
        pr_err("%s: no en pin available", __func__);
        return -EINVAL;
    }
    ret = devm_gpio_request_one(dev, wl2864c_data.en_gpio, GPIOF_OUT_INIT_LOW, "wl2864c_en");
    if (ret < 0)
    {
        pr_err("%s: wl2864c_en request failed %d\n", __func__, ret);
        return ret;
    }
    else
    {
	
        pr_err("%s: en-gpios request ok\n", __func__);
	gpio_set_value(wl2864c_data.en_gpio, 1);
    }
#endif
    return 0;
}

/*!
 * wl2864c match id function
 *
 * @param dev struct device *
 * @return  Error code indicating success or failure
 */
#if 0
static int wl2864c_match_id(struct device *dev)
{
    int ret = 0;
    ret = of_property_read_u32(dev->of_node, "id_reg", (u32 *) &(wl2864c_data.id_reg));
    if (ret)
    {
        pr_err("%s: id_reg missing or invalid\n", __func__);
        return ret;
    }
    ret = of_property_read_u32(dev->of_node, "id_val", (u32 *) &(wl2864c_data.id_val));
    if (ret)
    {
        pr_err("%s: id_val missing or invalid\n", __func__);
        return ret;
    }
    ret = wl2864c_read_reg(wl2864c_data.id_reg, &(wl2864c_data.chip_id));

    if (ret < 0 || wl2864c_data.chip_id != wl2864c_data.id_val) {
        pr_err("%s: is not found %d %x\n", __func__, ret, wl2864c_data.chip_id);
        return -ENODEV;
    }
    pr_err("%s: is found %d\n", __func__, wl2864c_data.chip_id);
    return 0;
}


static int wl2868c_match_id(struct device *dev)
{
    int ret = 0;
    ret = of_property_read_u32(dev->of_node, "id_reg_2868c", (u32 *) &(wl2864c_data.id_reg));
    if (ret)
    {
        pr_err("%s: id_reg missing or invalid\n", __func__);
        return ret;
    }
    ret = of_property_read_u32(dev->of_node, "id_val_2868c", (u32 *) &(wl2864c_data.id_val));
    if (ret)
    {
        pr_err("%s: id_val missing or invalid\n", __func__);
        return ret;
    }
    ret = wl2864c_read_reg(wl2864c_data.id_reg, &(wl2864c_data.chip_id));

    if (ret < 0 || wl2864c_data.chip_id != wl2864c_data.id_val) {
        pr_err("%s: is not found %d %x\n", __func__, ret, wl2864c_data.chip_id);
        return -ENODEV;
    }
    pr_err("%s: is found %d\n", __func__, wl2864c_data.chip_id);
    return 0;
}
#endif
/*!
 * wl2864c init dev function
 *
 * @param dev struct device *
 * @return  Error code indicating success or failure
 */


static int wl2864c_init_dev(void)
{
    int ret = 0;
#ifdef WL2864C_DUMP
    u8 ii = 0, val=0;
#endif
    wl2864c_ldo_vout(1, 0x00); //1.2V
    wl2864c_ldo_vout(2, 0x00); //1.2V
    wl2864c_ldo_vout(3, 0x00); //2.8V
    wl2864c_ldo_vout(4, 0x00); //3.3V
    wl2864c_ldo_vout(5, 0x00); //2.8V
    wl2864c_ldo_vout(6, 0x00); //2.8V
    wl2864c_ldo_vout(7, 0x00); //1.8V

#if 0
	ret = wl2864c_write_reg(WL2864C_REG_LDO_EN, 0x7F);
    pr_err("%s: ldo_en write ret=%d\n", __func__, ret);
    if (ret < 0 )
    {
        pr_err("%s: ldo_en write fail ret=%d\n", __func__, ret);
        return ret;
    }
#endif

#ifdef WL2864C_DUMP
    for(ii=0; ii<0x10; ii++)
    {
    	ret = wl2864c_read_reg(ii, &val);
        pr_err("%s:dump_reg read ii=0x%x, val=0x%x, ret=%d\n", __func__, ii, val, ret);
    }
#endif
    return ret;
}


/*!
 * wl2864c GetHexCh function
 *
 * @param value u8
 * @param shift int
 * @return char value
 */
static char GetHexCh(
    u8 value, 
    int shift)
{
    u8 data = (value >> shift) & 0x0F;
    char ch = 0;
    if(data >= 10)
    {
        ch = data - 10  + 'A';
    }
    else if (data >= 0)
    {
        ch = data + '0';
    } 
    return ch;
}

/*!
 * wl2864c read function
 *
 * @param file struct file *
 * @param buf char __user *
 * @param count size_t
 * @param offset loff_t *
 * @return  read count
 */
static ssize_t wl2864c_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    char *buffer = NULL;
    int ret = 0, num = 0, i = 0;
    u8 u8add = wl2864c_data.offset, u8val = 0;
    buffer = kmalloc(WL2864C_IO_BUFFER_LIMIT, GFP_KERNEL);
    if (buffer == NULL) 
    {
        pr_err("%s: wl2864c: malloc failed %d\n", __func__, ret);
        return -ENOMEM;
    }
    if (count > WL2864C_IO_REG_LIMIT)
    {
        pr_err("%s: wl2864c: read count %d > %d\n", __func__, count, WL2864C_IO_REG_LIMIT);
        return -ERANGE;
    }
    
    pr_err("%s: wl2864c: read %d registers from %02X to %02X.\n", __func__, count, u8add, (u8add + count - 1));
    for (i = 0; i < count; i++, u8add++) 
    {
        ret = wl2864c_read_reg(u8add, &u8val);
        if (ret < 0) 
        {
            pr_err("%s: wl2864c: read %X failed %d\n", __func__, u8add, ret);
            kfree(buffer);
            return ret;
        }
        buffer[num++] = GetHexCh(u8add, 4);
        buffer[num++] = GetHexCh(u8add, 0);
        buffer[num++] = ' ';
        buffer[num++] = GetHexCh(u8val, 4);
        buffer[num++] = GetHexCh(u8val, 0);
        buffer[num++] = ' ';
        pr_err("%s: wl2864c: read REG[%02X %02X]\n", __func__, u8add, u8val);
    }
    copy_to_user(buf, buffer, num);
    kfree(buffer);
    return count;
}

/*!
 * wl2864c GetHex function
 *
 * @param ch char
 * @return hex value
 */
static u8 GetHex(char ch)
{
    u8 value = 0;
    if(ch >= 'a')
    {
        value = ch - 'a' + 10;
    } 
    else if (ch >= 'A')
    {
        value = ch - 'A' + 10;
    } 
    else if (ch >= '0')
    {
        value = ch - '0';
    } 
    return value;
}

/*!
 * wl2864c write function
 *
 * @param file struct file *
 * @param buf char __user *
 * @param count size_t
 * @param offset loff_t *
 * @return  write count
 */
static ssize_t wl2864c_write(struct file *file, const char __user *buf,size_t count, loff_t *offset)
{
    int ret = 0, i = 0;
    char *buffer = NULL;

    if (count > WL2864C_IO_BUFFER_LIMIT)
    {
        pr_err("%s: wl2864c: write size %d > %d\n", __func__, count, WL2864C_IO_BUFFER_LIMIT);
        return -ERANGE;
    }
    buffer = memdup_user(buf, count);
    if (IS_ERR(buffer))
    {
        pr_err("%s: wl2864c: can't get user data\n", __func__);
        return PTR_ERR(buffer);
    }
    pr_err("%s: wl2864c: write %d bytes.\n", __func__, count);

    for (i = 0; i < count; i += 6)
    {
        u8 u8add = (GetHex(buffer[i + 0]) << 4) | GetHex(buffer[i + 1]);
        u8 u8val = (GetHex(buffer[i + 3]) << 4) | GetHex(buffer[i + 4]);
        ret = wl2864c_write_reg(u8add, u8val);
        if (ret < 0 )
        {
            pr_err("%s: wl2864c: write failed %d\n", __func__, ret);
            kfree(buffer);
            return -ENODEV;
        }
        pr_err("%s: wl2864c: write REG[%02X %02X]\n", __func__, u8add, u8val);
    }
    kfree(buffer);
    return count;
}

/*!
 * wl2864c seek function
 *
 * @param file struct file *
 * @param offset loff_t
 * @param whence int
 * @return file pos
 */
loff_t wl2864c_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_CUR:
		wl2864c_data.offset += offset;
	        break;
	default:
        	wl2864c_data.offset = 0;
		break;
	}
    	pr_err("%s: wl2864c: update read pos to %02X\n", __func__, wl2864c_data.offset);
	return file->f_pos;;
}

/*!
 * wl2864c open function
 *
 * @param inode struct inode *
 * @param file struct file *
 * @return Error code indicating success or failure
 */
static int wl2864c_open(struct inode *inode, struct file *file)
{
    if (!wl2864c_data.on)
    {
        pr_err("%s: wl2864c: open failed.\n", __func__);
        return -ENODEV;
    }

    wl2864c_data.offset = 0;
    return 0;
}

/*!
 * file_operations struct
 */
static const struct file_operations wl2864c_fops = 
{
    .owner   = THIS_MODULE,
    .open    = wl2864c_open,
    .llseek  = wl2864c_llseek,
    .read    = wl2864c_read,
    .write   = wl2864c_write,
};

/*!
 * miscdevice struct
 */
static struct miscdevice wl2864c_miscdev = 
{
    .minor    = WL2864C_MISC_MAJOR,
    .name    = WL2864C_DRIVER_NAME,
    .fops    = &wl2864c_fops,
};

/*!
 * wl2864c I2C probe function
 *
 * @param client struct i2c_client *
 * @param id struct i2c_device_id *
 * @return  Error code indicating success or failure
 */
static int wl2864c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    memset(&wl2864c_data, 0, sizeof(struct wl2864c_data_t));
    
    wl2864c_data.i2c_client = client;


    pr_err("%s: entry\n", __func__);

    ret = wl2864c_power_on(&client->dev);
    if(ret)
    {
        pr_err("%s: wl2864c_power_on failed %d\n", __func__, ret);
        return ret;
    }
    msleep(1);
    #if 0
    ret = wl2864c_match_id(&client->dev);
    if(ret)
    {
            pr_err("%s: wl2864c_match_id failed %d\n", __func__, ret);
	    client->addr = 0x2F;
	    ret = wl2868c_match_id(&client->dev);
	    if(ret)
	    {
		pr_err("%s: wl2868c_match_id failed %d\n", __func__, ret);
        	return ret;
	    }

    }
    #endif
    if (200 > up_board_id_read())
    {
    	client->addr = 0x29;
    	wl2864c_data.chip_id = 0x01;    
    }else{
    	client->addr = 0x2F;
    	wl2864c_data.chip_id = 0x82;
    }
    
    

    ret = wl2864c_init_dev();
    if (ret < 0) 
    {
        pr_err("%s: wl2864c_init_dev failed\n", __func__);
        return ret;
    }

    ret = misc_register(&wl2864c_miscdev);
    if (ret < 0) 
    {
        pr_err("%s: failed to register wl2864c device\n", __func__);
        return ret;
    }
    wl2864c_data.on = true;
    pr_err("%s: wl2864c_probe successed! chip id = %d\n", __func__,wl2864c_data.chip_id);
    return 0;
}

/*!
 * wl2864c I2C remove function
 *
 * @param client struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int wl2864c_remove(struct i2c_client *client)
{

    misc_deregister(&wl2864c_miscdev);

    wl2864c_data.on = false;
    pr_err("%s: deregister wl2864c device ok\n", __func__);
    return 0;
}

/*!
 * i2c_device_id struct
 */
static const struct i2c_device_id wl2864c_id[] = 
{
    {WL2864C_DRIVER_NAME, 0},
    {},
};

/*!
 * i2c_driver struct
 */

static const struct of_device_id wl2864c_dt_match[] = {
    {.compatible = "will,wl2864c_pmu", },
    {},
};

static struct i2c_driver wl2864c_i2c_driver = 
{
    .probe  = wl2864c_probe,
    .remove = wl2864c_remove,
    .driver = 
        {
          .name  = WL2864C_DRIVER_NAME,
          .owner = THIS_MODULE,
          .of_match_table = of_match_ptr(wl2864c_dt_match),
        },
    .id_table = wl2864c_id,
};

/*!
 * wl2864c init function
 *
 * @return  Error code indicating success or failure
 */
static __init int wl2864c_init(void)
{
    u8 ret = 0;
    
    pr_err("%s: entry\n", __func__);
    ret = i2c_add_driver(&wl2864c_i2c_driver);

    if (ret != 0)
    {
        pr_err("%s: add driver failed, error=%d\n", __func__, ret);
        return ret;
    }
    pr_err("%s: add driver success\n", __func__);
    return ret;
}

/*!
 * WL2864C cleanup function
 */
static void __exit wl2864c_clean(void)
{
    i2c_del_driver(&wl2864c_i2c_driver);
}

module_init(wl2864c_init);
module_exit(wl2864c_clean);

MODULE_DESCRIPTION("WL2864C Power IC Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
