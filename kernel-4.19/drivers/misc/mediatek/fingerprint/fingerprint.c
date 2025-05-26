#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/compat.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#ifdef CONFIG_OF
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#endif
#include <linux/gpio.h>
#include <linux/spi/spi.h>
//#include "mtk_spi.h"
#include "fingerprint.h"

#include <linux/cust_include/cust_project_all_config.h>


DECLARE_WAIT_QUEUE_HEAD(finger_init_waiter);

int up_finger_probe_isok = 0; 

struct pinctrl *up_finger_pinctrl;
struct pinctrl_state *up_finger_reset_high,*up_finger_reset_low,*up_finger_spi0_mi_as_spi0_mi,*up_finger_spi0_mi_as_gpio,
*up_finger_spi0_mo_as_spi0_mo,*up_finger_spi0_mo_as_gpio,*up_finger_spi0_clk_as_spi0_clk,*up_finger_spi0_clk_as_gpio,
*up_finger_spi0_cs_as_spi0_cs,*up_finger_spi0_cs_as_gpio,*up_finger_eint_pull_down,*up_finger_eint_pull_up,*up_finger_eint_pull_dis;

int up_finger_get_gpio_info(struct platform_device *pdev)
{
	struct device_node *node;
	int ret;
	node = of_find_compatible_node(NULL, NULL, "mediatek,up_finger");
	printk("node.name <%s> full name <%s>\n",node->name,node->full_name);

	wake_up_interruptible(&finger_init_waiter);

	up_finger_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(up_finger_pinctrl)) {
		ret = PTR_ERR(up_finger_pinctrl);
		dev_err(&pdev->dev, "up_finger cannot find pinctrl and ret = (%d)\n",ret);
		return ret;
	}

	printk("[%s] up_finger_pinctrl+++++++++++++++++\n",pdev->name);

	up_finger_reset_high = pinctrl_lookup_state(up_finger_pinctrl, "finger_reset_en1");
	if (IS_ERR(up_finger_reset_high)) {
		ret = PTR_ERR(up_finger_reset_high);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_reset_high!\n");
		return ret;
	}
	up_finger_reset_low = pinctrl_lookup_state(up_finger_pinctrl, "finger_reset_en0");
	if (IS_ERR(up_finger_reset_low)) {
		ret = PTR_ERR(up_finger_reset_low);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_reset_low!\n");
		return ret;
	}
	up_finger_spi0_mi_as_spi0_mi = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_mi_as_spi0_mi");
	if (IS_ERR(up_finger_spi0_mi_as_spi0_mi)) {
		ret = PTR_ERR(up_finger_spi0_mi_as_spi0_mi);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_mi_as_spi0_mi!\n");
		return ret;
	}
	up_finger_spi0_mi_as_gpio = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_mi_as_gpio");
	if (IS_ERR(up_finger_spi0_mi_as_gpio)) {
		ret = PTR_ERR(up_finger_spi0_mi_as_gpio);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_mi_as_gpio!\n");
		return ret;
	}
	up_finger_spi0_mo_as_spi0_mo = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_mo_as_spi0_mo");
	if (IS_ERR(up_finger_spi0_mo_as_spi0_mo)) {
		ret = PTR_ERR(up_finger_spi0_mo_as_spi0_mo);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_mo_as_spi0_mo!\n");
		return ret;
	}
	up_finger_spi0_mo_as_gpio = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_mo_as_gpio");
	if (IS_ERR(up_finger_spi0_mo_as_gpio)) {
		ret = PTR_ERR(up_finger_spi0_mo_as_gpio);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_mo_as_gpio!\n");
		return ret;
	}
	up_finger_spi0_clk_as_spi0_clk = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_clk_as_spi0_clk");
	if (IS_ERR(up_finger_spi0_clk_as_spi0_clk)) {
		ret = PTR_ERR(up_finger_spi0_clk_as_spi0_clk);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_clk_as_spi0_clk!\n");
		return ret;
	}
	up_finger_spi0_clk_as_gpio = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_clk_as_gpio");
	if (IS_ERR(up_finger_spi0_clk_as_gpio)) {
		ret = PTR_ERR(up_finger_spi0_clk_as_gpio);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_clk_as_gpio!\n");
		return ret;
	}
	up_finger_spi0_cs_as_spi0_cs = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_cs_as_spi0_cs");
	if (IS_ERR(up_finger_spi0_cs_as_spi0_cs)) {
		ret = PTR_ERR(up_finger_spi0_cs_as_spi0_cs);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_cs_as_spi0_cs!\n");
		return ret;
	}
	up_finger_spi0_cs_as_gpio = pinctrl_lookup_state(up_finger_pinctrl, "finger_spi0_cs_as_gpio");
	if (IS_ERR(up_finger_spi0_cs_as_gpio)) {
		ret = PTR_ERR(up_finger_spi0_cs_as_gpio);
		dev_err(&pdev->dev, " Cannot find up_finger pinctrl up_finger_spi0_cs_as_gpio!\n");
		return ret;
	}

    	up_finger_eint_pull_down = pinctrl_lookup_state(up_finger_pinctrl, "finger_eint_pull_down");
    	if (IS_ERR(up_finger_eint_pull_down)) {
    		ret = PTR_ERR(up_finger_eint_pull_down);
    		dev_err(&pdev->dev, " Cannot find fp pinctrl up_finger_eint_pull_down!\n");
    		return ret;
    	}
    	up_finger_eint_pull_up= pinctrl_lookup_state(up_finger_pinctrl, "finger_eint_pull_up");
    	if (IS_ERR(up_finger_eint_pull_up)) {
    		ret = PTR_ERR(up_finger_eint_pull_up);
    		dev_err(&pdev->dev, " Cannot find fp pinctrl up_finger_eint_pull_up!\n");
    		return ret;
    	}
    
    	up_finger_eint_pull_dis = pinctrl_lookup_state(up_finger_pinctrl, "finger_eint_pull_dis");
    	if (IS_ERR(up_finger_eint_pull_dis)) {
    		ret = PTR_ERR(up_finger_eint_pull_dis);
    		dev_err(&pdev->dev, " Cannot find fp pinctrl up_finger_eint_pull_dis!\n");
    		return ret;
    	}

	printk("up_finger get gpio info ok--------\n");
	return 0;
}

