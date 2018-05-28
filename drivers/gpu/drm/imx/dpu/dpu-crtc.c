/*
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

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <video/dpu.h>
#include "dpu-crtc.h"
#include "dpu-kms.h"
#include "dpu-plane.h"
#include "imx-drm.h"

static inline struct dpu_plane_state **
alloc_dpu_plane_states(struct dpu_crtc *dpu_crtc)
{
	struct dpu_plane_state **states;

	states = kcalloc(dpu_crtc->hw_plane_num, sizeof(*states), GFP_KERNEL);
	if (!states)
		return ERR_PTR(-ENOMEM);

	return states;
}

struct dpu_plane_state **
crtc_state_get_dpu_plane_states(struct drm_crtc_state *state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(state);
	struct dpu_crtc_state *dcstate = to_dpu_crtc_state(imx_crtc_state);

	return dcstate->dpu_plane_states;
}

static void dpu_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct dpu_plane *dplane = to_dpu_plane(crtc->primary);
	struct dpu_plane_res *res = &dplane->grp->res;
	struct dpu_extdst *plane_ed = res->ed[dplane->stream_id];
	unsigned long ret;

	drm_crtc_vblank_on(crtc);

	enable_irq(dpu_crtc->safety_shdld_irq);
	enable_irq(dpu_crtc->content_shdld_irq);
	enable_irq(dpu_crtc->dec_shdld_irq);

	framegen_enable_clock(dpu_crtc->fg);
	extdst_pixengcfg_sync_trigger(plane_ed);
	extdst_pixengcfg_sync_trigger(dpu_crtc->ed);
	framegen_shdtokgen(dpu_crtc->fg);
	framegen_enable(dpu_crtc->fg);

	ret = wait_for_completion_timeout(&dpu_crtc->safety_shdld_done, HZ);
	if (ret == 0)
		dev_warn(dpu_crtc->dev,
			 "enable - wait for safety shdld done timeout\n");
	ret = wait_for_completion_timeout(&dpu_crtc->content_shdld_done, HZ);
	if (ret == 0)
		dev_warn(dpu_crtc->dev,
			 "enable - wait for content shdld done timeout\n");
	ret = wait_for_completion_timeout(&dpu_crtc->dec_shdld_done, HZ);
	if (ret == 0)
		dev_warn(dpu_crtc->dev,
			 "enable - wait for DEC shdld done timeout\n");

	disable_irq(dpu_crtc->safety_shdld_irq);
	disable_irq(dpu_crtc->content_shdld_irq);
	disable_irq(dpu_crtc->dec_shdld_irq);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}

	/*
	 * TKT320590:
	 * Turn TCON into operation mode later after the first dumb frame is
	 * generated by DPU.  This makes DPR/PRG be able to evade the frame.
	 */
	framegen_wait_for_frame_counter_moving(dpu_crtc->fg);
	tcon_set_operation_mode(dpu_crtc->tcon);
}

static void dpu_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	framegen_disable(dpu_crtc->fg);
	framegen_wait_done(dpu_crtc->fg, &old_crtc_state->adjusted_mode);
	framegen_disable_clock(dpu_crtc->fg);

	WARN_ON(!crtc->state->event);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}

	drm_crtc_vblank_off(crtc);
}

static void dpu_drm_crtc_reset(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct imx_crtc_state *imx_crtc_state;
	struct dpu_crtc_state *state;

	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

		imx_crtc_state = to_imx_crtc_state(crtc->state);
		state = to_dpu_crtc_state(imx_crtc_state);
		kfree(state->dpu_plane_states);
		kfree(state);
		crtc->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		crtc->state = &state->imx_crtc_state.base;
		crtc->state->crtc = crtc;

		state->dpu_plane_states = alloc_dpu_plane_states(dpu_crtc);
		if (IS_ERR(state->dpu_plane_states))
			kfree(state);
	}
}

static struct drm_crtc_state *
dpu_drm_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct dpu_crtc_state *state;

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->dpu_plane_states = alloc_dpu_plane_states(dpu_crtc);
	if (IS_ERR(state->dpu_plane_states)) {
		kfree(state);
		return NULL;
	}

	__drm_atomic_helper_crtc_duplicate_state(crtc,
					&state->imx_crtc_state.base);

	return &state->imx_crtc_state.base;
}

