/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2017-2018 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 */

#ifndef _ASM_ARCH_IMX8M_CLOCK_H
#define _ASM_ARCH_IMX8M_CLOCK_H

#define MHZ(X)	((X) * 1000000UL)

#ifdef CONFIG_IMX8MQ
#include <asm/arch/clock_imx8mq.h>
#elif defined(CONFIG_IMX8MM) || defined(CONFIG_IMX8MN)
#include <asm/arch/clock_imx8mm.h>
#else
#error "Error no clock.h"
#endif

#endif
