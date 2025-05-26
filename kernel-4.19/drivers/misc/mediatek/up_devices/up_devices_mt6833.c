

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <linux/spi/spi.h>
#include <linux/iio/consumer.h>
#include <linux/of_platform.h>
#include "up_devices_mt6833.h"
#include "Cust_Memory_type.h"

#include <linux/cust_include/cust_project_all_config.h>
#ifdef __CUST_URXDO_GPIO_SELECT__
	#if __CUST_URXDO_GPIO_SELECT__
		#include <linux/of_gpio.h> 

		#define URXDO_SELECT_GPIO_NAME       "urxdo_gpio_select"

		static int up_urxdo_select_gpio_num;
		int up_urxdo_flag = 0;
		int get_urxdo_select_gpio(void);
		static int up_parse_urxdo_dts(struct device_node *node, const char *gpio_name);
	#endif
#endif

#define UP_DEVICE "up_device"
#define UP_VERSION "ALPS_S0MP1"

#define LCM_INFO	1
#define TPD_INFO	1
#define SENSOR_INFO	1

int up_board_id_read(void);

typedef int (*up_dev_print)(struct seq_file *se_f);

typedef int (*up_dev_set_used)(char * module_name, int pdata);
#if LCM_INFO
struct up_lcm_device_info
{
    struct list_head up_list;  // list in up_devices_info
    char lcm_module_descrip[50];
    struct list_head lcm_list;  // list in up_lcm_info
    struct mipi_dsi_driver* pdata;
    compatible_type type;
};
#endif
struct up_camera_device_dinfo
{
    struct list_head up_list;
    char camera_module_descrip[48];    
    struct list_head camera_list;
    int  camera_id;
    struct IMGSENSOR_SENSOR_LIST* pdata;
    compatible_type type;
    enum CAMERA_DUAL_CAMERA_SENSOR_ENUM  cam_type;
    int otp_state;
};
#if SENSOR_INFO
struct up_accsensor_device_dinfo
{
    struct list_head up_list;
    char sensor_descrip[20];
    struct list_head sensor_list;
    int dev_addr;
    int ps_direction;   // ps is throthold, als/msensor is diriction
    compatible_type type;
    struct sensor_info * pdata;
};

//MSENSOR
struct up_msensor_device_dinfo
{
    struct list_head up_list;
    char sensor_descrip[20];
    struct list_head sensor_list;
    int dev_addr;
    int ps_direction;   // ps is throthold, als/msensor is diriction
    compatible_type type;
    struct sensor_info * pdata;
};

//ALSPSSENSOR
struct up_alspssensor_device_dinfo
{
    struct list_head up_list;
    char sensor_descrip[20];
    struct list_head sensor_list;
    int dev_addr;
    int ps_direction;   // ps is throthold, als/alspssensor is diriction
    compatible_type type;
    struct sensor_info * pdata;
};
//BAROSENSOR
struct up_barosensor_device_dinfo
{
    struct list_head up_list;
    char sensor_descrip[20];
    struct list_head sensor_list;
    int dev_addr;
    int ps_direction;   // ps is throthold, barosensor is diriction
    compatible_type type;
    struct sensor_info * pdata;
};

//SARSENSOR
struct up_sarsensor_device_dinfo
{
    struct list_head up_list;
    char sensor_descrip[20];
    struct list_head sensor_list;
    int dev_addr;
    int ps_direction;   // ps is throthold, als/alspssensor is diriction
    compatible_type type;
    struct sensor_info * pdata;
};
#endif
#if TPD_INFO
struct up_touchpanel_device_dinfo
{
    struct list_head up_list;     // list in up_devices_info
    char touch_descrip[20];
    struct list_head touch_list;
    int dev_addr;
    struct i2c_driver * pdata;
	struct spi_driver * pdataspi;
//    int ps_direction;   // ps is throthold, als/msensor is diriction
    compatible_type type;
};

struct up_touchpanel_spi_device_dinfo
{
    struct list_head up_list;     // list in up_devices_info
    char touch_descrip[20];
    struct list_head touch_list;
    int dev_addr;
    struct spi_driver * pdata;
//    int ps_direction;   // ps is throthold, als/msensor is diriction
    compatible_type type;
};
#endif
struct up_type_info
{
    struct list_head up_device;  // list in up_devices_info
    struct list_head up_dinfo;    // list in up_touch_device_dinfo
    up_dev_print type_print;
    up_dev_set_used set_used;
    int dev_type;
    void * current_used_dinfo;
    struct mutex	info_mutex;
};

struct list_head updisp_info_list;

struct up_mis_disp_info
{
    struct list_head mis_info_list;  // list in up_devices_info
    void * current_used_dinfo;
};

struct up_devices_info
{
         struct list_head  type_list;   // up_type_info list
         struct list_head  dev_list;    // all_list

        struct mutex	de_mutex;
};

#ifdef __CUST_URXDO_GPIO_SELECT__
#if __CUST_URXDO_GPIO_SELECT__
static int up_parse_urxdo_dts(struct device_node *node, const char *gpio_name)
{
       int gpio_num = 0;
       struct gpio_desc *desc;
       int ret = 0;

       if (node)
       {
               gpio_num = of_get_named_gpio(node, gpio_name, 0);
               if (gpio_num < 0)
               {
                       printk("%s: of_get_named_gpio fail. \n", __func__);
                       return -1;
               }
               else
               {
                       printk("%s: of_get_named_gpio GPIO is %d.\n", __func__, gpio_num);
                       desc = gpio_to_desc(gpio_num);
                       if (!desc)
                       {
                               printk("%s: gpio_desc is null.\n", __func__);
                               return -1;
                       }
                       else
                               printk("%s: gpio_desc is not null.\n", __func__);

                       if (gpio_is_valid(gpio_num))
                               printk("%s: gpio number %d is valid. \n", __func__ ,gpio_num);

                       ret = gpio_request(gpio_num, gpio_name);
                       if (ret)
                       {
                               printk("%s: gpio_request fail. \n", __func__);
                               return -1;
                       }
                       else
                       {
                               ret = gpio_direction_input(gpio_num);
                               if (ret)
                               {
                                       printk("%s: gpio_direction_output failed. \n", __func__);
                                       return -1;
                               }

							   if(gpio_get_value(gpio_num))
							   {
									up_urxdo_flag = 0;
									printk("urxdo_1 %s: urxdo_gpio is <%d> status is <%d> flag = %d!\n", __func__,gpio_num,gpio_get_value(gpio_num),up_urxdo_flag);
							   }
							   else
							   {
									up_urxdo_flag = 1;
									printk("urxdo_2 %s: urxdo_gpio is <%d> status is <%d> flag = %d!\n", __func__,gpio_num,gpio_get_value(gpio_num),up_urxdo_flag);
							   }
                               return gpio_num;
                       }
               }
       }
       else
       {
               printk("%s: get gpio num fail. \n", __func__);
               return -1;
       }
}

int get_urxdo_select_gpio(void)
{
       struct device_node *node;
       printk("%s: enter. \n", __func__);

       node = of_find_compatible_node(NULL, NULL, "mediatek,up_devices");
       if (node)
       {
               up_urxdo_select_gpio_num = up_parse_urxdo_dts(node, URXDO_SELECT_GPIO_NAME);

               if (0 > up_urxdo_select_gpio_num)
               {
                       printk("%s: up_parse_dts fail. \n", __func__);
                       return -1;
               }
       }
       else
       {
               printk("%s: cannot get the node: 'mediatek,up_devices'.\n", __func__);
               return -ENODEV;
       }

       printk("%s: end. \n", __func__);
       return 0;
}

#endif
#endif//URXDO