static void dpu_drm_crtc_destroy_state(struct drm_crtc *crtc,
				       struct drm_crtc_state *state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(state);
	struct dpu_crtc_state *dcstate;

	if (state) {
		__drm_atomic_helper_crtc_destroy_state(state);
		dcstate = to_dpu_crtc_state(imx_crtc_state);
		kfree(dcstate->dpu_plane_states);
		kfree(dcstate);
	}
}

static int dpu_enable_vblank(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	enable_irq(dpu_crtc->vbl_irq);

	return 0;
}

static void dpu_disable_vblank(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	disable_irq_nosync(dpu_crtc->vbl_irq);
}

static const struct drm_crtc_funcs dpu_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = dpu_drm_crtc_reset,
	.atomic_duplicate_state = dpu_drm_crtc_duplicate_state,
	.atomic_destroy_state = dpu_drm_crtc_destroy_state,
	.enable_vblank = dpu_enable_vblank,
	.disable_vblank = dpu_disable_vblank,
};

static irqreturn_t dpu_vbl_irq_handler(int irq, void *dev_id)
{
	struct dpu_crtc *dpu_crtc = dev_id;

	drm_crtc_handle_vblank(&dpu_crtc->base);

	return IRQ_HANDLED;
}

static irqreturn_t dpu_safety_shdld_irq_handler(int irq, void *dev_id)
{
	struct dpu_crtc *dpu_crtc = dev_id;

	complete(&dpu_crtc->safety_shdld_done);

	return IRQ_HANDLED;
}

static irqreturn_t dpu_content_shdld_irq_handler(int irq, void *dev_id)
{
	struct dpu_crtc *dpu_crtc = dev_id;

	complete(&dpu_crtc->content_shdld_done);

	return IRQ_HANDLED;
}

static irqreturn_t dpu_dec_shdld_irq_handler(int irq, void *dev_id)
{
	struct dpu_crtc *dpu_crtc = dev_id;

	complete(&dpu_crtc->dec_shdld_done);

	return IRQ_HANDLED;
}

static int dpu_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_crtc_state *crtc_state)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct dpu_plane_state *dpstate;
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);
	struct dpu_crtc_state *dcstate = to_dpu_crtc_state(imx_crtc_state);
	int i = 0;

	/*
	 * cache the plane states so that the planes can be disabled in
	 * ->atomic_begin.
	 */
	drm_for_each_plane_mask(plane, crtc->dev, crtc_state->plane_mask) {
		plane_state =
			drm_atomic_get_plane_state(crtc_state->state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);

		dpstate = to_dpu_plane_state(plane_state);
		dcstate->dpu_plane_states[i++] = dpstate;
	}

	return 0;
}

static void dpu_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct imx_crtc_state *imx_crtc_state =
					to_imx_crtc_state(old_crtc_state);
	struct dpu_crtc_state *old_dcstate = to_dpu_crtc_state(imx_crtc_state);
	int i;

	/*
	 * Disable all planes' resources in SHADOW only.
	 * Whether any of them would be disabled or kept running depends
	 * on new plane states' commit.
	 */
	for (i = 0; i < dpu_crtc->hw_plane_num; i++) {
		struct dpu_plane_state *old_dpstate;
		struct drm_plane_state *plane_state;
		struct dpu_plane *dplane;
		struct drm_plane *plane;
		struct dpu_plane_res *res;
		struct dpu_fetchunit *fu;
		struct dpu_fetchunit *fe = NULL;
		struct dpu_hscaler *hs = NULL;
		struct dpu_vscaler *vs = NULL;
		struct dpu_layerblend *lb;
		struct dpu_extdst *ed;
		extdst_src_sel_t ed_src;
		int lb_id;
		bool crtc_disabling_on_primary = false;

		old_dpstate = old_dcstate->dpu_plane_states[i];
		if (!old_dpstate)
			continue;

		plane_state = &old_dpstate->base;
		dplane = to_dpu_plane(plane_state->plane);
		res = &dplane->grp->res;

		fu = dpstate_to_fu(old_dpstate);
		if (!fu)
			return;

		lb_id = blend_to_id(old_dpstate->blend);
		if (lb_id < 0)
			return;

		lb = res->lb[lb_id];

		layerblend_pixengcfg_clken(lb, CLKEN__DISABLE);
		if (fetchunit_is_fetchdecode(fu)) {
			fe = fetchdecode_get_fetcheco(fu);
			hs = fetchdecode_get_hscaler(fu);
			vs = fetchdecode_get_vscaler(fu);
			hscaler_pixengcfg_clken(hs, CLKEN__DISABLE);
			vscaler_pixengcfg_clken(vs, CLKEN__DISABLE);
			hscaler_mode(hs, SCALER_NEUTRAL);
			vscaler_mode(vs, SCALER_NEUTRAL);
		}
		if (old_dpstate->is_top) {
			ed = res->ed[dplane->stream_id];
			ed_src = dplane->stream_id ?
				ED_SRC_CONSTFRAME1 : ED_SRC_CONSTFRAME0;
			extdst_pixengcfg_src_sel(ed, ed_src);
		}

		plane = old_dpstate->base.plane;
		if (!crtc->state->enable &&
		    plane->type == DRM_PLANE_TYPE_PRIMARY)
			crtc_disabling_on_primary = true;

		if (crtc_disabling_on_primary && old_dpstate->use_prefetch) {
			fu->ops->pin_off(fu);
			if (fetchunit_is_fetchdecode(fu) &&
			    fe->ops->is_enabled(fe))
				fe->ops->pin_off(fe);
		} else {
			fu->ops->disable_src_buf(fu);
			fu->ops->unpin_off(fu);
			if (fetchunit_is_fetchdecode(fu)) {
				fetchdecode_pixengcfg_dynamic_src_sel(fu,
								FD_SRC_DISABLE);
				fe->ops->disable_src_buf(fe);
				fe->ops->unpin_off(fe);
			}
		}
	}
}

