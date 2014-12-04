/*
 * Copyright 2011-2015 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>
#include <linux/irqchip/arm-gic.h>
#include "common.h"
#include "hardware.h"

#define GPC_CNTR		0x000
#define GPC_IMR1		0x008
#define GPC_PGC_MF_PDN		0x220
#define GPC_PGC_GPU_PDN		0x260
#define GPC_PGC_GPU_PUPSCR	0x264
#define GPC_PGC_GPU_PDNSCR	0x268
#define GPC_PGC_CPU_PDN		0x2a0
#define GPC_PGC_CPU_PUPSCR	0x2a4
#define GPC_PGC_CPU_PDNSCR	0x2a8
#define GPC_PGC_SW2ISO_SHIFT	0x8
#define GPC_PGC_SW_SHIFT	0x0
#define GPC_M4_LPSR		0x2c
#define GPC_M4_LPSR_M4_SLEEPING_SHIFT	4
#define GPC_M4_LPSR_M4_SLEEPING_MASK	0x1
#define GPC_M4_LPSR_M4_SLEEP_HOLD_REQ_MASK	0x1
#define GPC_M4_LPSR_M4_SLEEP_HOLD_REQ_SHIFT	0
#define GPC_M4_LPSR_M4_SLEEP_HOLD_ACK_MASK	0x1
#define GPC_M4_LPSR_M4_SLEEP_HOLD_ACK_SHIFT	1

#define IMR_NUM			4
#define GPC_MAX_IRQS		(IMR_NUM * 32)

#define GPU_VPU_PUP_REQ		BIT(1)
#define GPU_VPU_PDN_REQ		BIT(0)

#define GPC_CLK_MAX		6

struct pu_domain {
	struct generic_pm_domain base;
	struct regulator *reg;
	struct clk *clk[GPC_CLK_MAX];
	int num_clks;
};

static void __iomem *gpc_base;
static u32 gpc_wake_irqs[IMR_NUM];
static u32 gpc_saved_imrs[IMR_NUM];
static u32 gpc_mf_irqs[IMR_NUM];
static u32 gpc_mf_request_on[IMR_NUM];
static DEFINE_SPINLOCK(gpc_lock);

void imx_gpc_add_m4_wake_up_irq(u32 hwirq, bool enable)
{
	unsigned int idx = hwirq / 32;
	unsigned long flags;
	u32 mask;

	/* Sanity check for SPI irq */
	if (hwirq < 32)
		return;

	mask = 1 << hwirq % 32;
	spin_lock_irqsave(&gpc_lock, flags);
	gpc_wake_irqs[idx] = enable ? gpc_wake_irqs[idx] | mask :
		gpc_wake_irqs[idx] & ~mask;
	spin_unlock_irqrestore(&gpc_lock, flags);
}

void imx_gpc_hold_m4_in_sleep(void)
{
	int val;
	unsigned long timeout = jiffies + msecs_to_jiffies(500);

	/* wait M4 in wfi before asserting hold request */
	while (!imx_gpc_is_m4_sleeping())
		if (time_after(jiffies, timeout))
			pr_err("M4 is NOT in expected sleep!\n");

	val = readl_relaxed(gpc_base + GPC_M4_LPSR);
	val &= ~(GPC_M4_LPSR_M4_SLEEP_HOLD_REQ_MASK <<
		GPC_M4_LPSR_M4_SLEEP_HOLD_REQ_SHIFT);
	writel_relaxed(val, gpc_base + GPC_M4_LPSR);

	timeout = jiffies + msecs_to_jiffies(500);
	while (readl_relaxed(gpc_base + GPC_M4_LPSR)
		& (GPC_M4_LPSR_M4_SLEEP_HOLD_ACK_MASK <<
		GPC_M4_LPSR_M4_SLEEP_HOLD_ACK_SHIFT))
		if (time_after(jiffies, timeout))
			pr_err("Wait M4 hold ack timeout!\n");
}

void imx_gpc_release_m4_in_sleep(void)
{
	int val;

	val = readl_relaxed(gpc_base + GPC_M4_LPSR);
	val |= GPC_M4_LPSR_M4_SLEEP_HOLD_REQ_MASK <<
		GPC_M4_LPSR_M4_SLEEP_HOLD_REQ_SHIFT;
	writel_relaxed(val, gpc_base + GPC_M4_LPSR);
}

