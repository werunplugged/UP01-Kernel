/*
 * Generic GPIO card-detect helper
 *
 * Copyright (C) 2011, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "slot-gpio.h"

struct mmc_gpio {
	struct gpio_desc *ro_gpio;
	struct gpio_desc *cd_gpio;
	bool override_ro_active_level;
	bool override_cd_active_level;
	irqreturn_t (*cd_gpio_isr)(int irq, void *dev_id);
	char *ro_label;
	u32 cd_debounce_delay_ms;
	char cd_label[];
};

#if !(defined(CONFIG_MACH_MT6779) || defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6853) || defined(CONFIG_MACH_MT6877) || defined(CONFIG_MACH_MT6768) || defined(CONFIG_MACH_MT6893))
static struct timer_list * sd_debance_timer;
#define SD_DEBANCE_TIME_CHECK  128  // 512 ms - 256ms
static int irq_card_states;
static int previous_time_card_states;
static int sd_card_irq_level=0;
static int irq_same_state_count=0;
unsigned long sd_data;
void mmc_cd_debounce_process(void *dev_id);
extern unsigned int up_msdc1_cd_irq;
int up_detect_mmc_card_removed(void *dev_id)
{
       struct mmc_host *host = dev_id;
      int card_insert=0;
       if(host->ops->get_cd)
               card_insert=host->ops->get_cd(host);
      return card_insert;
}
static void sd_eint_timer_handler(struct timer_list *timer_list)
{
       unsigned int status;
       struct mmc_host *host = (void *)sd_data;
	   	struct mmc_gpio *ctx;
       status = up_detect_mmc_card_removed((void *)sd_data);
      if(status == previous_time_card_states)
      {
           irq_same_state_count ++;
      }
       else
       {
               previous_time_card_states = status;
               irq_same_state_count=0;
       }
       if(irq_same_state_count>3)
       {
               irq_same_state_count=0;
               printk("sd_eint_timer_handler irq_s=%d, previ_s=%d \n",irq_card_states ,previous_time_card_states);
               if(previous_time_card_states==irq_card_states)
               {
                       printk(int_timer_handler: repo card changed for core and enable irq~~~~ \n");
                       sd_card_irq_level=~sd_card_irq_level;
                       if(sd_card_irq_level)
                               irq_set_irq_type(up_msdc1_cd_irq, IRQF_TRIGGER_HIGH);
                       else
                               irq_set_irq_type(up_msdc1_cd_irq, IRQF_TRIGGER_LOW);
					   ctx = host->slot.handler_priv;
                       host->trigger_card_event = true;
                       mmc_detect_change(host, msecs_to_jiffies(200));        
               }
               enable_irq(up_msdc1_cd_irq);
       }
       else
       {
               mmc_cd_debounce_process((void *)sd_data);
       }
       return;
}
void mmc_cd_debounce_process(void *dev_id)
{
       struct timer_list *eint_timer = sd_debance_timer;
       int cpu = 0;
       eint_timer->expires = jiffies + msecs_to_jiffies(SD_DEBANCE_TIME_CHECK);
       printk("debance_timer=%d\n",msecs_to_jiffies(SD_DEBANCE_TIME_CHECK));
       sd_data = (unsigned long)dev_id;
       eint_timer->function = sd_eint_timer_handler;
       if (!timer_pending(eint_timer)) {
               timer_setup(eint_timer,sd_eint_timer_handler,0);
               add_timer_on(eint_timer, cpu);
       }
}
static irqreturn_t mmc_gpio_cd_irqt(int irq, void *dev_id)
{
       up_msdc1_cd_irq= irq;
	/* Schedule a card detection after a debounce timeout */
	//struct mmc_host *host = dev_id;
       irq_card_states = up_detect_mmc_card_removed(dev_id);
       previous_time_card_states = irq_card_states;
       printk("irq_prcess mmc_gpio_cd_irqt irq, card_cd = %d,\n",irq_card_states);
       disable_irq_nosync(up_msdc1_cd_irq);

       mmc_cd_debounce_process(dev_id);
       return IRQ_HANDLED;
}
#else
static irqreturn_t mmc_gpio_cd_irqt(int irq, void *dev_id)
{
	/* Schedule a card detection after a debounce timeout */
	struct mmc_host *host = dev_id;
	struct mmc_gpio *ctx = host->slot.handler_priv;

	host->trigger_card_event = true;
	mmc_detect_change(host, msecs_to_jiffies(ctx->cd_debounce_delay_ms));

	return IRQ_HANDLED;
}
#endif

int mmc_gpio_alloc(struct mmc_host *host)
{
	size_t len = strlen(dev_name(host->parent)) + 4;
	struct mmc_gpio *ctx = devm_kzalloc(host->parent,
				sizeof(*ctx) + 2 * len,	GFP_KERNEL);

	if (ctx) {
		ctx->ro_label = ctx->cd_label + len;
		ctx->cd_debounce_delay_ms = 200;
		snprintf(ctx->cd_label, len, "%s cd", dev_name(host->parent));
		snprintf(ctx->ro_label, len, "%s ro", dev_name(host->parent));
		host->slot.handler_priv = ctx;
		host->slot.cd_irq = -EINVAL;
	}

	return ctx ? 0 : -ENOMEM;
}

