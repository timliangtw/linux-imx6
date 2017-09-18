/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/io.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <video/dpu.h>
#include "dpu-prv.h"

#define SSQCNTS			0
#define SSQCYCLE		0x8
#define SWRESET			0xC
#define TCON_CTRL		0x10
#define BYPASS			BIT(3)
#define RSDSINVCTRL		0x14
#define MAPBIT3_0		0x18
#define MAPBIT7_4		0x1C
#define MAPBIT11_8		0x20
#define MAPBIT15_12		0x24
#define MAPBIT19_16		0x28
#define MAPBIT23_20		0x2C
#define MAPBIT27_24		0x30
#define MAPBIT31_28		0x34
#define MAPBIT34_32		0x38
#define MAPBIT3_0_DUAL		0x3C
#define MAPBIT7_4_DUAL		0x40
#define MAPBIT11_8_DUAL		0x44
#define MAPBIT15_12_DUAL	0x48
#define MAPBIT19_16_DUAL	0x4C
#define MAPBIT23_20_DUAL	0x50
#define MAPBIT27_24_DUAL	0x54
#define MAPBIT31_28_DUAL	0x58
#define MAPBIT34_32_DUAL	0x5C
#define SPGPOSON(n)		(0x60 + (n) * 16)
#define X(n)			(((n) & 0x7FFF) << 16)
#define Y(n)			((n) & 0x7FFF)
#define SPGMASKON(n)		(0x64 + (n) * 16)
#define SPGPOSOFF(n)		(0x68 + (n) * 16)
#define SPGMASKOFF(n)		(0x6C + (n) * 16)
#define SMXSIGS(n)		(0x120 + (n) * 8)
#define SMXFCTTABLE(n)		(0x124 + (n) * 8)
#define RESET_OVER_UNFERFLOW	0x180
#define DUAL_DEBUG		0x184

struct dpu_tcon {
	void __iomem *base;
	struct mutex mutex;
	int id;
	bool inuse;
	struct dpu_soc *dpu;
};

static inline u32 dpu_tcon_read(struct dpu_tcon *tcon, unsigned int offset)
{
	return readl(tcon->base + offset);
}

static inline void dpu_tcon_write(struct dpu_tcon *tcon, u32 value,
				  unsigned int offset)
{
	writel(value, tcon->base + offset);
}