unsigned int imx_gpc_is_m4_sleeping(void)
{
	if (readl_relaxed(gpc_base + GPC_M4_LPSR) &
		(GPC_M4_LPSR_M4_SLEEPING_MASK <<
		GPC_M4_LPSR_M4_SLEEPING_SHIFT))
		return 1;

	return 0;
}

unsigned int imx_gpc_is_mf_mix_off(void)
{
	return readl_relaxed(gpc_base + GPC_PGC_MF_PDN);
}

static void imx_gpc_mf_mix_off(void)
{
	int i;

	for (i = 0; i < IMR_NUM; i++)
		if (((gpc_wake_irqs[i] | gpc_mf_request_on[i]) &
						gpc_mf_irqs[i]) != 0)
			return;

	pr_info("Turn off M/F mix!\n");
	/* turn off mega/fast mix */
	writel_relaxed(0x1, gpc_base + GPC_PGC_MF_PDN);
}

void imx_gpc_set_arm_power_up_timing(u32 sw2iso, u32 sw)
{
	writel_relaxed((sw2iso << GPC_PGC_SW2ISO_SHIFT) |
		(sw << GPC_PGC_SW_SHIFT), gpc_base + GPC_PGC_CPU_PUPSCR);
}

void imx_gpc_set_arm_power_down_timing(u32 sw2iso, u32 sw)
{
	writel_relaxed((sw2iso << GPC_PGC_SW2ISO_SHIFT) |
		(sw << GPC_PGC_SW_SHIFT), gpc_base + GPC_PGC_CPU_PDNSCR);
}

void imx_gpc_set_arm_power_in_lpm(bool power_off)
{
	writel_relaxed(power_off, gpc_base + GPC_PGC_CPU_PDN);
}

void imx_gpc_pre_suspend(bool arm_power_off)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	/* power down the mega-fast power domain */
	if ((cpu_is_imx6sx() || cpu_is_imx6ul()) && arm_power_off)
		imx_gpc_mf_mix_off();

	/* Tell GPC to power off ARM core when suspend */
	if (arm_power_off)
		imx_gpc_set_arm_power_in_lpm(arm_power_off);

	for (i = 0; i < IMR_NUM; i++) {
		gpc_saved_imrs[i] = readl_relaxed(reg_imr1 + i * 4);
		writel_relaxed(~gpc_wake_irqs[i], reg_imr1 + i * 4);
	}
}

void imx_gpc_post_resume(void)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	/* Keep ARM core powered on for other low-power modes */
	imx_gpc_set_arm_power_in_lpm(false);
	/* Keep M/F mix powered on for other low-power modes */
	if (cpu_is_imx6sx() || cpu_is_imx6ul())
		writel_relaxed(0x0, gpc_base + GPC_PGC_MF_PDN);

	for (i = 0; i < IMR_NUM; i++)
		writel_relaxed(gpc_saved_imrs[i], reg_imr1 + i * 4);
}

static int imx_gpc_irq_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned int idx = d->hwirq / 32;
	unsigned long flags;
	u32 mask;

	mask = 1 << d->hwirq % 32;
	spin_lock_irqsave(&gpc_lock, flags);
	gpc_wake_irqs[idx] = on ? gpc_wake_irqs[idx] | mask :
				  gpc_wake_irqs[idx] & ~mask;
	spin_unlock_irqrestore(&gpc_lock, flags);

	/*
	 * Do *not* call into the parent, as the GIC doesn't have any
	 * wake-up facility...
	 */
	return 0;
}

void imx_gpc_mask_all(void)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	for (i = 0; i < IMR_NUM; i++) {
		gpc_saved_imrs[i] = readl_relaxed(reg_imr1 + i * 4);
		writel_relaxed(~0, reg_imr1 + i * 4);
	}

}

void imx_gpc_restore_all(void)
{
	void __iomem *reg_imr1 = gpc_base + GPC_IMR1;
	int i;

	for (i = 0; i < IMR_NUM; i++)
		writel_relaxed(gpc_saved_imrs[i], reg_imr1 + i * 4);
}