struct up_devices_info * up_devices;
static int up_camera_info_print(struct seq_file *se_f);
static int up_touchpanel_info_print(struct seq_file *se_f);
static int up_accsensor_info_print(struct seq_file *se_f);
static int up_msensor_info_print(struct seq_file *se_f);
static int up_alspssensor_info_print(struct seq_file *se_f);
static int up_sarsensor_info_print(struct seq_file *se_f);
static int up_barosensor_info_print(struct seq_file *se_f);
static int up_lcm_set_used(char * module_name,int pdata);
static int up_camera_set_used(char * module_name, int pdata);
static int up_touchpanel_set_used(char * module_name, int pdata);
static int up_accsensor_set_used(char * module_name, int pdata);
static int up_msensor_set_used(char * module_name, int pdata);
static int up_alspssensor_set_used(char * module_name, int pdata);
static int up_sarsensor_set_used(char * module_name, int pdata);
static int up_barosensor_set_used(char * module_name, int pdata);
#if LCM_INFO
static int up_lcm_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * lcm_type_info;    
    struct up_lcm_device_info * plcm_device;
    int flag = -1;

    seq_printf(se_f, "---------UP LCM USAGE--------\t \n");
    
    list_for_each_entry(lcm_type_info,&up_devices->type_list,up_device){
            if(lcm_type_info->dev_type == ID_LCM_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
              list_for_each_entry(plcm_device,&lcm_type_info->up_dinfo,lcm_list){
                seq_printf(se_f, "      %s\t:   ",plcm_device->lcm_module_descrip);
                if(plcm_device->type == DEVICE_SUPPORTED)
                   seq_printf(se_f, "  supported \n");
                else
                    seq_printf(se_f, "  used \n");
                }
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
}
#endif
static void init_device_type_info(struct up_type_info * pdevice, void *used_info, int type)
{
       memset(pdevice,0, sizeof(struct up_type_info));
       INIT_LIST_HEAD(&pdevice->up_device);
       INIT_LIST_HEAD(&pdevice->up_dinfo);
       pdevice ->dev_type = type;
       if(used_info)
          pdevice->current_used_dinfo=used_info;
#if LCM_INFO       
       if(type == ID_LCM_TYPE)
       {
          pdevice->type_print = up_lcm_info_print;
          pdevice->set_used= up_lcm_set_used;
       }
       else 
#endif
	   if(type == ID_CAMERA_TYPE)
       {
           pdevice->type_print = up_camera_info_print;
           pdevice->set_used = up_camera_set_used;
       }
#if TPD_INFO
       else if(type == ID_TOUCH_TYPE)
       {
           pdevice->type_print = up_touchpanel_info_print;
           pdevice->set_used = up_touchpanel_set_used;
       }
#endif
#if SENSOR_INFO
       else if(type == ID_ACCSENSOR_TYPE)
       {
           pdevice->type_print = up_accsensor_info_print;
           pdevice->set_used = up_accsensor_set_used;
       }
       else if(type == ID_MSENSOR_TYPE)
       {
           pdevice->type_print = up_msensor_info_print;
           pdevice->set_used = up_msensor_set_used;
       }
       else if(type == ID_ALSPSSENSOR_TYPE)
       {
           pdevice->type_print = up_alspssensor_info_print;
           pdevice->set_used = up_alspssensor_set_used;
       }
       else if(type == ID_SARSENSOR_TYPE)
       {
           pdevice->type_print = up_sarsensor_info_print;
           pdevice->set_used = up_sarsensor_set_used;
       }
       else if(type == ID_BAROSENSOR_TYPE)
       {
           pdevice->type_print = up_barosensor_info_print;
           pdevice->set_used = up_barosensor_set_used;
       }
#endif
       mutex_init(&pdevice->info_mutex);
       list_add(&pdevice->up_device,&up_devices->type_list);
}
#if LCM_INFO
static void init_lcm_device(struct up_lcm_device_info *pdevice, struct mipi_dsi_driver* nLcm)
{
    memset(pdevice,0, sizeof(struct up_lcm_device_info));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->lcm_list);
    strcpy(pdevice->lcm_module_descrip,nLcm->driver.name);
    pdevice->pdata=nLcm;
    
}

