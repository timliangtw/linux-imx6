/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>

#define PU_SOC_VOLTAGE_NORMAL	1250000
#define PU_SOC_VOLTAGE_HIGH	1275000
#define DC_VOLTAGE_MIN		1300000
#define DC_VOLTAGE_MAX		1400000
#define FREQ_1P2_GHZ		1200000000
#define FREQ_396_MHZ		396000
#define FREQ_696_MHZ		696000
#define FREQ_198_MHZ		198000
#define FREQ_24_MHZ		24000

static struct regulator *arm_reg;
static struct regulator *pu_reg;
static struct regulator *soc_reg;
static struct regulator *dc_reg;

static struct clk *arm_clk;
static struct clk *pll1_sys_clk;
static struct clk *pll1_sw_clk;
static struct clk *step_clk;
static struct clk *pll2_pfd2_396m_clk;

/* clk used by i.MX6UL */
static struct clk *pll1_bypass;
static struct clk *pll1_bypass_src;
static struct clk *pll1;
static struct clk *pll2_bus_clk;
static struct clk *secondary_sel_clk;

static struct device *cpu_dev;
static bool free_opp;
static struct cpufreq_frequency_table *freq_table;
static unsigned int transition_latency;
static struct mutex set_cpufreq_lock;
static u32 *imx6_soc_volt;
static u32 soc_opp_count;
static bool ignore_dc_reg;

static int imx6q_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct dev_pm_opp *opp;
	unsigned long freq_hz, volt, volt_old;
	unsigned int old_freq, new_freq;
	int ret;

	mutex_lock(&set_cpufreq_lock);

	new_freq = freq_table[index].frequency;
	freq_hz = new_freq * 1000;
	old_freq = policy->cur;

	/*
	 * ON i.MX6ULL, the 24MHz setpoint is not seen by cpufreq
	 * so we neet to prevent the cpufreq change frequency
	 * from 24MHz to 198Mhz directly. busfreq will handle this
	 * when exit from low bus mode.
	 */
	if (old_freq == FREQ_24_MHZ && new_freq == FREQ_198_MHZ) {
		mutex_unlock(&set_cpufreq_lock);
		return 0;
	};

	rcu_read_lock();
	opp = dev_pm_opp_find_freq_ceil(cpu_dev, &freq_hz);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(cpu_dev, "failed to find OPP for %ld\n", freq_hz);
		mutex_unlock(&set_cpufreq_lock);
		return PTR_ERR(opp);
	}

	volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();
	volt_old = regulator_get_voltage(arm_reg);

	dev_dbg(cpu_dev, "%u MHz, %ld mV --> %u MHz, %ld mV\n",
		old_freq / 1000, volt_old / 1000,
		new_freq / 1000, volt / 1000);
	/*
	 * CPU freq is increasing, so need to ensure
	 * that bus frequency is increased too.
	 */
	if (old_freq <= FREQ_396_MHZ && new_freq > FREQ_396_MHZ)
		request_bus_freq(BUS_FREQ_HIGH);

	/* scaling up?  scale voltage before frequency */
	if (new_freq > old_freq) {
		if (!IS_ERR(pu_reg)) {
			ret = regulator_set_voltage_tol(pu_reg, imx6_soc_volt[index], 0);
			if (ret) {
				dev_err(cpu_dev, "failed to scale vddpu up: %d\n", ret);
				mutex_unlock(&set_cpufreq_lock);
				return ret;
			}
		}
		ret = regulator_set_voltage_tol(soc_reg, imx6_soc_volt[index], 0);
		if (ret) {
			dev_err(cpu_dev, "failed to scale vddsoc up: %d\n", ret);
			mutex_unlock(&set_cpufreq_lock);
			return ret;
		}
		ret = regulator_set_voltage_tol(arm_reg, volt, 0);
		if (ret) {
			dev_err(cpu_dev,
				"failed to scale vddarm up: %d\n", ret);
			mutex_unlock(&set_cpufreq_lock);
			return ret;
		}
	}

	/*
	 * The setpoints are selected per PLL/PDF frequencies, so we need to
	 * reprogram PLL for frequency scaling.  The procedure of reprogramming
	 * PLL1 is as below.
	 * For i.MX6UL, it has a secondary clk mux, the cpu frequency change
	 * flow is slightly different from other i.MX6 OSC.
	 * The cpu frequeny change flow for i.MX6(except i.MX6UL) is as below:
	 *  - Enable pll2_pfd2_396m_clk and reparent pll1_sw_clk to it
	 *  - Reprogram pll1_sys_clk and reparent pll1_sw_clk back to it
	 *  - Disable pll2_pfd2_396m_clk
	 */
	if (of_machine_is_compatible("fsl,imx6ul")) {
		/*
		 * When changing pll1_sw_clk's parent to pll1_sys_clk,
		 * CPU may run at higher than 528MHz, this will lead to
		 * the system unstable if the voltage is lower than the
		 * voltage of 528MHz, so lower the CPU frequency to one
		 * half before changing CPU frequency.
		 */
		clk_set_rate(arm_clk, (old_freq >> 1) * 1000);
		clk_set_parent(pll1_sw_clk, pll1_sys_clk);
		if (freq_hz > clk_get_rate(pll2_pfd2_396m_clk))
			clk_set_parent(secondary_sel_clk, pll2_bus_clk);
		else
			clk_set_parent(secondary_sel_clk, pll2_pfd2_396m_clk);
		clk_set_parent(step_clk, secondary_sel_clk);
		clk_set_parent(pll1_sw_clk, step_clk);
		if (freq_hz > clk_get_rate(pll2_bus_clk)) {
			clk_set_rate(pll1, new_freq * 1000);
			clk_set_parent(pll1_sw_clk, pll1_sys_clk);
		}
	} else {
		clk_set_parent(step_clk, pll2_pfd2_396m_clk);
		clk_set_parent(pll1_sw_clk, step_clk);
		if (freq_hz > clk_get_rate(pll2_pfd2_396m_clk)) {
			clk_set_rate(pll1_sys_clk, new_freq * 1000);
			clk_set_parent(pll1_sw_clk, pll1_sys_clk);
		}
	}

	/* Ensure the arm clock divider is what we expect */
	ret = clk_set_rate(arm_clk, new_freq * 1000);
	if (ret) {
		dev_err(cpu_dev, "failed to set clock rate: %d\n", ret);
		regulator_set_voltage_tol(arm_reg, volt_old, 0);
		mutex_unlock(&set_cpufreq_lock);
		return ret;
	}

	/* scaling down?  scale voltage after frequency */
	if (new_freq < old_freq) {
		ret = regulator_set_voltage_tol(arm_reg, volt, 0);
		if (ret) {
			dev_warn(cpu_dev,
				 "failed to scale vddarm down: %d\n", ret);
			ret = 0;
		}
		ret = regulator_set_voltage_tol(soc_reg, imx6_soc_volt[index], 0);
		if (ret) {
			dev_warn(cpu_dev, "failed to scale vddsoc down: %d\n", ret);
			ret = 0;
		}
		if (!IS_ERR(pu_reg)) {
			ret = regulator_set_voltage_tol(pu_reg, imx6_soc_volt[index], 0);
			if (ret) {
				dev_warn(cpu_dev, "failed to scale vddpu down: %d\n", ret);
				ret = 0;
			}
		}
	}
	/*
	 * If CPU is dropped to the lowest level, release the need
	 * for a high bus frequency.
	 */
	if (old_freq > FREQ_396_MHZ && new_freq <= FREQ_396_MHZ)
		release_bus_freq(BUS_FREQ_HIGH);

	mutex_unlock(&set_cpufreq_lock);
	return 0;
}