int up_finger_set_reset(int cmd)
{

	if(IS_ERR(up_finger_reset_low)||IS_ERR(up_finger_reset_high))
	{
		 pr_err( "err: up_finger_reset_low or up_finger_reset_high is error!!!");
		 return -1;
	}	
	

	switch (cmd)
	{
	case 0 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_reset_low);
	break;
	case 1 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_reset_high);
	break;
	}
	return 0;
}

int up_finger_set_irq(int cmd)
{
        if(IS_ERR(up_finger_eint_pull_down)||IS_ERR(up_finger_eint_pull_up)||IS_ERR(up_finger_eint_pull_dis))
	{
		 pr_err( "err: up_finger_int_as_gpio is error!!!!");
		 return -1;
	}	

	switch (cmd)
	{
	case 0 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_eint_pull_down);
	break;
	case 1 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_eint_pull_up);
	break;
	case 2 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_eint_pull_dis);
	break;
	}
	return 0;


}

/*********************blestech(start  2018-1-19)***********/

unsigned int up_finger_get_irqnum(void){
    struct device_node *node = NULL;
    //MALOGF("start");
    node = of_find_compatible_node(NULL, NULL, "mediatek,up_finger");
    return irq_of_parse_and_map(node, 0);
}

unsigned int up_finger_get_irq_gpio(void){
    struct device_node *node = NULL;
    //MALOGF("start");
    node = of_find_compatible_node(NULL, NULL, "mediatek,up_finger");
    return of_get_named_gpio(node, "int-gpio", 0);
}

unsigned int up_finger_get_reset_gpio(void){
    struct device_node *node = NULL;
    //MALOGF("start");
    node = of_find_compatible_node(NULL, NULL, "mediatek,up_finger");
    return of_get_named_gpio(node, "reset-gpio", 0);
}

int up_finger_set_spi_mode(int cmd)
{

	if(IS_ERR(up_finger_spi0_clk_as_gpio)||IS_ERR(up_finger_spi0_cs_as_gpio)||IS_ERR(up_finger_spi0_mi_as_gpio) \
		||IS_ERR(up_finger_spi0_mo_as_gpio)||IS_ERR(up_finger_spi0_clk_as_spi0_clk)||IS_ERR(up_finger_spi0_cs_as_spi0_cs) \
		||IS_ERR(up_finger_spi0_mi_as_spi0_mi)||IS_ERR(up_finger_spi0_mo_as_spi0_mo))
	{
		 pr_err( "err: up_finger_reset_low or up_finger_reset_high is error!!!");
		 return -1;
	}	

	

	switch (cmd)
	{
	case 0 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_clk_as_gpio);
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_cs_as_gpio);
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_mi_as_gpio);
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_mo_as_gpio);
	break;
	case 1 : 		
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_clk_as_spi0_clk);
#if defined(CONFIG_FINGERPRINT_CHIPONE_REE)
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_cs_as_gpio);
#else
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_cs_as_spi0_cs);
#endif
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_mi_as_spi0_mi);
		pinctrl_select_state(up_finger_pinctrl, up_finger_spi0_mo_as_spi0_mo);
	break;
	}
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id up_finger_match[] = {
	{ .compatible = "mediatek,up_finger", },
	{}
};
MODULE_DEVICE_TABLE(of, up_finger_match);
#endif

static struct platform_device *up_finger_plat = NULL;


void up_waite_for_finger_dts_paser(void)
{
    if(up_finger_plat==NULL)
    {
        wait_event_interruptible_timeout(finger_init_waiter, up_finger_plat != NULL, 3 * HZ);
    }
    else
	return;
	
}

int up_get_max_finger_spi_cs_number(void)
{
    return MAX_FINGER_CHIP_CS_NUMBER;
}

int up_finger_plat_probe(struct platform_device *pdev) {
	up_finger_plat = pdev;
	printk("up_finger_plat_probe entry\n");
	up_finger_get_gpio_info(pdev);
	return 0;
}

int up_finger_plat_remove(struct platform_device *pdev) {
	up_finger_plat = NULL;
	return 0;
} 


#ifndef CONFIG_OF
static struct platform_device up_finger_dev = {
	.name		  = "up_finger",
	.id		  = -1,
};
#endif


static struct platform_driver up_finger_pdrv = {
	.probe	  = up_finger_plat_probe,
	.remove	 = up_finger_plat_remove,
	.driver = {
		.name  = "up_finger",
		.owner = THIS_MODULE,			
#ifdef CONFIG_OF
		.of_match_table = up_finger_match,
#endif
	}
};


static int __init up_finger_init(void)
{

#ifndef CONFIG_OF
    int retval=0;
    retval = platform_device_register(&up_finger_dev);
    if (retval != 0){
        return retval;
    }
#endif
    if(platform_driver_register(&up_finger_pdrv))
    {
    	printk("failed to register driver");
    	return -ENODEV;
    }
    
    return 0;
}
/*----------------------------------------------------------------------------*/
static void __exit up_finger_exit(void)
{
	platform_driver_unregister(&up_finger_pdrv);
}


rootfs_initcall(up_finger_init);
module_exit(up_finger_exit);

MODULE_DESCRIPTION("for up fingerprint driver");
MODULE_LICENSE("GPL");