static int up_lcm_set_used(char * module_name, int pdata)
{
    struct up_type_info * lcm_type_info;    
    struct up_lcm_device_info * plcm_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(lcm_type_info,&up_devices->type_list,up_device){
             if(lcm_type_info->dev_type == ID_LCM_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(plcm_device,&lcm_type_info->up_dinfo,lcm_list){
           if(!strcmp(module_name,plcm_device->lcm_module_descrip))
           {
               plcm_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}

int up_lcm_device_add(struct mipi_dsi_driver* nLcm, compatible_type isUsed)
{
    struct up_type_info * lcm_type_info;    
    struct up_lcm_device_info * plcm_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(lcm_type_info,&up_devices->type_list,up_device){
             if(lcm_type_info->dev_type == ID_LCM_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           lcm_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(lcm_type_info == NULL)
            {
                  printk("lcm_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
           
           if(isUsed)
             init_device_type_info(lcm_type_info, nLcm, ID_LCM_TYPE);
           else
             init_device_type_info(lcm_type_info, NULL, ID_LCM_TYPE);
       }
       else
       {
           if(isUsed &&(lcm_type_info->current_used_dinfo!=NULL))
             printk("~~~~add lcm error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(plcm_device,&lcm_type_info->up_dinfo,lcm_list){
              if(!strcmp(nLcm->driver.name,plcm_device->lcm_module_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             printk("error ___ lcm type is duplicated \n");
             goto duplicated_faild;
       }
       else
       {
           plcm_device = kmalloc(sizeof(struct up_lcm_device_info), GFP_KERNEL);
            if(plcm_device == NULL)
            {
                  printk("lcm_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto devicemalloc_faid;
            }
            
           init_lcm_device(plcm_device,nLcm);
           plcm_device->type=isUsed;
           
           
           list_add(&plcm_device->up_list, &up_devices->dev_list);
           list_add(&plcm_device->lcm_list,&lcm_type_info->up_dinfo);
       }
 
     
     return 0;
duplicated_faild:
devicemalloc_faid:
    kfree(plcm_device);
malloc_faid:
    
    printk("%s: error return: %x: ---\n",__func__,reterror);
    return reterror;
    
}
#endif

static int up_camera_set_used(char * module_name, int pdata)
{
    struct up_type_info * camera_type_info;    
    struct up_camera_device_dinfo * pcamera_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(camera_type_info,&up_devices->type_list,up_device){
             if(camera_type_info->dev_type == ID_CAMERA_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(pcamera_device,&camera_type_info->up_dinfo,camera_list){
           if(!strcmp(module_name,pcamera_device->camera_module_descrip))
           {
               pcamera_device->type = DEVICE_USED;
               pcamera_device->cam_type = pdata&0x00FF;
	       pcamera_device->otp_state = (pdata>>8)&0xff;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}



static int up_camera_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * camera_type_info;    
    struct up_camera_device_dinfo * pcamera_device;
    int flag = -1;

    seq_printf(se_f, "---------UP CAMERA USAGE-------- \n");

    list_for_each_entry(camera_type_info,&up_devices->type_list,up_device){
            if(camera_type_info->dev_type == ID_CAMERA_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
              list_for_each_entry(pcamera_device,&camera_type_info->up_dinfo,camera_list){
                seq_printf(se_f, "      %20s otp:%4d\t\t:   ",pcamera_device->camera_module_descrip,pcamera_device->otp_state);
                if(pcamera_device->type == DEVICE_SUPPORTED)
                   seq_printf(se_f, "  supported \n");
                else
                {
                    seq_printf(se_f, "  used     \t");
                    if(pcamera_device->cam_type ==DUAL_CAMERA_MAIN_SENSOR)
					{					
                        seq_printf(se_f, "  main camera ");

                        seq_printf(se_f, "\n");
					}
                    else if(pcamera_device->cam_type ==DUAL_CAMERA_SUB_SENSOR)
					{	
                        seq_printf(se_f, "  sub camera ");
						
                        seq_printf(se_f, "\n");
					}
                    else if(pcamera_device->cam_type == 3)
					{	
                        seq_printf(se_f, "  main2 camera \n");
					}
                    else if(pcamera_device->cam_type == 4)
					{	
                        seq_printf(se_f, "  sub2 camera \n");
					}
                    else if(pcamera_device->cam_type == 5)
					{	
                        seq_printf(se_f, "  main3 camera \n");
					}
                    else
                        seq_printf(se_f, " camera unsupportd type %d\n", pcamera_device->cam_type);
                }
                
                
                }
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
}

static void init_camera_device(struct up_camera_device_dinfo *pdevice, struct IMGSENSOR_SENSOR_LIST* mCamera)
{
    memset(pdevice,0, sizeof(struct up_camera_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->camera_list);
    strcpy(pdevice->camera_module_descrip,mCamera->name);
    pdevice->pdata=mCamera;
    
}


int up_camera_device_add(struct IMGSENSOR_SENSOR_LIST* mCamera, compatible_type isUsed)
{
    struct up_type_info * camera_type_info;    
    struct up_camera_device_dinfo * pcamera_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(camera_type_info,&up_devices->type_list,up_device){
             if(camera_type_info->dev_type == ID_CAMERA_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           camera_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);
           
           if(camera_type_info == NULL)
            {
                  printk("cam_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(camera_type_info, mCamera, ID_CAMERA_TYPE);
           else
             init_device_type_info(camera_type_info, NULL, ID_CAMERA_TYPE);
       }
       else
       {
           if(isUsed &&(camera_type_info->current_used_dinfo!=NULL))
             printk("~~~~add camera error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(pcamera_device,&camera_type_info->up_dinfo,camera_list){
              if(!strcmp(mCamera->name,pcamera_device->camera_module_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           pcamera_device = kmalloc(sizeof(struct up_camera_device_dinfo), GFP_KERNEL);
            if(camera_type_info == NULL)
            {
                  printk("cam_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_camera_device(pcamera_device,mCamera);
           pcamera_device->type=isUsed;
           
           list_add(&pcamera_device->up_list, &up_devices->dev_list);
           list_add(&pcamera_device->camera_list,&camera_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(pcamera_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}

#if TPD_INFO
static int up_touchpanel_set_used(char * module_name, int pdata)
{
    struct up_type_info * touchpanel_type_info;    
    struct up_touchpanel_device_dinfo * ptouchpanel_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(touchpanel_type_info,&up_devices->type_list,up_device){
             if(touchpanel_type_info->dev_type == ID_TOUCH_TYPE)
            {
                printk("touch type has find break !!\n");
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(ptouchpanel_device,&touchpanel_type_info->up_dinfo,touch_list){
           if(!strcmp(module_name,ptouchpanel_device->touch_descrip))
           {
               ptouchpanel_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}

static int up_touchpanel_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * touchpanel_type_info;    
    struct up_touchpanel_device_dinfo * ptouchpanel_device;
    int flag = -1;

    seq_printf(se_f, "--------UP TOUCHPANEL USAGE-------\t \n");

    list_for_each_entry(touchpanel_type_info,&up_devices->type_list,up_device){
            if(touchpanel_type_info->dev_type == ID_TOUCH_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
              list_for_each_entry(ptouchpanel_device,&touchpanel_type_info->up_dinfo,touch_list){
                seq_printf(se_f, "      %20s\t:   ",ptouchpanel_device->touch_descrip);
                if(ptouchpanel_device->type == DEVICE_SUPPORTED)
                   seq_printf(se_f, "  supported \n");
                else
                {
                    seq_printf(se_f, "  used \t");
                   	seq_printf(se_f, "  %20s\t \n", touch_fw_version);
                }
                
                }
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
    
}

static void init_touchpanel_device(struct up_touchpanel_device_dinfo *pdevice, struct i2c_driver * mTouch)
{
    memset(pdevice,0, sizeof(struct up_touchpanel_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->touch_list);
    strcpy(pdevice->touch_descrip,mTouch->driver.name);
    pdevice->pdata=mTouch;
    
}


int up_touchpanel_device_add(struct i2c_driver* mTouch, compatible_type isUsed)
{
    struct up_type_info * touchpanel_type_info;    
    struct up_touchpanel_device_dinfo * ptouchpanel_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(touchpanel_type_info,&up_devices->type_list,up_device){
             if(touchpanel_type_info->dev_type == ID_TOUCH_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           touchpanel_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(touchpanel_type_info == NULL)
            {
                  printk("tp_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(touchpanel_type_info, mTouch, ID_TOUCH_TYPE);
           else
             init_device_type_info(touchpanel_type_info, NULL, ID_TOUCH_TYPE);
       }
       else
       {
           if(isUsed &&(touchpanel_type_info->current_used_dinfo!=NULL))
             printk("~~~~add tp error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(ptouchpanel_device,&touchpanel_type_info->up_dinfo,touch_list){
              if(!strcmp(mTouch->driver.name,ptouchpanel_device->touch_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           ptouchpanel_device = kmalloc(sizeof(struct up_touchpanel_device_dinfo), GFP_KERNEL);
            if(ptouchpanel_device == NULL)
            {
                  printk("tp_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_touchpanel_device(ptouchpanel_device,mTouch);
           ptouchpanel_device->type=isUsed;
           
           list_add(&ptouchpanel_device->up_list, &up_devices->dev_list);
           list_add(&ptouchpanel_device->touch_list,&touchpanel_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(ptouchpanel_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}


int up_spitouchpanel_device_add(struct spi_driver* mTouch, compatible_type isUsed)
{
    struct up_type_info * touchpanel_type_info;    
    struct up_touchpanel_device_dinfo * ptouchpanel_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(touchpanel_type_info,&up_devices->type_list,up_device){
             if(touchpanel_type_info->dev_type == ID_TOUCH_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           touchpanel_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(touchpanel_type_info == NULL)
            {
                  printk("tp_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(touchpanel_type_info, mTouch, ID_TOUCH_TYPE);
           else
             init_device_type_info(touchpanel_type_info, NULL, ID_TOUCH_TYPE);
       }
       else
       {
           if(isUsed &&(touchpanel_type_info->current_used_dinfo!=NULL))
             printk("~~~~add tp error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(ptouchpanel_device,&touchpanel_type_info->up_dinfo,touch_list){
              if(!strcmp(mTouch->driver.name,ptouchpanel_device->touch_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           ptouchpanel_device = kmalloc(sizeof(struct up_touchpanel_device_dinfo), GFP_KERNEL);
            if(ptouchpanel_device == NULL)
            {
                  printk("tp_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }

			memset(ptouchpanel_device,0, sizeof(struct up_touchpanel_device_dinfo));
			INIT_LIST_HEAD(&ptouchpanel_device->up_list);
			INIT_LIST_HEAD(&ptouchpanel_device->touch_list);
			strcpy(ptouchpanel_device->touch_descrip,mTouch->driver.name);
			ptouchpanel_device->pdataspi=mTouch;

           ptouchpanel_device->type=isUsed;
           
           list_add(&ptouchpanel_device->up_list, &up_devices->dev_list);
           list_add(&ptouchpanel_device->touch_list,&touchpanel_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(ptouchpanel_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}





static void init_touchpanel_spi_device(struct up_touchpanel_spi_device_dinfo *pdevice, struct spi_driver * mTouch)
{
    memset(pdevice,0, sizeof(struct up_touchpanel_spi_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->touch_list);
    strcpy(pdevice->touch_descrip,mTouch->driver.name);
    pdevice->pdata=mTouch;
    
}


int up_touchpanel_spi_device_add(struct spi_driver* mTouch, compatible_type isUsed)
{
    struct up_type_info * touchpanel_type_info;    
    struct up_touchpanel_spi_device_dinfo * ptouchpanel_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(touchpanel_type_info,&up_devices->type_list,up_device){
             if(touchpanel_type_info->dev_type == ID_TOUCH_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           touchpanel_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(touchpanel_type_info == NULL)
            {
                  printk("tp_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(touchpanel_type_info, mTouch, ID_TOUCH_TYPE);
           else
             init_device_type_info(touchpanel_type_info, NULL, ID_TOUCH_TYPE);
       }
       else
       {
           if(isUsed &&(touchpanel_type_info->current_used_dinfo!=NULL))
             printk("~~~~add tp error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(ptouchpanel_device,&touchpanel_type_info->up_dinfo,touch_list){
              if(!strcmp(mTouch->driver.name,ptouchpanel_device->touch_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           ptouchpanel_device = kmalloc(sizeof(struct up_touchpanel_spi_device_dinfo), GFP_KERNEL);
            if(ptouchpanel_device == NULL)
            {
                  printk("tp_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_touchpanel_spi_device(ptouchpanel_device,mTouch);
           ptouchpanel_device->type=isUsed;
           
           list_add(&ptouchpanel_device->up_list, &up_devices->dev_list);
           list_add(&ptouchpanel_device->touch_list,&touchpanel_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(ptouchpanel_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}
#endif
#if SENSOR_INFO
static int up_accsensor_set_used(char * module_name, int pdata)
{
    struct up_type_info * accsensor_type_info;    
    struct up_accsensor_device_dinfo * paccsensor_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(accsensor_type_info,&up_devices->type_list,up_device){
             if(accsensor_type_info->dev_type == ID_ACCSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(paccsensor_device,&accsensor_type_info->up_dinfo,sensor_list){
           if(!strcmp(module_name,paccsensor_device->sensor_descrip))
           {
               paccsensor_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}

static int up_accsensor_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * accsensor_type_info;    
    struct up_accsensor_device_dinfo * paccsensor_device;
    int flag = -1;

    seq_printf(se_f, "--------UP accsensor USAGE-------\t \n");
    
    list_for_each_entry(accsensor_type_info,&up_devices->type_list,up_device){
            if(accsensor_type_info->dev_type == ID_ACCSENSOR_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
		list_for_each_entry(paccsensor_device,&accsensor_type_info->up_dinfo,sensor_list){

	        seq_printf(se_f, "      %20s\t\t:   ",paccsensor_device->sensor_descrip);
	        if(paccsensor_device->type == DEVICE_SUPPORTED)
	           seq_printf(se_f, "  supported \n");
	        else
	        {
	            seq_printf(se_f, "  used \t\n");
	        }
	    }
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
    
}

static void init_accsensor_device(struct up_accsensor_device_dinfo *pdevice, struct sensor_info * maccsensor)
{
    memset(pdevice,0, sizeof(struct up_accsensor_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->sensor_list);
    strcpy(pdevice->sensor_descrip,maccsensor->name);
    pdevice->pdata=maccsensor;
    
}

int up_accsensor_device_add(struct sensor_info* maccsensor, compatible_type isUsed)
{
    struct up_type_info * accsensor_type_info;    
    struct up_accsensor_device_dinfo * paccsensor_device;
    struct sensor_info * pacc_sensor;
    int flag = -1;
    int reterror=0;
    
    pacc_sensor = maccsensor;

     list_for_each_entry(accsensor_type_info,&up_devices->type_list,up_device){
             if(accsensor_type_info->dev_type == ID_ACCSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           accsensor_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(accsensor_type_info == NULL)
            {
                  printk("acc_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(accsensor_type_info, maccsensor, ID_ACCSENSOR_TYPE);
           else
             init_device_type_info(accsensor_type_info, NULL, ID_ACCSENSOR_TYPE);
       }
       else
       {
           if(isUsed &&(accsensor_type_info->current_used_dinfo!=NULL))
             printk("~~~~add accsensor error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(paccsensor_device,&accsensor_type_info->up_dinfo,sensor_list){
              if(!strcmp(maccsensor->name,paccsensor_device->sensor_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           paccsensor_device = (struct up_accsensor_device_dinfo *)kmalloc(sizeof(struct up_accsensor_device_dinfo), GFP_KERNEL);
            if(paccsensor_device == NULL)
            {
                  printk("acc_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_accsensor_device(paccsensor_device , pacc_sensor);

           paccsensor_device->type=isUsed;
           
           list_add(&paccsensor_device->up_list, &up_devices->dev_list);
           list_add(&paccsensor_device->sensor_list,&accsensor_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(paccsensor_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}

//MSENSOR START
static int up_msensor_set_used(char * module_name, int pdata)
{
    struct up_type_info * msensor_type_info;    
    struct up_msensor_device_dinfo * pmsensor_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(msensor_type_info,&up_devices->type_list,up_device){
             if(msensor_type_info->dev_type == ID_MSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(pmsensor_device,&msensor_type_info->up_dinfo,sensor_list){
           if(!strcmp(module_name,pmsensor_device->sensor_descrip))
           {
               pmsensor_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}

static int up_msensor_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * msensor_type_info;    
    struct up_msensor_device_dinfo * pmsensor_device;
    int flag = -1;

    seq_printf(se_f, "--------UP msensor USAGE-------\t \n");
    
    list_for_each_entry(msensor_type_info,&up_devices->type_list,up_device){
            if(msensor_type_info->dev_type == ID_MSENSOR_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
	    list_for_each_entry(pmsensor_device,&msensor_type_info->up_dinfo,sensor_list){

            seq_printf(se_f, "      %20s\t\t:   ",pmsensor_device->sensor_descrip);
            if(pmsensor_device->type == DEVICE_SUPPORTED)
               seq_printf(se_f, "  supported \n");
            else
            {
                seq_printf(se_f, "  used \t\n");
            }

		}
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    
    return 0;
}

static void init_msensor_device(struct up_msensor_device_dinfo *pdevice, struct sensor_info * mmsensor)
{
    memset(pdevice,0, sizeof(struct up_msensor_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->sensor_list);
    strcpy(pdevice->sensor_descrip,mmsensor->name);
    pdevice->pdata=mmsensor;
    
}

int up_msensor_device_add(struct sensor_info* mmsensor, compatible_type isUsed)
{
    struct up_type_info * msensor_type_info;    
    struct up_msensor_device_dinfo * pmsensor_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(msensor_type_info,&up_devices->type_list,up_device){
             if(msensor_type_info->dev_type == ID_MSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           msensor_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(msensor_type_info == NULL)
            {
                  printk("msensor_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(msensor_type_info, mmsensor, ID_MSENSOR_TYPE);
           else
             init_device_type_info(msensor_type_info, NULL, ID_MSENSOR_TYPE);
       }
       else
       {
           if(isUsed &&(msensor_type_info->current_used_dinfo!=NULL))
             printk("~~~~add msensor error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(pmsensor_device,&msensor_type_info->up_dinfo,sensor_list){
              if(!strcmp(mmsensor->name,pmsensor_device->sensor_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           pmsensor_device = kmalloc(sizeof(struct up_msensor_device_dinfo), GFP_KERNEL);
            if(pmsensor_device == NULL)
            {
                  printk("msensor_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_msensor_device(pmsensor_device,mmsensor);
           pmsensor_device->type=isUsed;
           
           list_add(&pmsensor_device->up_list, &up_devices->dev_list);
           list_add(&pmsensor_device->sensor_list,&msensor_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(pmsensor_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}
//MSENSOR END

//ALSPSSENSOR START
static int up_alspssensor_set_used(char * module_name, int pdata)
{
    struct up_type_info * alspssensor_type_info;    
    struct up_alspssensor_device_dinfo * palspssensor_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(alspssensor_type_info,&up_devices->type_list,up_device){
             if(alspssensor_type_info->dev_type == ID_ALSPSSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(palspssensor_device,&alspssensor_type_info->up_dinfo,sensor_list){
           if(!strcmp(module_name,palspssensor_device->sensor_descrip))
           {
               palspssensor_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}

static int up_alspssensor_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * alspssensor_type_info;    
    struct up_alspssensor_device_dinfo * palspssensor_device;
    int flag = -1;

    seq_printf(se_f, "--------UP alspssensor USAGE-------\t \n");
    
    list_for_each_entry(alspssensor_type_info,&up_devices->type_list,up_device){
            if(alspssensor_type_info->dev_type == ID_ALSPSSENSOR_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
	    list_for_each_entry(palspssensor_device,&alspssensor_type_info->up_dinfo,sensor_list){

            seq_printf(se_f, "      %20s\t\t:   ",palspssensor_device->sensor_descrip);
            if(palspssensor_device->type == DEVICE_SUPPORTED)
               seq_printf(se_f, "  supported \n");
            else
            {
                seq_printf(se_f, "  used \t\n");
            }

		}
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
}

static void init_alspssensor_device(struct up_alspssensor_device_dinfo *pdevice, struct sensor_info * malspssensor)
{
    memset(pdevice,0, sizeof(struct up_alspssensor_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->sensor_list);
    strcpy(pdevice->sensor_descrip,malspssensor->name);
    pdevice->pdata=malspssensor;
    
}


int up_alspssensor_device_add(struct sensor_info* malspssensor, compatible_type isUsed)
{
    struct up_type_info * alspssensor_type_info;    
    struct up_alspssensor_device_dinfo * palspssensor_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(alspssensor_type_info,&up_devices->type_list,up_device){
             if(alspssensor_type_info->dev_type == ID_ALSPSSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           alspssensor_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(alspssensor_type_info == NULL)
            {
                  printk("alsps_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(alspssensor_type_info, malspssensor, ID_ALSPSSENSOR_TYPE);
           else
             init_device_type_info(alspssensor_type_info, NULL, ID_ALSPSSENSOR_TYPE);
       }
       else
       {
           if(isUsed &&(alspssensor_type_info->current_used_dinfo!=NULL))
             printk("~~~~add alsps error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(palspssensor_device,&alspssensor_type_info->up_dinfo,sensor_list){
              if(!strcmp(malspssensor->name,palspssensor_device->sensor_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           palspssensor_device = kmalloc(sizeof(struct up_alspssensor_device_dinfo), GFP_KERNEL);
            if(palspssensor_device == NULL)
            {
                  printk("alsps_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_alspssensor_device(palspssensor_device,malspssensor);
           palspssensor_device->type=isUsed;
           
           list_add(&palspssensor_device->up_list, &up_devices->dev_list);
           list_add(&palspssensor_device->sensor_list,&alspssensor_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(palspssensor_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}
//ALSPSSENSOR END

//SARSENSOR START
static int up_sarsensor_set_used(char * module_name, int pdata)
{
    struct up_type_info * sarsensor_type_info;    
    struct up_sarsensor_device_dinfo * psarsensor_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(sarsensor_type_info,&up_devices->type_list,up_device){
             if(sarsensor_type_info->dev_type == ID_SARSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(psarsensor_device,&sarsensor_type_info->up_dinfo,sensor_list){
           if(!strcmp(module_name,psarsensor_device->sensor_descrip))
           {
               psarsensor_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1){
     	strncpy(up_sar_name_buf,psarsensor_device->sensor_descrip,6); 
        return 0;
    }
error_notype:
    return reterror;
    
}

static int up_sarsensor_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * sarsensor_type_info;    
    struct up_sarsensor_device_dinfo * psarsensor_device;
    int flag = -1;

    seq_printf(se_f, "--------UP sarsensor USAGE-------\t \n");
    
    list_for_each_entry(sarsensor_type_info,&up_devices->type_list,up_device){
            if(sarsensor_type_info->dev_type == ID_SARSENSOR_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
		list_for_each_entry(psarsensor_device,&sarsensor_type_info->up_dinfo,sensor_list){

	        seq_printf(se_f, "      %20s\t\t:   ",psarsensor_device->sensor_descrip);
	        if(psarsensor_device->type == DEVICE_SUPPORTED)
	           seq_printf(se_f, "  supported \n");
	        else
	        {
	            seq_printf(se_f, "  used \t\n");
	        }
	    }
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
    
}

static void init_sarsensor_device(struct up_sarsensor_device_dinfo *pdevice, struct sensor_info * msarsensor)
{
    memset(pdevice,0, sizeof(struct up_sarsensor_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->sensor_list);
    strcpy(pdevice->sensor_descrip,msarsensor->name);
    pdevice->pdata=msarsensor;
    
}

int up_sarsensor_device_add(struct sensor_info* msarsensor, compatible_type isUsed)
{
    struct up_type_info * sarsensor_type_info;    
    struct up_sarsensor_device_dinfo * psarsensor_device;
    struct sensor_info * psar_sensor;
    int flag = -1;
    int reterror=0;
    
    psar_sensor = msarsensor;

     list_for_each_entry(sarsensor_type_info,&up_devices->type_list,up_device){
             if(sarsensor_type_info->dev_type == ID_SARSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           sarsensor_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(sarsensor_type_info == NULL)
            {
                  printk("sar_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(sarsensor_type_info, msarsensor, ID_SARSENSOR_TYPE);
           else
             init_device_type_info(sarsensor_type_info, NULL, ID_SARSENSOR_TYPE);
       }
       else
       {
           if(isUsed &&(sarsensor_type_info->current_used_dinfo!=NULL))
             printk("~~~~add sarsensor error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(psarsensor_device,&sarsensor_type_info->up_dinfo,sensor_list){
              if(!strcmp(msarsensor->name,psarsensor_device->sensor_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           psarsensor_device = (struct up_sarsensor_device_dinfo *)kmalloc(sizeof(struct up_sarsensor_device_dinfo), GFP_KERNEL);
            if(psarsensor_device == NULL)
            {
                  printk("sar_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_sarsensor_device(psarsensor_device , psar_sensor);

           psarsensor_device->type=isUsed;
           
           list_add(&psarsensor_device->up_list, &up_devices->dev_list);
           list_add(&psarsensor_device->sensor_list,&sarsensor_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(psarsensor_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}

//SARSENSOR END

//BAROSENSOR START
static int up_barosensor_set_used(char * module_name, int pdata)
{
    struct up_type_info * barosensor_type_info;    
    struct up_barosensor_device_dinfo * pbarosensor_device;
    int flag = -1;
    int reterror=0;
    

     list_for_each_entry(barosensor_type_info,&up_devices->type_list,up_device){
             if(barosensor_type_info->dev_type == ID_BAROSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }
     
     if(flag == -1)  // this mean type is new
     {
        reterror = -1;
        goto error_notype;
     }

    flag =-1;

    list_for_each_entry(pbarosensor_device,&barosensor_type_info->up_dinfo,sensor_list){
           if(!strcmp(module_name,pbarosensor_device->sensor_descrip))
           {
               pbarosensor_device->type = DEVICE_USED;
               flag =1;  // this mean device is ok
               break;
           }
       }
    
    if(flag == 1)
        return 0;
    
error_notype:
    return reterror;
    
}

static int up_barosensor_info_print(struct seq_file *se_f)
{
    
    struct up_type_info * barosensor_type_info;    
    struct up_barosensor_device_dinfo * pbarosensor_device;
    int flag = -1;

    seq_printf(se_f, "--------UP barosensor USAGE-------\t \n");
    
    list_for_each_entry(barosensor_type_info,&up_devices->type_list,up_device){
            if(barosensor_type_info->dev_type == ID_BAROSENSOR_TYPE)
           {
               flag =1;  // this mean type is ok
               break;
           }
       }
    if(flag == 1)
    {
              list_for_each_entry(pbarosensor_device,&barosensor_type_info->up_dinfo,sensor_list){
                seq_printf(se_f, "      %20s\t\t:   ",pbarosensor_device->sensor_descrip);
                if(pbarosensor_device->type == DEVICE_SUPPORTED)
                   seq_printf(se_f, "  supported \n");
                else
                {
                    seq_printf(se_f, "  used \t\n");
                }
                }
    }
    else
    {
        seq_printf(se_f, "        \t:   NONE    \n");          
    }
    return 0;
}

static void init_barosensor_device(struct up_barosensor_device_dinfo *pdevice, struct sensor_info * mbarosensor)
{
    memset(pdevice,0, sizeof(struct up_barosensor_device_dinfo));
    INIT_LIST_HEAD(&pdevice->up_list);
    INIT_LIST_HEAD(&pdevice->sensor_list);
    strcpy(pdevice->sensor_descrip,mbarosensor->name);
    pdevice->pdata=mbarosensor;
    
}


int up_barosensor_device_add(struct sensor_info* mbarosensor, compatible_type isUsed)
{
    struct up_type_info * barosensor_type_info;    
    struct up_barosensor_device_dinfo * pbarosensor_device;
    int flag = -1;
    int reterror=0;

     list_for_each_entry(barosensor_type_info,&up_devices->type_list,up_device){
             if(barosensor_type_info->dev_type == ID_BAROSENSOR_TYPE)
            {
                flag =1;  // this mean type is ok
                break;
            }
        }

       if(flag == -1)  // this mean type is new
       {
           barosensor_type_info = kmalloc(sizeof(struct up_type_info), GFP_KERNEL);

           if(barosensor_type_info == NULL)
            {
                  printk("baro_alloc type info failed ~~~~ \n");
                  reterror = -1;
                  goto malloc_faid;
            }
       
           if(isUsed)
             init_device_type_info(barosensor_type_info, mbarosensor, ID_BAROSENSOR_TYPE);
           else
             init_device_type_info(barosensor_type_info, NULL, ID_BAROSENSOR_TYPE);
       }
       else
       {
           if(isUsed &&(barosensor_type_info->current_used_dinfo!=NULL))
             printk("~~~~add alsps error , duplicated current used lcm \n");
       }
     
       flag =-1;
       
       list_for_each_entry(pbarosensor_device,&barosensor_type_info->up_dinfo,sensor_list){
              if(!strcmp(mbarosensor->name,pbarosensor_device->sensor_descrip))
              {
                  flag =1;  // this mean device is ok
                  break;
              }
          }
       if(flag ==1)
       {
             goto duplicated_faild;
       }
       else
       {
           pbarosensor_device = kmalloc(sizeof(struct up_barosensor_device_dinfo), GFP_KERNEL);
            if(pbarosensor_device == NULL)
            {
                  printk("baro_alloc type info failed ~~~~ \n");
                  reterror = -2;
                  goto malloc_faid;
            }
           init_barosensor_device(pbarosensor_device,mbarosensor);
           pbarosensor_device->type=isUsed;
           
           list_add(&pbarosensor_device->up_list, &up_devices->dev_list);
           list_add(&pbarosensor_device->sensor_list,&barosensor_type_info->up_dinfo);
       }
 
          
          return 0;
          
     duplicated_faild:
         kfree(pbarosensor_device);
     malloc_faid:
         
         printk("%s: error return: %x: ---\n",__func__,reterror);
         return reterror;
    
}
//BAROSENSOR END
#endif

static int up_set_device_used(int dev_type, char * module_name, int pdata)
{
    struct up_type_info * type_info;    
    int ret_val = 0;
    
    list_for_each_entry(type_info,&up_devices->type_list,up_device){
        if(type_info->dev_type == dev_type)
        {
            if(type_info->set_used!=NULL)
                type_info->set_used(module_name,pdata);
        }
    }
    
    return ret_val;
}

#if LCM_INFO
int up_set_lcm_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_LCM_TYPE,module_name,pdata);
    return ret_val;
}
#endif

#if TPD_INFO
int up_set_touch_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_TOUCH_TYPE,module_name,pdata);
    return ret_val;
}
#endif

int up_set_camera_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_CAMERA_TYPE,module_name,pdata);
    return ret_val;
}
#if SENSOR_INFO
int up_set_accsensor_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_ACCSENSOR_TYPE,module_name,pdata);
    return ret_val;
}

//MSENSOR
int up_set_msensor_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_MSENSOR_TYPE,module_name,pdata);
    return ret_val;
}

//ALSPSSENSOR
int up_set_alspssensor_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_ALSPSSENSOR_TYPE,module_name,pdata);
    return ret_val;
}

//SARSENSOR
int up_set_sarsensor_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_SARSENSOR_TYPE,module_name,pdata);
    return ret_val;
}
//BAROSENSOR
int up_set_barosensor_device_used(char * module_name, int pdata)
{
    int ret_val = 0;
    ret_val = up_set_device_used(ID_BAROSENSOR_TYPE,module_name,pdata);
    return ret_val;
}
#endif
//extern LCM_DRIVER *lcm_driver_list[];
//extern unsigned int lcm_count;
extern int  proc_upinfo_init(void);
#if defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6877) || defined(CONFIG_MACH_MT6893)
extern struct IMGSENSOR_SENSOR_LIST gimgsensor_sensor_list[];
#else
extern struct ACDK_KD_SENSOR_INIT_FUNCTION_STRUCT kdSensorList[];
#endif
//extern LCM_DRIVER * DISP_GetLcmDrv(void);
//static LCM_DRIVER *lcm_drv = NULL;
#if 0 //LCM_INFO
static void init_lcm(void)
{
    int temp;
	lcm_drv =  DISP_GetLcmDrv();

    if(lcm_count ==1)
    {
        up_lcm_device_add(lcm_driver_list[0], DEVICE_USED);
    }
    else
    {
        for(temp= 0;temp< lcm_count;temp++)
        {
            if(lcm_drv == lcm_driver_list[temp])                    
                up_lcm_device_add(lcm_driver_list[temp], DEVICE_USED);
            else
                up_lcm_device_add(lcm_driver_list[temp], DEVICE_SUPPORTED);
        }
    }
}
#endif

void up_add_display_info(char * buffer, int lenth)
{
    struct up_mis_disp_info * new_display_info;
    char * buffer_info=NULL;

#if 0
   if(*(buffer+lenth)!="\0")
   {
         printk("display info error\n");
         return;
   }
#endif    
    new_display_info = kmalloc(sizeof(struct up_mis_disp_info), GFP_KERNEL );
    list_add(&new_display_info->mis_info_list , &updisp_info_list);
    buffer_info = kmalloc(lenth , GFP_KERNEL );
    new_display_info->current_used_dinfo = buffer_info;
    memcpy(buffer_info, buffer,lenth );
    
}

void up_display_info_dump(struct seq_file *m)
{
    struct up_mis_disp_info * pdisp_info=NULL;

    seq_printf(m, "----UP INFO DUMP---\t \n");

    list_for_each_entry(pdisp_info,&updisp_info_list ,mis_info_list){
        seq_printf(m, "Info:%s\n",(char *)pdisp_info->current_used_dinfo);        
    }
    
}

#ifdef EMMC_COMPATIBLE_NUM

extern char *saved_command_line;


int get_lpddr_emmc_used_index(void)
{
     char *ptr;
     int lpddr_index=0;
     ptr=strstr(saved_command_line,"lpddr_used_index=");
     if(ptr==NULL)
	     return -1;
     ptr+=strlen("lpddr_used_index=");
     lpddr_index=simple_strtol(ptr,NULL,10);
     return lpddr_index;
} 

int get_lpddr_emmc_flash_type(void)
{
     char *ptr;
     int flash_type=0;
     ptr=strstr(saved_command_line,"flash_type=");
     if(ptr==NULL)
	     return 0xFF;
     ptr+=strlen("flash_type=");
     flash_type=simple_strtol(ptr,NULL,10);
     return flash_type;
} 

#endif

int get_secure_boot_state(void)
{
     char *ptr;
     int secure_boot_state=0;
     ptr=strstr(saved_command_line,"androidboot.secureboot=");
     if(ptr==NULL)
	     return -1;
     ptr+=strlen("androidboot.secureboot=");
     secure_boot_state=simple_strtol(ptr,NULL,10);
     return secure_boot_state;
} 


#include <accdet.h>
extern struct head_dts_data accdet_dts;
#if !defined(CONFIG_SND_SOC_AW881XX_2PA)
#ifndef CONFIG_MTK_SPEAKER
#ifndef __CUST_EXTAMP_MODE__
#else
    extern int extamp_sound_mode;
#endif
#endif
#endif
extern char dump_stack_arch_desc_str[128];
char bbchip_name[128];

#if defined(CONFIG_SND_SOC_AW881XX_2PA)
    extern char *Digital_audio_pa;
    extern int pa_model;
    enum up_awinicpa_aw881xx_id {
	UP_AW881XX_CHIPID = 0x1806,
	UP_AW881XX_PID_01 = 0x01,
	UP_AW881XX_PID_02 = 0x02,
	UP_AW881XX_PID_03 = 0x03,
};
#endif

int up_device_dump(struct seq_file *m)
{

    struct up_type_info * type_info;    
    int value = 0;
    int i = 0;
#if 0 //LCM_INFO
    if(0 == ilcm_init)    
    {
        init_lcm();
        ilcm_init = 1;
    }
#endif

	for(i=0;i<strlen(dump_stack_arch_desc_str);i++)
	{
		if(dump_stack_arch_desc_str[i] ==' ')
		{
			bbchip_name[i] = '\0';
			break;
		}
		else
		{
			bbchip_name[i]=dump_stack_arch_desc_str[i];
		}
	}

    seq_printf(m,"base:%s \tBBChip:%s\n",UP_VERSION,bbchip_name);
    seq_printf(m," \t\t UP DEVICE DUMP--begin\n");
    value = up_board_id_read();
    seq_printf(m, "boardval:%d mv\n", value);
    list_for_each_entry(type_info,&up_devices->type_list,up_device)
    {
	    if(type_info->type_print == NULL)
	    {
	        printk(" error !!! this type [%d] has no dump fun \n",type_info->dev_type);
	    }
	    else
	        type_info->type_print(m);
    }
    
    seq_printf(m,"---------CUST MIC MODE-------- \n");
    seq_printf(m,"   accdet mic mode      :    %d\n",accdet_dts.mic_mode);
#if defined(CONFIG_SND_SOC_AW881XX_2PA)
	printk("pa_model=%d\n",pa_model);
    	switch(pa_model){
    		case UP_AW881XX_CHIPID:
    			seq_printf(m,"  Digital Audio Pa : [%s]\n",Digital_audio_pa);
    			return 0;
    		default:
    			seq_printf(m,"  Supported Digital Audio Pa dev HW isn't exist\n");
    			break;	
    	}
#else
	#ifndef CONFIG_MTK_SPEAKER
		#ifndef __CUST_EXTAMP_MODE__
	
		#else
		    seq_printf(m,"    UP_EXTAMP_HP_MODE   :    %d\n",extamp_sound_mode);
		#endif
	#endif
#endif
        seq_printf(m, "modem: %s\n", CUSTOM_MODEM);
	
    up_display_info_dump(m);
    return 0;
}


int up_memory_dump(struct seq_file *m)
{	
    #ifdef EMMC_COMPATIBLE_NUM
    {
        int tem = 0;
        int flash_idex=0;
        flash_idex = get_lpddr_emmc_used_index();
        seq_printf(m, "----UP Memory USAGE---\t \n");
        seq_printf(m," EMMC compatible num =%d,type=%x  \n", EMMC_COMPATIBLE_NUM,get_lpddr_emmc_flash_type());
        for(;tem<EMMC_COMPATIBLE_NUM;tem++)
        {
            if(flash_idex==tem)
                seq_printf(m," EMMC COMP[%d] =%s\t --used\n", tem, Cust_emmc_support[tem]);
            else
                seq_printf(m," EMMC COMP[%d] =%s\t--support\n", tem, Cust_emmc_support[tem]);
        }
        
        seq_printf(m," \t\t UP Memory DUMP--end\n\n");

    }
    #endif
    up_display_info_dump(m);
    
    return 0;
    
}


#ifdef UP_DEVICES_IOCTL_SUPPORT
#include "up_devices_ioctl.h"

static int  open_flag=1;
static spinlock_t lock;

static int up_devices_open (struct inode *ind,struct file * fl){
	spin_lock(&lock);
	if(1 != open_flag){
		spin_unlock(&lock);
		printk("up_devices_open already open\n");
		return -EBUSY;
	}

	open_flag --;
	spin_unlock(&lock);
	return 0;
}

static	int up_devices_release (struct inode *inode, struct file *file){
	printk("up_devices_release close\n");
	open_flag ++;
	return 0;
}
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int* rawdata);


static long up_devices_ioctl (struct file *file, unsigned int cmd, unsigned long arg){
	int ret = -1;
	int data = -1;
	int adcdata[4] = {0};
	int rawdata = 0;
	switch (cmd) {
	case UPIOC_GET_EXTAMP_SOUND_MODE :
		ret = copy_to_user((int *)arg, &extamp_sound_mode ,sizeof(int));
		printk("UPIOC_GET_EXTAMP_SOUND_MODE ret:%d,extamp_sound_mode:%d\n",ret,extamp_sound_mode);
		break;
	case UPIOC_SET_EXTAMP_SOUND_MODE :
		ret = copy_from_user(&data ,(int *)arg,sizeof(int));
		if (data < 1 || data > 5){
			printk("UPIOC_SET_EXTAMP_SOUND_MODE err,mode:%d \n",data);
			break;
		}else
			extamp_sound_mode = data;
		ret = copy_to_user((int *)arg, &extamp_sound_mode ,sizeof(int));
		break;
	case UPIOC_GET_ACCDET_MIC_MODE :
	
		ret = copy_to_user((unsigned int *)arg, & accdet_dts_data.accdet_mic_mode ,sizeof(int));
		break;
	case UPIOC_SET_ACCDET_MIC_MODE :

		break;
	case UPIOC_GET_ADC_VALUE:
		ret = copy_from_user(&data ,(int *)arg,sizeof(int));
		if (ret != 0) break;
		if( data < 0 || data > 14 ){
			printk("UPIOC_GET_ADC_VALUE data err,data:%d \n",data);
		}
		ret = IMM_GetOneChannelValue(data, adcdata, &rawdata);
		printk("up_adc_read Channel:%d rawdata= %x adcdata= %x %x, ret=%d \n",data,rawdata, adcdata[0], adcdata[1],ret);
		if (ret != 0) break;
		ret = copy_to_user((int *)arg, &rawdata ,sizeof(int));
		break;
	default:
		printk("up_devices ioctl is not exits\n");	
		break;
	}
	if (ret < 0){
		printk("up_devices ioctl failed\n");
	}
	return ret;
}


static const struct file_operations up_devices_fops = {
    .owner =    THIS_MODULE,
//    .write =    up_devices_write,
//    .read =     up_devices_read,
    .unlocked_ioctl = up_devices_ioctl,
//    .compat_ioctl = up_devices_compat_ioctl,
    .open =     up_devices_open,
    .release =  up_devices_release,
    .llseek =   no_llseek,
};


static struct miscdevice up_devices_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "up_devices",
    .fops = &up_devices_fops,
};

#endif 

int get_cts_flag_from_cmdline(void)
{
    char *ptr;
    int ret=0;
    ptr=strstr(saved_command_line,"up_cts_flag=");
    if(ptr==NULL)
        return -1;
    ptr+=strlen("up_cts_flag=");
    ret=simple_strtol(ptr,NULL,10);
    printk("get_cts_flag_from_cmdline. ret = %d. \n", ret);
    return ret ? 1 : 0;
} 

#ifdef __CUST_CTS_BOARD_BY_ADC__
#include <linux/iio/consumer.h>
#include <linux/of_platform.h>

int up_get_cts_volt_byadc(void)
{
	int cts_volt = 0;
	int ret = 0;
	int auxadc_voltage = 0;
	struct iio_channel *channel;
	struct device_node *device_node;
	struct platform_device *device_dev;

	device_node = of_find_node_by_name(NULL, "up_device");
	if (!device_node) {
		printk("[%s] of_find_node_by_name fail\n", __func__);
		return -1;
	}

	device_dev = of_find_device_by_node(device_node);
	if (!device_dev) {
		printk("[%s] of_find_device_by_node fail\n", __func__);
		return -1;
	}

	channel = iio_channel_get(&(device_dev->dev), "cts-ch");
	if (IS_ERR(channel)) {
		ret = PTR_ERR(channel);
		printk("[%s] iio channel not found %d\n", __func__, ret);
		return -1;
	}

	if (channel)
		ret = iio_read_channel_processed(channel, &auxadc_voltage);

	if (ret <= 0) {
		printk("[%s] iio_read_channel_processed failed\n", __func__);
		return -1;
	}

	printk("[%s] auxadc_voltage is %d\n", __func__, auxadc_voltage);
	cts_volt = auxadc_voltage;// * 1500 / 4096;
	printk("[%s] cts_voltage is %d\n", __func__, cts_volt);
	
	return cts_volt;
}
#endif

int get_old_machine_identification_result(void)
{
    char *ptr;
    int result = 0;
    ptr = strstr(saved_command_line,"old_machine_identification_result=");
    if(ptr==NULL)
        return -1;
    ptr += strlen("old_machine_identification_result=");
    result = simple_strtol(ptr,NULL,10);
    printk("%s: old_machine_identification_result= %d\n", __func__, result);
    return result ? 1 : 0;
} 

int is_up_cts_board(void)
{
#ifdef __CUST_URXDO_GPIO_SELECT__
#if __CUST_URXDO_GPIO_SELECT__
	return up_urxdo_flag;
#endif
#endif

#ifdef __CUST_CTS_BOARD_BY_ADC__
	int id_volt = 0;
	int ret;

	ret = up_get_cts_volt_byadc();
	if (ret < 0)
		printk("id_volt read fail\n");
	else {
		id_volt = ret;
		printk("id_volt = %d\n", id_volt);
	}
	if(id_volt > 500) {
		return true;
	} else {
		return false;
	}
#endif
    {
        int value = get_cts_flag_from_cmdline();
        printk("is_up_cts_board.  value = %d. \n", value);
        return (1 == value) ? 1: 0;
    }
	return 0;

}


int up_cts_dump(struct seq_file *m)
{
    seq_printf(m, is_up_cts_board() ? "1" : "0");
    return 0;
}

static ssize_t mt_sar_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{ 	
	if (!dev) {
		printk("mt_sar_name_show. dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%s\n", up_sar_name_buf);
}
DEVICE_ATTR(up_sar_name, 0664, mt_sar_name_show, NULL);
static ssize_t mt_cts_flag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int value;
	if (!dev) {
		printk("mt_cts_flag_show. dev is null!!\n");
		return 0;
	}
	value = is_up_cts_board() ? 1 : 0;
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}
DEVICE_ATTR(up_cts_flag, 0664, mt_cts_flag_show, NULL);
static ssize_t mt_fm_switch_flag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    	int value = 0;
	if (!dev) {
		printk("mt_fm_switch_flag_show. dev is null!!\n");
		return 0;
	}
	#ifdef __CUST_FM_SWITCH__
		value = 1;
	#else
		value = 0;
	#endif
	
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}
DEVICE_ATTR(up_fm_switch, 0664, mt_fm_switch_flag_show, NULL);


static ssize_t mt_secure_boot_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    	int value = 0;
	if (!dev) {
		printk("mt_secure_boot_show. dev is null!!\n");
		return 0;
	}
    value = get_secure_boot_state();
	
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}
DEVICE_ATTR(secure_boot, 0664, mt_secure_boot_show, NULL);



int up_board_id_read(void)
{
	struct device_node *device_node;
	struct platform_device *device_dev;
    	int value,ret = 0;
	struct iio_channel *channel;
	device_node = of_find_node_by_name(NULL, "up_device");
	if (!device_node) {
		printk("[%s] of_find_node_by_name fail\n", __func__);
		return -1;
	}

	device_dev = of_find_device_by_node(device_node);
	if (!device_dev) {
		printk("[%s] of_find_device_by_node fail\n", __func__);
		return -1;
	}

	channel = devm_iio_channel_get(&device_dev->dev, "board-id");
	if (IS_ERR(channel)) {
		ret = PTR_ERR(channel);
		return ret;
	}
	ret = iio_read_channel_processed(channel, &value);
	if (ret < 0) {
		printk("%s iio_read_channel_processed: fail(%d)\n", __func__, ret);
		return ret;
	}

	return value;


}

static ssize_t up_board_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    	int value = 0;

	value = up_board_id_read();
	if(value<0)
		return value;
	
	return scnprintf(buf, PAGE_SIZE, "vol:%dmv board:%s\n", value, value<200?"DVT1":value<650?"MP":value<1050?"DVT2":value<1300?"PVT":"TODO");
}
DEVICE_ATTR(up_board_id, 0664, up_board_id_show, NULL);

static struct device_attribute *mt_sysfs_attributes[] = {
    &dev_attr_up_cts_flag,
    &dev_attr_up_sar_name,
    &dev_attr_up_fm_switch,
    &dev_attr_up_board_id,
    &dev_attr_secure_boot,
	NULL
};

static int init_up_sysfs(struct device *dev)
{
	struct device_attribute **attr;
	int rc;
	printk("init_up_sysfs. begin. \n");

	for (attr = mt_sysfs_attributes; *attr; attr++) {
		rc = device_create_file(dev, *attr);
		if (rc)
			goto out_unreg;
	}
	return 0;

out_unreg:
	for (; attr >= mt_sysfs_attributes; attr--)
		device_remove_file(dev, *attr);
	return rc;
}



static int up_devices_probe(struct platform_device *pdev) 
{
        int temp;
        int err =0;
		struct device *dev = &pdev->dev;
        
        err = proc_upinfo_init();
        if(err<0)
            goto proc_error;
        
        up_devices = kmalloc(sizeof(struct up_devices_info), GFP_KERNEL);
        
        if(up_devices == NULL)
        {
             printk("%s: error probe becase of mem\n",__func__);
             return -1;
        }
        
        INIT_LIST_HEAD(&up_devices->type_list);
        INIT_LIST_HEAD(&up_devices->dev_list);
        mutex_init(&up_devices->de_mutex);

        INIT_LIST_HEAD(&updisp_info_list);
        

       for(temp=0;;temp++)
       {
#if defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6877) || defined(CONFIG_MACH_MT6893)
           if(gimgsensor_sensor_list[temp].init!=NULL)
           {
               up_camera_device_add(&gimgsensor_sensor_list[temp], DEVICE_SUPPORTED);
           }
#else
           if(kdSensorList[temp].SensorInit!=NULL)
           {
               up_camera_device_add(&kdSensorList[temp], DEVICE_SUPPORTED);
           }
#endif
           else
            break;
       }
#ifdef UP_DEVICES_IOCTL_SUPPORT
	err = misc_register(&up_devices_misc);
	if (err < 0) {
            	printk("%s: misc_register error!\n",__func__);
		goto  proc_error;
	}


	spin_lock_init(&lock);
	
#endif


	if (init_up_sysfs(dev)) {
		printk("failed to init_sysfs. \n");
	}


	return 0;
 proc_error:
       return err;
}
/*----------------------------------------------------------------------------*/
static int up_devices_remove(struct platform_device *pdev)
{
#ifdef UP_DEVICES_IOCTL_SUPPORT
	misc_deregister(&up_devices_misc);
#endif
	return 0;
}
#ifdef CONFIG_OF
struct of_device_id up_device_of_match[] = {
	{ .compatible = "mediatek,up_devices", },
	{},
};
#endif

/*----------------------------------------------------------------------------*/
static struct platform_driver up_devices_driver = {
	.probe      = up_devices_probe,
	.remove     = up_devices_remove,    
      .driver = {
              .name = UP_DEVICE,
              .owner = THIS_MODULE,
#ifdef CONFIG_OF
              .of_match_table = up_device_of_match,
#endif
      },

};

#ifndef CONFIG_OF
static struct platform_device up_dev = {
	.name		  = "up_devices",
	.id		  = -1,
};
#endif

/*----------------------------------------------------------------------------*/
int up_debug = 0;
static int __init up_devices_init(void)
{

#ifndef CONFIG_OF
    retval = platform_device_register(&up_dev);
    if (retval != 0){
        return retval;
    }
#endif

up_debug = 1;
    if(platform_driver_register(&up_devices_driver))
    {
up_debug = 0;    
    	printk("failed to register driver");
    	return -ENODEV;
    }
up_debug = 0;    

#ifdef __CUST_URXDO_GPIO_SELECT__
	#if __CUST_URXDO_GPIO_SELECT__
		if(0 > get_urxdo_select_gpio()) {
			printk("get_urxdo_select_gpio failed.\n");	
		}
	#endif
#endif   
    return 0;
}
/*----------------------------------------------------------------------------*/
static void __exit up_devices_exit(void)
{
	platform_driver_unregister(&up_devices_driver);
}
/*----------------------------------------------------------------------------*/
rootfs_initcall(up_devices_init);
module_exit(up_devices_exit);
/*----------------------------------------------------------------------------*/
MODULE_DESCRIPTION("UP DEVICE INFO");
MODULE_LICENSE("GPL");




