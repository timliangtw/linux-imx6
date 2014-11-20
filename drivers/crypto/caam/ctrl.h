/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAAM control-plane driver backend public-level include definitions
 *
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 */

#ifndef CTRL_H
#define CTRL_H

/* Prototypes for backend-level services exposed to APIs */
int caam_get_era(u64 caam_id);

extern bool caam_dpaa2;

#endif /* CTRL_H */