static int imx6q_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret;

	policy->clk = arm_clk;
	policy->cur = clk_get_rate(arm_clk) / 1000;

	ret = cpufreq_generic_init(policy, freq_table, transition_latency);
	if (ret) {
		dev_err(cpu_dev, "imx6 cpufreq init failed!\n");
		return ret;
	}
	if (policy->cur > FREQ_396_MHZ)
		request_bus_freq(BUS_FREQ_HIGH);
	return 0;
}

static struct cpufreq_driver imx6q_cpufreq_driver = {
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = imx6q_set_target,
	.get = cpufreq_generic_get,
	.init = imx6q_cpufreq_init,
	.name = "imx6q-cpufreq",
	.attr = cpufreq_generic_attr,
};

static int imx6_cpufreq_pm_notify(struct notifier_block *nb,
	unsigned long event, void *dummy)
{
	struct cpufreq_policy *data = cpufreq_cpu_get(0);
	static u32 cpufreq_policy_min_pre_suspend;

	/*
	 * During suspend/resume, When cpufreq driver try to increase
	 * voltage/freq, it needs to control I2C/SPI to communicate
	 * with external PMIC to adjust voltage, but these I2C/SPI
	 * devices may be already suspended, to avoid such scenario,
	 * we just increase cpufreq to highest setpoint before suspend.
	 */
	if (!data)
		return NOTIFY_BAD;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		cpufreq_policy_min_pre_suspend = data->user_policy.min;
		data->user_policy.min = data->user_policy.max;

		if (!IS_ERR(dc_reg) && !ignore_dc_reg)
			regulator_set_voltage_tol(dc_reg, DC_VOLTAGE_MAX, 0);
		break;
	case PM_POST_SUSPEND:
		data->user_policy.min = cpufreq_policy_min_pre_suspend;

		if (!IS_ERR(dc_reg) && !ignore_dc_reg)
			regulator_set_voltage_tol(dc_reg, DC_VOLTAGE_MIN, 0);
		break;
	default:
		break;
	}

	cpufreq_update_policy(0);
	cpufreq_cpu_put(data);

	return NOTIFY_OK;
}