int mmc_gpio_get_ro(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;

	if (!ctx || !ctx->ro_gpio)
		return -ENOSYS;

	if (ctx->override_ro_active_level)
		return !gpiod_get_raw_value_cansleep(ctx->ro_gpio) ^
			!!(host->caps2 & MMC_CAP2_RO_ACTIVE_HIGH);

	return gpiod_get_value_cansleep(ctx->ro_gpio);
}
EXPORT_SYMBOL(mmc_gpio_get_ro);

int mmc_gpio_get_cd(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	int cansleep;

	if (!ctx || !ctx->cd_gpio)
		return -ENOSYS;

	cansleep = gpiod_cansleep(ctx->cd_gpio);
	if (ctx->override_cd_active_level) {
		int value = cansleep ?
				gpiod_get_raw_value_cansleep(ctx->cd_gpio) :
				gpiod_get_raw_value(ctx->cd_gpio);
		return !value ^ !!(host->caps2 & MMC_CAP2_CD_ACTIVE_HIGH);
	}

	return cansleep ?
		gpiod_get_value_cansleep(ctx->cd_gpio) :
		gpiod_get_value(ctx->cd_gpio);
}
EXPORT_SYMBOL(mmc_gpio_get_cd);

/**
 * mmc_gpio_request_ro - request a gpio for write-protection
 * @host: mmc host
 * @gpio: gpio number requested
 *
 * As devm_* managed functions are used in mmc_gpio_request_ro(), client
 * drivers do not need to worry about freeing up memory.
 *
 * Returns zero on success, else an error.
 */
int mmc_gpio_request_ro(struct mmc_host *host, unsigned int gpio)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	int ret;

	if (!gpio_is_valid(gpio))
		return -EINVAL;

	ret = devm_gpio_request_one(host->parent, gpio, GPIOF_DIR_IN,
				    ctx->ro_label);
	if (ret < 0)
		return ret;

	ctx->override_ro_active_level = true;
	ctx->ro_gpio = gpio_to_desc(gpio);

	return 0;
}
EXPORT_SYMBOL(mmc_gpio_request_ro);

void mmc_gpiod_request_cd_irq(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	int irq = -EINVAL;
	int ret;

	if (host->slot.cd_irq >= 0 || !ctx || !ctx->cd_gpio)
		return;

	/*
	 * Do not use IRQ if the platform prefers to poll, e.g., because that
	 * IRQ number is already used by another unit and cannot be shared.
	 */
	if (!(host->caps & MMC_CAP_NEEDS_POLL))
		irq = gpiod_to_irq(ctx->cd_gpio);

	if (irq >= 0) {
#if !(defined(CONFIG_MACH_MT6779) || defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6853) || defined(CONFIG_MACH_MT6877) || defined(CONFIG_MACH_MT6768) || defined(CONFIG_MACH_MT6893))
		if(sd_card_irq_level==0)
			ret = devm_request_threaded_irq(&host->class_dev, irq,
				NULL, mmc_gpio_cd_irqt,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				ctx->cd_label, host);
		else
			ret = devm_request_threaded_irq(&host->class_dev, irq,
				NULL, mmc_gpio_cd_irqt,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				ctx->cd_label, host);               
#else
		if (!ctx->cd_gpio_isr)
			ctx->cd_gpio_isr = mmc_gpio_cd_irqt;
		ret = devm_request_threaded_irq(host->parent, irq,
			NULL, ctx->cd_gpio_isr,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			ctx->cd_label, host);
#endif
		if (ret < 0)
			irq = ret;
	}

	host->slot.cd_irq = irq;

	if (irq < 0)
		host->caps |= MMC_CAP_NEEDS_POLL;
}
EXPORT_SYMBOL(mmc_gpiod_request_cd_irq);

int mmc_gpio_set_cd_wake(struct mmc_host *host, bool on)
{
	int ret = 0;

	if (!(host->caps & MMC_CAP_CD_WAKE) ||
	    host->slot.cd_irq < 0 ||
	    on == host->slot.cd_wake_enabled)
		return 0;

	if (on) {
		ret = enable_irq_wake(host->slot.cd_irq);
		host->slot.cd_wake_enabled = !ret;
	} else {
		disable_irq_wake(host->slot.cd_irq);
		host->slot.cd_wake_enabled = false;
	}

	return ret;
}
EXPORT_SYMBOL(mmc_gpio_set_cd_wake);

/* Register an alternate interrupt service routine for
 * the card-detect GPIO.
 */
void mmc_gpio_set_cd_isr(struct mmc_host *host,
			 irqreturn_t (*isr)(int irq, void *dev_id))
{
	struct mmc_gpio *ctx = host->slot.handler_priv;

	WARN_ON(ctx->cd_gpio_isr);
	ctx->cd_gpio_isr = isr;
}
EXPORT_SYMBOL(mmc_gpio_set_cd_isr);

