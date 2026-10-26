// SPDX-License-Identifier: GPL-2.0+
/*
 *  (C) Copyright 2024
 *  Svyatoslav Ryhel <clamor95@gmail.com>
 *
 * Based on U-Boot v2026.07 board/xiaomi/mocha/mocha.c.
 */

#include <dm.h>
#include <env.h>
#include <env_internal.h>
#include <fdt_support.h>
#include <hang.h>
#include <i2c.h>
#include <log.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <asm/arch/gpio.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/tegra.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;

/* Nonzero initializer keeps this latch in .data across U-Boot relocation.
 * Configure only the active-low Q0 power-key input before display probing.
 * The factory loader leaves its GPIO configuration disabled on this tablet.
 * Match the key's DT pinmux and never enable its output driver.
 */
static int boot_power_key = -1;

void pinmux_init(void)
{
	struct gpio_ctlr *ctlr = (struct gpio_ctlr *)NV_PA_GPIO_BASE;
	unsigned int gpio = TEGRA_GPIO(Q, 0);
	struct gpio_ctlr_bank *bank = &ctlr->gpio_bank[GPIO_BANK(gpio)];
	unsigned int bit = BIT(GPIO_BIT(gpio));
	unsigned int port = GPIO_PORT(gpio);

	/* Clear output enable before switching the pin from SFIO to GPIO. */
	clrbits_le32(&bank->gpio_dir_out[port], bit);
	pinmux_set_io(PMUX_PINGRP_KB_COL0_PQ0, PMUX_PIN_INPUT);
	pinmux_set_func(PMUX_PINGRP_KB_COL0_PQ0, PMUX_FUNC_RSVD2);
	pinmux_set_pullupdown(PMUX_PINGRP_KB_COL0_PQ0, PMUX_PULL_UP);
	pinmux_tristate_disable(PMUX_PINGRP_KB_COL0_PQ0);
	setbits_le32(&bank->gpio_config[port], bit);
	udelay(20);

	if ((readl(&bank->gpio_config[port]) & bit) &&
	    !(readl(&bank->gpio_dir_out[port]) & bit))
		boot_power_key = !!(readl(&bank->gpio_in[port]) & bit);
}

void nvidia_board_late_init(void)
{
	const char *state = boot_power_key < 0 ? "unavailable" :
		boot_power_key ? "released" : "held";

	/* A chainloaded image must leave the early 32-byte env_get() buffer
	 * before autoboot runs nested scripts. Import only compiled defaults;
	 * this board deliberately has no persistent environment backend.
	 */
	if (!(gd->flags & GD_FLG_ENV_READY)) {
		env_set_default(NULL, 0);
		if (!(gd->flags & GD_FLG_ENV_READY)) {
			puts("Mocha: cannot initialise the RAM boot environment\n");
			hang();
		}
	}
	if (env_set_ulong("mocha_desktop_requested", boot_power_key == 0) ||
	    env_set("mocha_power_key_early", state)) {
		puts("Mocha: cannot store the boot power-key state\n");
		hang();
	}
	printf("Mocha early power key: %s\n", state);
}

#ifdef CONFIG_MMC_SDHCI_TEGRA

#define TPS65913_I2C_ADDRESS			0x58
#define TPS65913_PRIMARY_SECONDARY_PAD2		0xfb
#define   GPIO_4				BIT(0)
#define TPS65913_PRIMARY_SECONDARY_PAD3		0xfe
#define   DVFS2					BIT(1)
#define   DVFS1					BIT(0)

/* We are using this function only till palmas pinctrl driver is available */
void pin_mux_mmc(void)
{
	struct udevice *dev;
	int ret;

	ret = i2c_get_chip_for_busnum(0, TPS65913_I2C_ADDRESS, 1, &dev);
	if (ret) {
		log_debug("%s: cannot find PMIC I2C chip\n", __func__);
		return;
	}

	/* GPIO4 function has to be GPIO */
	dm_i2c_reg_clrset(dev, TPS65913_PRIMARY_SECONDARY_PAD2,
			  GPIO_4, 0);

	/* DVFS1 and DVFS2 are disabled */
	dm_i2c_reg_clrset(dev, TPS65913_PRIMARY_SECONDARY_PAD3,
			  DVFS2 | DVFS1, 0);
}
#endif