static void dpu_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct imx_crtc_state *imx_crtc_state =
					to_imx_crtc_state(old_crtc_state);
	struct dpu_crtc_state *old_dcstate = to_dpu_crtc_state(imx_crtc_state);
	struct dpu_plane *dplane = to_dpu_plane(crtc->primary);
	struct dpu_plane_res *res = &dplane->grp->res;
	struct dpu_extdst *ed = res->ed[dplane->stream_id];
	unsigned long ret;
	int i;
	bool need_modeset = drm_atomic_crtc_needs_modeset(crtc->state);

	if (!crtc->state->active && !old_crtc_state->active)
		return;

	if (!need_modeset) {
		enable_irq(dpu_crtc->content_shdld_irq);

		extdst_pixengcfg_sync_trigger(ed);

		ret = wait_for_completion_timeout(&dpu_crtc->content_shdld_done,
						  HZ);
		if (ret == 0)
			dev_warn(dpu_crtc->dev,
			      "flush - wait for content shdld done timeout\n");

		disable_irq(dpu_crtc->content_shdld_irq);

		WARN_ON(!crtc->state->event);

		if (crtc->state->event) {
			spin_lock_irq(&crtc->dev->event_lock);
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
			spin_unlock_irq(&crtc->dev->event_lock);

			crtc->state->event = NULL;
		}
	} else if (!crtc->state->active) {
		extdst_pixengcfg_sync_trigger(ed);
	}

	for (i = 0; i < dpu_crtc->hw_plane_num; i++) {
		struct dpu_plane_state *old_dpstate;
		struct dpu_fetchunit *fu;
		struct dpu_fetchunit *fe;
		struct dpu_hscaler *hs;
		struct dpu_vscaler *vs;

		old_dpstate = old_dcstate->dpu_plane_states[i];
		if (!old_dpstate)
			continue;

		fu = dpstate_to_fu(old_dpstate);
		if (!fu)
			return;

		if (!fu->ops->is_enabled(fu) || fu->ops->is_pinned_off(fu))
			fu->ops->set_stream_id(fu, DPU_PLANE_SRC_DISABLED);

		if (fetchunit_is_fetchdecode(fu)) {
			fe = fetchdecode_get_fetcheco(fu);
			if (!fe->ops->is_enabled(fe) ||
			     fe->ops->is_pinned_off(fe))
				fe->ops->set_stream_id(fe,
							DPU_PLANE_SRC_DISABLED);

			hs = fetchdecode_get_hscaler(fu);
			if (!hscaler_is_enabled(hs))
				hscaler_set_stream_id(hs,
							DPU_PLANE_SRC_DISABLED);

			vs = fetchdecode_get_vscaler(fu);
			if (!vscaler_is_enabled(vs))
				vscaler_set_stream_id(vs,
							DPU_PLANE_SRC_DISABLED);
		}
	}
}

