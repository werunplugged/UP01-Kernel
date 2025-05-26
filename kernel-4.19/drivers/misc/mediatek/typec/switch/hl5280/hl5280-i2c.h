/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */
#ifndef HL5280_I2C_H
#define HL5280_I2C_H

#include <linux/of.h>
#include <linux/notifier.h>

enum fsa_function {
	FSA_MIC_GND_SWAP,
	FSA_USBC_ORIENTATION_CC1,
	FSA_USBC_ORIENTATION_CC2,
	FSA_USBC_DISPLAYPORT_DISCONNECTED,
	FSA_EVENT_MAX,
};

#ifdef CONFIG_USBSWITCH_HL5280
int hl5280_switch_event(enum fsa_function event);
int hl5280_reg_notifier(struct notifier_block *nb);
int hl5280_unreg_notifier(struct notifier_block *nb);
#else
static inline int hl5280_switch_event(enum fsa_function event)
{
	return 0;
}

static inline int hl5280_reg_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int hl5280_unreg_notifier(struct notifier_block *nb)
{
	return 0;
}
#endif /* CONFIG_USBSWITCH_HL5280 */

#endif /* HL5280_I2C_H */