static struct notifier_block imx6_cpufreq_pm_notifier = {
	.notifier_call = imx6_cpufreq_pm_notify,
};

static int imx6q_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct dev_pm_opp *opp;
	unsigned long min_volt, max_volt;
	int num, ret;
	const struct property *prop;
	const __be32 *val;
	u32 nr, j, i = 0;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		pr_err("failed to get cpu0 device\n");
		return -ENODEV;
	}

	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		dev_err(cpu_dev, "failed to find cpu0 node\n");
		return -ENOENT;
	}

	arm_clk = devm_clk_get(cpu_dev, "arm");
	pll1_sys_clk = devm_clk_get(cpu_dev, "pll1_sys");
	pll1_sw_clk = devm_clk_get(cpu_dev, "pll1_sw");
	step_clk = devm_clk_get(cpu_dev, "step");
	pll2_pfd2_396m_clk = devm_clk_get(cpu_dev, "pll2_pfd2_396m");
	pll1 = devm_clk_get(cpu_dev, "pll1");
	pll1_bypass = devm_clk_get(cpu_dev, "pll1_bypass");
	pll1_bypass_src = devm_clk_get(cpu_dev, "pll1_bypass_src");
	if (IS_ERR(arm_clk) || IS_ERR(pll1_sys_clk) || IS_ERR(pll1_sw_clk) ||
	    IS_ERR(step_clk) || IS_ERR(pll2_pfd2_396m_clk) || IS_ERR(pll1) ||
	    IS_ERR(pll1_bypass) || IS_ERR(pll1_bypass_src)) {
		dev_err(cpu_dev, "failed to get clocks\n");
		ret = -ENOENT;
		goto put_node;
	}

	if (of_machine_is_compatible("fsl,imx6ul")) {
		pll2_bus_clk = clk_get(cpu_dev, "pll2_bus");
		secondary_sel_clk = clk_get(cpu_dev, "secondary_sel");
		if (IS_ERR(pll2_bus_clk) || IS_ERR(secondary_sel_clk)) {
			dev_err(cpu_dev, "failed to get clocks specific to imx6ul\n");
			ret = -ENOENT;
			goto put_node;
		}
	}

	arm_reg = devm_regulator_get(cpu_dev, "arm");
	pu_reg = devm_regulator_get_optional(cpu_dev, "pu");
	soc_reg = devm_regulator_get(cpu_dev, "soc");
	if (IS_ERR(arm_reg) || IS_ERR(soc_reg)) {
		dev_err(cpu_dev, "failed to get regulators\n");
		ret = -ENOENT;
		goto put_node;
	}

	dc_reg = devm_regulator_get_optional(cpu_dev, "dc");

	/*
	 * soc_reg sync  with arm_reg if arm shares the same regulator
	 * with soc. Otherwise, regulator common framework will refuse to update
	 * this consumer's voltage right now while another consumer voltage
	 * still keep in old one. For example, imx6sx-sdb with pfuze200 in
	 * ldo-bypass mode.
	 */
	of_property_read_u32(np, "fsl,arm-soc-shared", &i);
	if (i == 1)
		soc_reg = arm_reg;
	/*
	 * We expect an OPP table supplied by platform.
	 * Just, incase the platform did not supply the OPP
	 * table, it will try to get it.
	 */
	num = dev_pm_opp_get_opp_count(cpu_dev);
	if (num < 0) {
		ret = dev_pm_opp_of_add_table(cpu_dev);
		if (ret < 0) {
			dev_err(cpu_dev, "failed to init OPP table: %d\n", ret);
			goto put_node;
		}

		/* Because we have added the OPPs here, we must free them */
		free_opp = true;

		num = dev_pm_opp_get_opp_count(cpu_dev);
		if (num < 0) {
			ret = num;
			dev_err(cpu_dev, "no OPP table is found: %d\n", ret);
			goto out_free_opp;
		}
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_free_opp;
	}

	/*
	 * On i.MX6UL EVK board, if the SOC is run in overide frequency,
	 * the dc_regulator voltage should not be touched.
	 */
	if (freq_table[num - 1].frequency == FREQ_696_MHZ)
		ignore_dc_reg = true;
	if (!IS_ERR(dc_reg) && !ignore_dc_reg)
		regulator_set_voltage_tol(dc_reg, DC_VOLTAGE_MIN, 0);

	/* Make imx6_soc_volt array's size same as arm opp number */
	imx6_soc_volt = devm_kzalloc(cpu_dev, sizeof(*imx6_soc_volt) * num, GFP_KERNEL);
	if (imx6_soc_volt == NULL) {
		ret = -ENOMEM;
		goto free_freq_table;
	}

	prop = of_find_property(np, "fsl,soc-operating-points", NULL);
	if (!prop || !prop->value)
		goto soc_opp_out;

	/*
	 * Each OPP is a set of tuples consisting of frequency and
	 * voltage like <freq-kHz vol-uV>.
	 */
	nr = prop->length / sizeof(u32);
	if (nr % 2 || (nr / 2) < num)
		goto soc_opp_out;

	for (j = 0; j < num; j++) {
		val = prop->value;
		for (i = 0; i < nr / 2; i++) {
			unsigned long freq = be32_to_cpup(val++);
			unsigned long volt = be32_to_cpup(val++);
			if (freq_table[j].frequency == freq) {
				imx6_soc_volt[soc_opp_count++] = volt;
#ifdef CONFIG_MX6_VPU_352M
				if (freq == 792000) {
					pr_info("increase SOC/PU voltage for VPU352MHz\n");
					imx6_soc_volt[soc_opp_count - 1] = 1250000;
				}
#endif
				break;
			}
		}
	}