static void dpu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc->state);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_encoder *encoder;
	struct dpu_plane *dplane = to_dpu_plane(crtc->primary);
	struct dpu_plane_res *res = &dplane->grp->res;
	struct dpu_extdst *plane_ed = res->ed[dplane->stream_id];
	extdst_src_sel_t ed_src;
	unsigned long encoder_types = 0;
	u32 encoder_mask;
	bool encoder_type_has_tmds = false;
	bool encoder_type_has_lvds = false;

	dev_dbg(dpu_crtc->dev, "%s: mode->hdisplay: %d\n", __func__,
			mode->hdisplay);
	dev_dbg(dpu_crtc->dev, "%s: mode->vdisplay: %d\n", __func__,
			mode->vdisplay);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		encoder_mask = 1 << drm_encoder_index(encoder);

		if (!(crtc->state->encoder_mask & encoder_mask))
			continue;

		encoder_types |= BIT(encoder->encoder_type);
	}

	if (encoder_types & BIT(DRM_MODE_ENCODER_TMDS)) {
		encoder_type_has_tmds = true;
		dev_dbg(dpu_crtc->dev, "%s: encoder type has TMDS\n", __func__);
	}

	if (encoder_types & BIT(DRM_MODE_ENCODER_LVDS)) {
		encoder_type_has_lvds = true;
		dev_dbg(dpu_crtc->dev, "%s: encoder type has LVDS\n", __func__);
	}

	framegen_cfg_videomode(dpu_crtc->fg, mode, false,
			encoder_type_has_tmds, encoder_type_has_lvds);
	framegen_displaymode(dpu_crtc->fg, FGDM__SEC_ON_TOP);

	framegen_panic_displaymode(dpu_crtc->fg, FGDM__TEST);

	tcon_cfg_videomode(dpu_crtc->tcon, mode, false);
	tcon_set_fmt(dpu_crtc->tcon, imx_crtc_state->bus_format);

	disengcfg_polarity_ctrl(dpu_crtc->dec, mode->flags);

	constframe_framedimensions(dpu_crtc->cf,
					mode->crtc_hdisplay,
					mode->crtc_vdisplay);

	ed_src = dpu_crtc->stream_id ? ED_SRC_CONSTFRAME5 : ED_SRC_CONSTFRAME4;
	extdst_pixengcfg_src_sel(dpu_crtc->ed, ed_src);

	ed_src = dpu_crtc->stream_id ? ED_SRC_CONSTFRAME1 : ED_SRC_CONSTFRAME0;
	extdst_pixengcfg_src_sel(plane_ed, ed_src);
}

static const struct drm_crtc_helper_funcs dpu_helper_funcs = {
	.mode_set_nofb = dpu_crtc_mode_set_nofb,
	.atomic_check = dpu_crtc_atomic_check,
	.atomic_begin = dpu_crtc_atomic_begin,
	.atomic_flush = dpu_crtc_atomic_flush,
	.atomic_enable = dpu_crtc_atomic_enable,
	.atomic_disable = dpu_crtc_atomic_disable,
};

static void dpu_crtc_put_resources(struct dpu_crtc *dpu_crtc)
{
	if (!IS_ERR_OR_NULL(dpu_crtc->cf))
		dpu_cf_put(dpu_crtc->cf);
	if (!IS_ERR_OR_NULL(dpu_crtc->dec))
		dpu_dec_put(dpu_crtc->dec);
	if (!IS_ERR_OR_NULL(dpu_crtc->ed))
		dpu_ed_put(dpu_crtc->ed);
	if (!IS_ERR_OR_NULL(dpu_crtc->fg))
		dpu_fg_put(dpu_crtc->fg);
	if (!IS_ERR_OR_NULL(dpu_crtc->tcon))
		dpu_tcon_put(dpu_crtc->tcon);
}