int tcon_set_fmt(struct dpu_tcon *tcon, u32 bus_format)
{
	mutex_lock(&tcon->mutex);
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		dpu_tcon_write(tcon, 0x19181716, MAPBIT3_0);
		dpu_tcon_write(tcon, 0x1d1c1b1a, MAPBIT7_4);
		dpu_tcon_write(tcon, 0x0f0e0d0c, MAPBIT11_8);
		dpu_tcon_write(tcon, 0x13121110, MAPBIT15_12);
		dpu_tcon_write(tcon, 0x05040302, MAPBIT19_16);
		dpu_tcon_write(tcon, 0x09080706, MAPBIT23_20);
		break;
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB888_1X30_PADLO:
	case MEDIA_BUS_FMT_RGB666_1X30_PADLO:
		dpu_tcon_write(tcon, 0x17161514, MAPBIT3_0);
		dpu_tcon_write(tcon, 0x1b1a1918, MAPBIT7_4);
		dpu_tcon_write(tcon, 0x0b0a1d1c, MAPBIT11_8);
		dpu_tcon_write(tcon, 0x0f0e0d0c, MAPBIT15_12);
		dpu_tcon_write(tcon, 0x13121110, MAPBIT19_16);
		dpu_tcon_write(tcon, 0x03020100, MAPBIT23_20);
		dpu_tcon_write(tcon, 0x07060504, MAPBIT27_24);
		dpu_tcon_write(tcon, 0x00000908, MAPBIT31_28);
		break;
	default:
		mutex_unlock(&tcon->mutex);
		return -EINVAL;
	}
	mutex_unlock(&tcon->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(tcon_set_fmt);

/* This function is used to workaround TKT320590 which is related to DPR/PRG. */
void tcon_set_operation_mode(struct dpu_tcon *tcon)
{
	u32 val;

	mutex_lock(&tcon->mutex);
	val = dpu_tcon_read(tcon, TCON_CTRL);
	val &= ~BYPASS;
	dpu_tcon_write(tcon, val, TCON_CTRL);
	mutex_unlock(&tcon->mutex);
}
EXPORT_SYMBOL_GPL(tcon_set_operation_mode);

void tcon_cfg_videomode(struct dpu_tcon *tcon,
			struct drm_display_mode *m, bool side_by_side)
{
	struct drm_display_mode tmp_m;
	struct dpu_soc *dpu = tcon->dpu;
	const struct dpu_devtype *devtype = dpu->devtype;
	u32 val;
	int y;

	drm_mode_copy(&tmp_m, m);

	if (side_by_side) {
		tmp_m.hdisplay /= 2;
		tmp_m.hsync_start /= 2;
		tmp_m.hsync_end /= 2;
		tmp_m.htotal /= 2;
	}

	mutex_lock(&tcon->mutex);
	/*
	 * TKT320590:
	 * Turn TCON into operation mode later after the first dumb frame is
	 * generated by DPU.  This makes DPR/PRG be able to evade the frame.
	 */
	val = dpu_tcon_read(tcon, TCON_CTRL);
	val |= BYPASS;
	dpu_tcon_write(tcon, val, TCON_CTRL);

	/* dsp_control[0]: hsync */
	dpu_tcon_write(tcon, X(tmp_m.hsync_start), SPGPOSON(0));
	dpu_tcon_write(tcon, 0xffff, SPGMASKON(0));

	dpu_tcon_write(tcon, X(tmp_m.hsync_end), SPGPOSOFF(0));
	dpu_tcon_write(tcon, 0xffff, SPGMASKOFF(0));

	dpu_tcon_write(tcon, 0x2, SMXSIGS(0));
	dpu_tcon_write(tcon, 0x1, SMXFCTTABLE(0));

	/* dsp_control[1]: vsync */
	dpu_tcon_write(tcon, X(tmp_m.hsync_start) | Y(m->vsync_start - 1),
								SPGPOSON(1));
	dpu_tcon_write(tcon, 0x0, SPGMASKON(1));

	dpu_tcon_write(tcon, X(tmp_m.hsync_start) | Y(m->vsync_end - 1),
								SPGPOSOFF(1));
	dpu_tcon_write(tcon, 0x0, SPGMASKOFF(1));

	dpu_tcon_write(tcon, 0x3, SMXSIGS(1));
	dpu_tcon_write(tcon, 0x1, SMXFCTTABLE(1));

	/* dsp_control[2]: data enable */
	/* horizontal */
	dpu_tcon_write(tcon, 0x0, SPGPOSON(2));
	dpu_tcon_write(tcon, 0xffff, SPGMASKON(2));

	dpu_tcon_write(tcon, X(tmp_m.hdisplay), SPGPOSOFF(2));
	dpu_tcon_write(tcon, 0xffff, SPGMASKOFF(2));

	/* vertical */
	dpu_tcon_write(tcon, 0x0, SPGPOSON(3));
	dpu_tcon_write(tcon, 0x7fff0000, SPGMASKON(3));

	dpu_tcon_write(tcon, Y(m->vdisplay), SPGPOSOFF(3));
	dpu_tcon_write(tcon, 0x7fff0000, SPGMASKOFF(3));

	dpu_tcon_write(tcon, 0x2c, SMXSIGS(2));
	dpu_tcon_write(tcon, 0x8, SMXFCTTABLE(2));

	/* dsp_control[3]: kachuck */
	y = m->vdisplay;
	/*
	 * If sync mode fixup is present, the kachuck signal from slave tcon
	 * should be one line later than the one from master tcon.
	 */
	if (side_by_side && tcon_is_slave(tcon) && devtype->has_syncmode_fixup)
		y++;
	dpu_tcon_write(tcon, X(0xa) | Y(y), SPGPOSON(4));
	dpu_tcon_write(tcon, 0x0, SPGMASKON(4));

	dpu_tcon_write(tcon, X(0x2a) | Y(y), SPGPOSOFF(4));
	dpu_tcon_write(tcon, 0x0, SPGMASKOFF(4));

	dpu_tcon_write(tcon, 0x6, SMXSIGS(3));
	dpu_tcon_write(tcon, 0x2, SMXFCTTABLE(3));
	mutex_unlock(&tcon->mutex);
}
EXPORT_SYMBOL_GPL(tcon_cfg_videomode);

bool tcon_is_master(struct dpu_tcon *tcon)
{
	return tcon->id == 0;
}
EXPORT_SYMBOL_GPL(tcon_is_master);

bool tcon_is_slave(struct dpu_tcon *tcon)
{
	return tcon->id == 1;
}
EXPORT_SYMBOL_GPL(tcon_is_slave);

struct dpu_tcon *dpu_tcon_get(struct dpu_soc *dpu, int id)
{
	struct dpu_tcon *tcon;
	int i;

	for (i = 0; i < ARRAY_SIZE(tcon_ids); i++)
		if (tcon_ids[i] == id)
			break;

	if (i == ARRAY_SIZE(tcon_ids))
		return ERR_PTR(-EINVAL);

	tcon = dpu->tcon_priv[i];

	mutex_lock(&tcon->mutex);

	if (tcon->inuse) {
		mutex_unlock(&tcon->mutex);
		return ERR_PTR(-EBUSY);
	}

	tcon->inuse = true;

	mutex_unlock(&tcon->mutex);

	return tcon;
}
EXPORT_SYMBOL_GPL(dpu_tcon_get);

void dpu_tcon_put(struct dpu_tcon *tcon)
{
	mutex_lock(&tcon->mutex);

	tcon->inuse = false;

	mutex_unlock(&tcon->mutex);
}
EXPORT_SYMBOL_GPL(dpu_tcon_put);

struct dpu_tcon *dpu_aux_tcon_peek(struct dpu_tcon *tcon)
{
	return tcon->dpu->tcon_priv[tcon->id ^ 1];
}
EXPORT_SYMBOL_GPL(dpu_aux_tcon_peek);

void _dpu_tcon_init(struct dpu_soc *dpu, unsigned int id)
{
}

int dpu_tcon_init(struct dpu_soc *dpu, unsigned int id,
			unsigned long unused, unsigned long base)
{
	struct dpu_tcon *tcon;

	tcon = devm_kzalloc(dpu->dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		return -ENOMEM;

	dpu->tcon_priv[id] = tcon;

	tcon->base = devm_ioremap(dpu->dev, base, SZ_512);
	if (!tcon->base)
		return -ENOMEM;

	tcon->dpu = dpu;
	mutex_init(&tcon->mutex);

	return 0;
}