/**
 * mmc_gpio_request_cd - request a gpio for card-detection
 * @host: mmc host
 * @gpio: gpio number requested
 * @debounce: debounce time in microseconds
 *
 * As devm_* managed functions are used in mmc_gpio_request_cd(), client
 * drivers do not need to worry about freeing up memory.
 *
 * If GPIO debouncing is desired, set the debounce parameter to a non-zero
 * value. The caller is responsible for ensuring that the GPIO driver associated
 * with the GPIO supports debouncing, otherwise an error will be returned.
 *
 * Returns zero on success, else an error.
 */
int mmc_gpio_request_cd(struct mmc_host *host, unsigned int gpio,
			unsigned int debounce)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	int ret;

	ret = devm_gpio_request_one(host->parent, gpio, GPIOF_DIR_IN,
				    ctx->cd_label);
	if (ret < 0)
		/*
		 * don't bother freeing memory. It might still get used by other
		 * slot functions, in any case it will be freed, when the device
		 * is destroyed.
		 */
		return ret;

	if (debounce) {
		ret = gpio_set_debounce(gpio, debounce);
		if (ret < 0)
			return ret;
	}

	ctx->override_cd_active_level = true;
	ctx->cd_gpio = gpio_to_desc(gpio);

	return 0;
}
EXPORT_SYMBOL(mmc_gpio_request_cd);

/**
 * mmc_gpiod_request_cd - request a gpio descriptor for card-detection
 * @host: mmc host
 * @con_id: function within the GPIO consumer
 * @idx: index of the GPIO to obtain in the consumer
 * @override_active_level: ignore %GPIO_ACTIVE_LOW flag
 * @debounce: debounce time in microseconds
 * @gpio_invert: will return whether the GPIO line is inverted or not, set
 * to NULL to ignore
 *
 * Use this function in place of mmc_gpio_request_cd() to use the GPIO
 * descriptor API.  Note that it must be called prior to mmc_add_host()
 * otherwise the caller must also call mmc_gpiod_request_cd_irq().
 *
 * Returns zero on success, else an error.
 */
int mmc_gpiod_request_cd(struct mmc_host *host, const char *con_id,
			 unsigned int idx, bool override_active_level,
			 unsigned int debounce, bool *gpio_invert)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	struct gpio_desc *desc;
	int ret;

	desc = devm_gpiod_get_index(host->parent, con_id, idx, GPIOD_IN);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	if (debounce) {
		ret = gpiod_set_debounce(desc, debounce);
		if (ret < 0)
			ctx->cd_debounce_delay_ms = debounce / 1000;
	}

	if (gpio_invert)
		*gpio_invert = !gpiod_is_active_low(desc);

	ctx->override_cd_active_level = override_active_level;
	ctx->cd_gpio = desc;
#if !(defined(CONFIG_MACH_MT6779) || defined(CONFIG_MACH_MT6833) || defined(CONFIG_MACH_MT6853) || defined(CONFIG_MACH_MT6877)|| defined(CONFIG_MACH_MT6768) || defined(CONFIG_MACH_MT6893))
	sd_debance_timer =
			kmalloc(sizeof(struct timer_list) , GFP_KERNEL);
	sd_debance_timer->expires = 0;
	sd_data = 0;
	sd_debance_timer->function = NULL;
	timer_setup(sd_debance_timer,NULL,0);
#endif
	return 0;
}
EXPORT_SYMBOL(mmc_gpiod_request_cd);

bool mmc_can_gpio_cd(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;

	return ctx->cd_gpio ? true : false;
}
EXPORT_SYMBOL(mmc_can_gpio_cd);

/**
 * mmc_gpiod_request_ro - request a gpio descriptor for write protection
 * @host: mmc host
 * @con_id: function within the GPIO consumer
 * @idx: index of the GPIO to obtain in the consumer
 * @override_active_level: ignore %GPIO_ACTIVE_LOW flag
 * @debounce: debounce time in microseconds
 * @gpio_invert: will return whether the GPIO line is inverted or not,
 * set to NULL to ignore
 *
 * Use this function in place of mmc_gpio_request_ro() to use the GPIO
 * descriptor API.
 *
 * Returns zero on success, else an error.
 */
int mmc_gpiod_request_ro(struct mmc_host *host, const char *con_id,
			 unsigned int idx, bool override_active_level,
			 unsigned int debounce, bool *gpio_invert)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	struct gpio_desc *desc;
	int ret;

	desc = devm_gpiod_get_index(host->parent, con_id, idx, GPIOD_IN);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	if (debounce) {
		ret = gpiod_set_debounce(desc, debounce);
		if (ret < 0)
			return ret;
	}

	if (gpio_invert)
		*gpio_invert = !gpiod_is_active_low(desc);

	ctx->override_ro_active_level = override_active_level;
	ctx->ro_gpio = desc;

	return 0;
}
EXPORT_SYMBOL(mmc_gpiod_request_ro);

bool mmc_can_gpio_ro(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;

	return ctx->ro_gpio ? true : false;
}
EXPORT_SYMBOL(mmc_can_gpio_ro);