static int dpu_crtc_get_resources(struct dpu_crtc *dpu_crtc)
{
	struct dpu_soc *dpu = dev_get_drvdata(dpu_crtc->dev->parent);
	unsigned int stream_id = dpu_crtc->stream_id;
	int ret;

	dpu_crtc->cf = dpu_cf_get(dpu, stream_id + 4);
	if (IS_ERR(dpu_crtc->cf)) {
		ret = PTR_ERR(dpu_crtc->cf);
		goto err_out;
	}
	dpu_crtc->aux_cf = dpu_aux_cf_peek(dpu_crtc->cf);

	dpu_crtc->dec = dpu_dec_get(dpu, stream_id);
	if (IS_ERR(dpu_crtc->dec)) {
		ret = PTR_ERR(dpu_crtc->dec);
		goto err_out;
	}
	dpu_crtc->aux_dec = dpu_aux_dec_peek(dpu_crtc->dec);

	dpu_crtc->ed = dpu_ed_get(dpu, stream_id + 4);
	if (IS_ERR(dpu_crtc->ed)) {
		ret = PTR_ERR(dpu_crtc->ed);
		goto err_out;
	}
	dpu_crtc->aux_ed = dpu_aux_ed_peek(dpu_crtc->ed);

	dpu_crtc->fg = dpu_fg_get(dpu, stream_id);
	if (IS_ERR(dpu_crtc->fg)) {
		ret = PTR_ERR(dpu_crtc->fg);
		goto err_out;
	}
	dpu_crtc->aux_fg = dpu_aux_fg_peek(dpu_crtc->fg);

	dpu_crtc->tcon = dpu_tcon_get(dpu, stream_id);
	if (IS_ERR(dpu_crtc->tcon)) {
		ret = PTR_ERR(dpu_crtc->tcon);
		goto err_out;
	}
	dpu_crtc->aux_tcon = dpu_aux_tcon_peek(dpu_crtc->tcon);

	return 0;
err_out:
	dpu_crtc_put_resources(dpu_crtc);

	return ret;
}

static int dpu_crtc_init(struct dpu_crtc *dpu_crtc,
	struct dpu_client_platformdata *pdata, struct drm_device *drm)
{
	struct dpu_soc *dpu = dev_get_drvdata(dpu_crtc->dev->parent);
	struct device *dev = dpu_crtc->dev;
	struct drm_crtc *crtc = &dpu_crtc->base;
	struct dpu_plane_grp *plane_grp = pdata->plane_grp;
	unsigned int stream_id = pdata->stream_id;
	int i, ret;

	init_completion(&dpu_crtc->safety_shdld_done);
	init_completion(&dpu_crtc->content_shdld_done);
	init_completion(&dpu_crtc->dec_shdld_done);

	dpu_crtc->stream_id = stream_id;
	dpu_crtc->crtc_grp_id = pdata->di_grp_id;
	dpu_crtc->hw_plane_num = plane_grp->hw_plane_num;

	dpu_crtc->plane = devm_kcalloc(dev, dpu_crtc->hw_plane_num,
					sizeof(*dpu_crtc->plane), GFP_KERNEL);
	if (!dpu_crtc->plane)
		return -ENOMEM;

	ret = dpu_crtc_get_resources(dpu_crtc);
	if (ret) {
		dev_err(dev, "getting resources failed with %d.\n", ret);
		return ret;
	}

	plane_grp->res.fg[stream_id] = dpu_crtc->fg;
	dpu_crtc->plane[0] = dpu_plane_init(drm, 0, stream_id, plane_grp,
					DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(dpu_crtc->plane[0])) {
		ret = PTR_ERR(dpu_crtc->plane[0]);
		dev_err(dev, "initializing plane0 failed with %d.\n", ret);
		goto err_put_resources;
	}

	crtc->port = pdata->of_node;
	drm_crtc_helper_add(crtc, &dpu_helper_funcs);
	ret = drm_crtc_init_with_planes(drm, crtc, &dpu_crtc->plane[0]->base, NULL,
			&dpu_crtc_funcs, NULL);
	if (ret) {
		dev_err(dev, "adding crtc failed with %d.\n", ret);
		goto err_put_resources;
	}

	for (i = 1; i < dpu_crtc->hw_plane_num; i++) {
		dpu_crtc->plane[i] = dpu_plane_init(drm,
					drm_crtc_mask(&dpu_crtc->base),
					stream_id, plane_grp,
					DRM_PLANE_TYPE_OVERLAY);
		if (IS_ERR(dpu_crtc->plane[i])) {
			ret = PTR_ERR(dpu_crtc->plane[i]);
			dev_err(dev, "initializing plane%d failed with %d.\n",
								i, ret);
			goto err_put_resources;
		}
	}