soc_opp_out:
	/* use fixed soc opp volt if no valid soc opp info found in dtb */
	if (soc_opp_count != num) {
		dev_warn(cpu_dev, "can NOT find valid fsl,soc-operating-points property in dtb, use default value!\n");
		for (j = 0; j < num; j++)
			imx6_soc_volt[j] = PU_SOC_VOLTAGE_NORMAL;
		if (freq_table[num - 1].frequency * 1000 == FREQ_1P2_GHZ)
			imx6_soc_volt[num - 1] = PU_SOC_VOLTAGE_HIGH;
	}

	if (of_property_read_u32(np, "clock-latency", &transition_latency))
		transition_latency = CPUFREQ_ETERNAL;

	/*
	 * Calculate the ramp time for max voltage change in the
	 * VDDSOC and VDDPU regulators.
	 */
	ret = regulator_set_voltage_time(soc_reg, imx6_soc_volt[0], imx6_soc_volt[num - 1]);
	if (ret > 0)
		transition_latency += ret * 1000;
	if (!IS_ERR(pu_reg)) {
		ret = regulator_set_voltage_time(pu_reg, imx6_soc_volt[0], imx6_soc_volt[num - 1]);
		if (ret > 0)
			transition_latency += ret * 1000;
	}

	/*
	 * OPP is maintained in order of increasing frequency, and
	 * freq_table initialised from OPP is therefore sorted in the
	 * same order.
	 */
	rcu_read_lock();
	opp = dev_pm_opp_find_freq_exact(cpu_dev,
				  freq_table[0].frequency * 1000, true);
	min_volt = dev_pm_opp_get_voltage(opp);
	opp = dev_pm_opp_find_freq_exact(cpu_dev,
				  freq_table[--num].frequency * 1000, true);
	max_volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();
	ret = regulator_set_voltage_time(arm_reg, min_volt, max_volt);
	if (ret > 0)
		transition_latency += ret * 1000;

	mutex_init(&set_cpufreq_lock);

	ret = cpufreq_register_driver(&imx6q_cpufreq_driver);
	if (ret) {
		dev_err(cpu_dev, "failed register driver: %d\n", ret);
		goto free_freq_table;
	}

	register_pm_notifier(&imx6_cpufreq_pm_notifier);

	of_node_put(np);
	return 0;

free_freq_table:
	dev_pm_opp_free_cpufreq_table(cpu_dev, &freq_table);
out_free_opp:
	if (free_opp)
		dev_pm_opp_of_remove_table(cpu_dev);
put_node:
	of_node_put(np);
	return ret;
}

static int imx6q_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&imx6q_cpufreq_driver);
	dev_pm_opp_free_cpufreq_table(cpu_dev, &freq_table);
	if (free_opp)
		dev_pm_opp_of_remove_table(cpu_dev);

	return 0;
}

static struct platform_driver imx6q_cpufreq_platdrv = {
	.driver = {
		.name	= "imx6q-cpufreq",
	},
	.probe		= imx6q_cpufreq_probe,
	.remove		= imx6q_cpufreq_remove,
};
module_platform_driver(imx6q_cpufreq_platdrv);

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale i.MX6Q cpufreq driver");
MODULE_LICENSE("GPL");