void imx_gpc_hwirq_unmask(unsigned int hwirq)
{
	void __iomem *reg;
	u32 val;

	reg = gpc_base + GPC_IMR1 + hwirq / 32 * 4;
	val = readl_relaxed(reg);
	val &= ~(1 << hwirq % 32);
	writel_relaxed(val, reg);
}

void imx_gpc_hwirq_mask(unsigned int hwirq)
{
	void __iomem *reg;
	u32 val;

	reg = gpc_base + GPC_IMR1 + hwirq / 32 * 4;
	val = readl_relaxed(reg);
	val |= 1 << (hwirq % 32);
	writel_relaxed(val, reg);
}

static void imx_gpc_irq_unmask(struct irq_data *d)
{
	imx_gpc_hwirq_unmask(d->hwirq);
	irq_chip_unmask_parent(d);
}

static void imx_gpc_irq_mask(struct irq_data *d)
{
	imx_gpc_hwirq_mask(d->hwirq);
	irq_chip_mask_parent(d);
}

static struct irq_chip imx_gpc_chip = {
	.name			= "GPC",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= imx_gpc_irq_mask,
	.irq_unmask		= imx_gpc_irq_unmask,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_wake		= imx_gpc_irq_set_wake,
	.irq_set_type           = irq_chip_set_type_parent,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
};

static int imx_gpc_domain_translate(struct irq_domain *d,
				    struct irq_fwspec *fwspec,
				    unsigned long *hwirq,
				    unsigned int *type)
{
	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count != 3)
			return -EINVAL;

		/* No PPI should point to this domain */
		if (fwspec->param[0] != 0)
			return -EINVAL;

		*hwirq = fwspec->param[1];
		*type = fwspec->param[2];
		return 0;
	}

	return -EINVAL;
}