	dpu_crtc->vbl_irq = dpu_map_inner_irq(dpu, stream_id ?
				IRQ_DISENGCFG_FRAMECOMPLETE1 :
				IRQ_DISENGCFG_FRAMECOMPLETE0);
	irq_set_status_flags(dpu_crtc->vbl_irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_irq(dev, dpu_crtc->vbl_irq, dpu_vbl_irq_handler, 0,
				"imx_drm", dpu_crtc);
	if (ret < 0) {
		dev_err(dev, "vblank irq request failed with %d.\n", ret);
		goto err_put_resources;
	}
	disable_irq(dpu_crtc->vbl_irq);

	dpu_crtc->safety_shdld_irq = dpu_map_inner_irq(dpu, stream_id ?
			IRQ_EXTDST5_SHDLOAD : IRQ_EXTDST4_SHDLOAD);
	irq_set_status_flags(dpu_crtc->safety_shdld_irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_irq(dev, dpu_crtc->safety_shdld_irq,
				dpu_safety_shdld_irq_handler, 0, "imx_drm",
				dpu_crtc);
	if (ret < 0) {
		dev_err(dev,
			"safety shadow load irq request failed with %d.\n",
			ret);
		goto err_put_resources;
	}
	disable_irq(dpu_crtc->safety_shdld_irq);

	dpu_crtc->content_shdld_irq = dpu_map_inner_irq(dpu, stream_id ?
			IRQ_EXTDST1_SHDLOAD : IRQ_EXTDST0_SHDLOAD);
	irq_set_status_flags(dpu_crtc->content_shdld_irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_irq(dev, dpu_crtc->content_shdld_irq,
				dpu_content_shdld_irq_handler, 0, "imx_drm",
				dpu_crtc);
	if (ret < 0) {
		dev_err(dev,
			"content shadow load irq request failed with %d.\n",
			ret);
		goto err_put_resources;
	}
	disable_irq(dpu_crtc->content_shdld_irq);

	dpu_crtc->dec_shdld_irq = dpu_map_inner_irq(dpu, stream_id ?
			IRQ_DISENGCFG_SHDLOAD1 : IRQ_DISENGCFG_SHDLOAD0);
	irq_set_status_flags(dpu_crtc->dec_shdld_irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_irq(dev, dpu_crtc->dec_shdld_irq,
				dpu_dec_shdld_irq_handler, 0, "imx_drm",
				dpu_crtc);
	if (ret < 0) {
		dev_err(dev,
			"DEC shadow load irq request failed with %d.\n",
			ret);
		goto err_put_resources;
	}
	disable_irq(dpu_crtc->dec_shdld_irq);

	return 0;

err_put_resources:
	dpu_crtc_put_resources(dpu_crtc);

	return ret;
}

static int dpu_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct dpu_client_platformdata *pdata = dev->platform_data;
	struct drm_device *drm = data;
	struct dpu_crtc *dpu_crtc;
	int ret;

	dpu_crtc = devm_kzalloc(dev, sizeof(*dpu_crtc), GFP_KERNEL);
	if (!dpu_crtc)
		return -ENOMEM;

	dpu_crtc->dev = dev;

	ret = dpu_crtc_init(dpu_crtc, pdata, drm);
	if (ret)
		return ret;

	if (!drm->mode_config.funcs)
		drm->mode_config.funcs = &dpu_drm_mode_config_funcs;

	dev_set_drvdata(dev, dpu_crtc);

	return 0;
}

static void dpu_crtc_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct dpu_crtc *dpu_crtc = dev_get_drvdata(dev);

	dpu_crtc_put_resources(dpu_crtc);
}

static const struct component_ops dpu_crtc_ops = {
	.bind = dpu_crtc_bind,
	.unbind = dpu_crtc_unbind,
};

static int dpu_crtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->platform_data)
		return -EINVAL;

	return component_add(dev, &dpu_crtc_ops);
}

static int dpu_crtc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dpu_crtc_ops);
	return 0;
}

static struct platform_driver dpu_crtc_driver = {
	.driver = {
		.name = "imx-dpu-crtc",
	},
	.probe = dpu_crtc_probe,
	.remove = dpu_crtc_remove,
};
module_platform_driver(dpu_crtc_driver);

MODULE_AUTHOR("NXP Semiconductor");
MODULE_DESCRIPTION("i.MX DPU CRTC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-dpu-crtc");