static int imx_gpc_domain_alloc(struct irq_domain *domain,
				  unsigned int irq,
				  unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	int i;

	if (fwspec->param_count != 3)
		return -EINVAL;	/* Not GIC compliant */
	if (fwspec->param[0] != 0)
		return -EINVAL;	/* No PPI should point to this domain */

	hwirq = fwspec->param[1];
	if (hwirq >= GPC_MAX_IRQS)
		return -EINVAL;	/* Can't deal with this */

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, irq + i, hwirq + i,
					      &imx_gpc_chip, NULL);

	parent_fwspec = *fwspec;
	parent_fwspec.fwnode = domain->parent->fwnode;
	return irq_domain_alloc_irqs_parent(domain, irq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops imx_gpc_domain_ops = {
	.translate	= imx_gpc_domain_translate,
	.alloc		= imx_gpc_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

int imx_gpc_mf_power_on(unsigned int irq, unsigned int on)
{
	struct irq_desc *d = irq_to_desc(irq);
	unsigned int idx = d->irq_data.hwirq / 32;
	unsigned long flags;
	u32 mask;

	mask = 1 << (d->irq_data.hwirq % 32);
	spin_lock_irqsave(&gpc_lock, flags);
	gpc_mf_request_on[idx] = on ? gpc_mf_request_on[idx] | mask :
				  gpc_mf_request_on[idx] & ~mask;
	spin_unlock_irqrestore(&gpc_lock, flags);

	return 0;
}

int imx_gpc_mf_request_on(unsigned int irq, unsigned int on)
{
	if (cpu_is_imx6sx() || cpu_is_imx6ul())
		return imx_gpc_mf_power_on(irq, on);
	else if (cpu_is_imx7d())
		return imx_gpcv2_mf_power_on(irq, on);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(imx_gpc_mf_request_on);

static int __init imx_gpc_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_domain *parent_domain, *domain;
	int i;

	if (!parent) {
		pr_err("%s: no parent, giving up\n", node->full_name);
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%s: unable to obtain parent domain\n", node->full_name);
		return -ENXIO;
	}

	gpc_base = of_iomap(node, 0);
	if (WARN_ON(!gpc_base))
	        return -ENOMEM;

	domain = irq_domain_add_hierarchy(parent_domain, 0, GPC_MAX_IRQS,
					  node, &imx_gpc_domain_ops,
					  NULL);
	if (!domain) {
		iounmap(gpc_base);
		return -ENOMEM;
	}

	/* Initially mask all interrupts */
	for (i = 0; i < IMR_NUM; i++)
		writel_relaxed(~0, gpc_base + GPC_IMR1 + i * 4);

	/* Read supported wakeup source in M/F domain */
	if (cpu_is_imx6sx() || cpu_is_imx6ul()) {
		of_property_read_u32_index(node, "fsl,mf-mix-wakeup-irq", 0,
			&gpc_mf_irqs[0]);
		of_property_read_u32_index(node, "fsl,mf-mix-wakeup-irq", 1,
			&gpc_mf_irqs[1]);
		of_property_read_u32_index(node, "fsl,mf-mix-wakeup-irq", 2,
			&gpc_mf_irqs[2]);
		of_property_read_u32_index(node, "fsl,mf-mix-wakeup-irq", 3,
			&gpc_mf_irqs[3]);
		if (!(gpc_mf_irqs[0] | gpc_mf_irqs[1] |
			gpc_mf_irqs[2] | gpc_mf_irqs[3]))
			pr_info("No wakeup source in Mega/Fast domain found!\n");
	}

	/*
	 * Clear the OF_POPULATED flag set in of_irq_init so that
	 * later the GPC power domain driver will not be skipped.
	 */
	of_node_clear_flag(node, OF_POPULATED);

	return 0;
}
IRQCHIP_DECLARE(imx_gpc, "fsl,imx6q-gpc", imx_gpc_init);

void __init imx_gpc_check_dt(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-gpc");
	if (WARN_ON(!np))
		return;

	if (WARN_ON(!of_find_property(np, "interrupt-controller", NULL))) {
		pr_warn("Outdated DT detected, suspend/resume will NOT work\n");

		/* map GPC, so that at least CPUidle and WARs keep working */
		gpc_base = of_iomap(np, 0);
	}
}

static void _imx6q_pm_pu_power_off(struct generic_pm_domain *genpd)
{
	int iso, iso2sw;
	u32 val;

	/* Read ISO and ISO2SW power down delays */
	val = readl_relaxed(gpc_base + GPC_PGC_GPU_PDNSCR);
	iso = val & 0x3f;
	iso2sw = (val >> 8) & 0x3f;

	/* Gate off PU domain when GPU/VPU when powered down */
	writel_relaxed(0x1, gpc_base + GPC_PGC_GPU_PDN);

	/* Request GPC to power down GPU/VPU */
	val = readl_relaxed(gpc_base + GPC_CNTR);
	val |= GPU_VPU_PDN_REQ;
	writel_relaxed(val, gpc_base + GPC_CNTR);

	/* Wait ISO + ISO2SW IPG clock cycles */
	ndelay((iso + iso2sw) * 1000 / 66);
}

static int imx6q_pm_pu_power_off(struct generic_pm_domain *genpd)
{
	struct pu_domain *pu = container_of(genpd, struct pu_domain, base);

	_imx6q_pm_pu_power_off(genpd);

	if (pu->reg)
		regulator_disable(pu->reg);

	return 0;
}

static int imx6q_pm_pu_power_on(struct generic_pm_domain *genpd)
{
	struct pu_domain *pu = container_of(genpd, struct pu_domain, base);
	int i, ret, sw, sw2iso;
	u32 val;

	if (pu->reg)
		ret = regulator_enable(pu->reg);
	if (pu->reg && ret) {
		pr_err("%s: failed to enable regulator: %d\n", __func__, ret);
		return ret;
	}

	/* Enable reset clocks for all devices in the PU domain */
	for (i = 0; i < pu->num_clks; i++)
		clk_prepare_enable(pu->clk[i]);

	/* Gate off PU domain when GPU/VPU when powered down */
	writel_relaxed(0x1, gpc_base + GPC_PGC_GPU_PDN);

	/* Read ISO and ISO2SW power down delays */
	val = readl_relaxed(gpc_base + GPC_PGC_GPU_PUPSCR);
	sw = val & 0x3f;
	sw2iso = (val >> 8) & 0x3f;

	/* Request GPC to power up GPU/VPU */
	val = readl_relaxed(gpc_base + GPC_CNTR);
	val |= GPU_VPU_PUP_REQ;
	writel_relaxed(val, gpc_base + GPC_CNTR);

	/* Wait ISO + ISO2SW IPG clock cycles */
	ndelay((sw + sw2iso) * 1000 / 66);

	/* Disable reset clocks for all devices in the PU domain */
	for (i = 0; i < pu->num_clks; i++)
		clk_disable_unprepare(pu->clk[i]);

	return 0;
}

static struct generic_pm_domain imx6q_arm_domain = {
	.name = "ARM",
};

static struct pu_domain imx6q_pu_domain = {
	.base = {
		.name = "PU",
		.power_off = imx6q_pm_pu_power_off,
		.power_on = imx6q_pm_pu_power_on,
		.states = {
			[0] = {
				.power_off_latency_ns = 25000,
				.power_on_latency_ns = 2000000,
			},
		},
		.state_count = 1,
	},
};

static struct generic_pm_domain imx6sl_display_domain = {
	.name = "DISPLAY",
};

static struct generic_pm_domain *imx_gpc_domains[] = {
	&imx6q_arm_domain,
	&imx6q_pu_domain.base,
	&imx6sl_display_domain,
};

static struct genpd_onecell_data imx_gpc_onecell_data = {
	.domains = imx_gpc_domains,
	.num_domains = ARRAY_SIZE(imx_gpc_domains),
};

static int imx_gpc_genpd_init(struct device *dev, struct regulator *pu_reg)
{
	struct clk *clk;
	int i, ret;

	imx6q_pu_domain.reg = pu_reg;

	for (i = 0; ; i++) {
		clk = of_clk_get(dev->of_node, i);
		if (IS_ERR(clk))
			break;
		if (i >= GPC_CLK_MAX) {
			dev_err(dev, "more than %d clocks\n", GPC_CLK_MAX);
			goto clk_err;
		}
		imx6q_pu_domain.clk[i] = clk;
	}
	imx6q_pu_domain.num_clks = i;

	/* Enable power always in case bootloader disabled it. */
	imx6q_pm_pu_power_on(&imx6q_pu_domain.base);

	if (!IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS))
		return 0;

	for (i = 0; i < ARRAY_SIZE(imx_gpc_domains); i++)
		pm_genpd_init(imx_gpc_domains[i], NULL, false);

	ret =  of_genpd_add_provider_onecell(dev->of_node,
					     &imx_gpc_onecell_data);
	if (ret)
		goto power_off;

	return 0;

power_off:
	imx6q_pm_pu_power_off(&imx6q_pu_domain.base);
clk_err:
	while (i--)
		clk_put(imx6q_pu_domain.clk[i]);
	imx6q_pu_domain.reg = NULL;
	return -EINVAL;
}

static int imx_gpc_probe(struct platform_device *pdev)
{
	struct regulator *pu_reg;
	int ret;

	/* bail out if DT too old and doesn't provide the necessary info */
	if (!of_property_read_bool(pdev->dev.of_node, "#power-domain-cells"))
		return 0;

	pu_reg = devm_regulator_get_optional(&pdev->dev, "pu");
	if (PTR_ERR(pu_reg) == -ENODEV)
		pu_reg = NULL;
	if (IS_ERR(pu_reg)) {
		ret = PTR_ERR(pu_reg);
		dev_err(&pdev->dev, "failed to get pu regulator: %d\n", ret);
		return ret;
	}

	return imx_gpc_genpd_init(&pdev->dev, pu_reg);
}

static const struct of_device_id imx_gpc_dt_ids[] = {
	{ .compatible = "fsl,imx6q-gpc" },
	{ .compatible = "fsl,imx6sl-gpc" },
	{ }
};

static struct platform_driver imx_gpc_driver = {
	.driver = {
		.name = "imx-gpc",
		.of_match_table = imx_gpc_dt_ids,
	},
	.probe = imx_gpc_probe,
};

static int __init imx_pgc_init(void)
{
	return platform_driver_register(&imx_gpc_driver);
}
subsys_initcall(imx_pgc_init);
