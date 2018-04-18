/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#ifndef _SC_LPCG_H
#define _SC_LPCG_H

/*LSIO SS */
#define		PWM_0_LPCG		0x5D400000
#define		PWM_1_LPCG		0x5D410000
#define		PWM_2_LPCG		0x5D420000
#define		PWM_3_LPCG		0x5D430000
#define		PWM_4_LPCG		0x5D440000
#define		PWM_5_LPCG		0x5D450000
#define		PWM_6_LPCG		0x5D460000
#define		PWM_7_LPCG		0x5D470000
#define		GPIO_0_LPCG		0x5D480000
#define		GPIO_1_LPCG		0x5D490000
#define		GPIO_2_LPCG		0x5D4A0000
#define		GPIO_3_LPCG		0x5D4B0000
#define		GPIO_4_LPCG		0x5D4C0000
#define		GPIO_5_LPCG		0x5D4D0000
#define		GPIO_6_LPCG		0x5D4E0000
#define		GPIO_7_LPCG		0x5D4F0000
#define		FSPI_0_LPCG		0x5D520000
#define		FSPI_1_LPCG		0x5D530000
#define		GPT_0_LPCG		0x5D540000
#define		GPT_1_LPCG		0x5D550000
#define		GPT_2_LPCG		0x5D560000
#define		GPT_3_LPCG		0x5D570000
#define		GPT_4_LPCG		0x5D580000
#define		OCRAM_LPCG		0x5D590000
#define		KPP_LPCG		0x5D5A0000
#define		ROMCP_LPCG		0x5D500000

/* HSIO SS */
#define		CRR_5_LPCG		0x5F0F0000
#define		CRR_4_LPCG		0x5F0E0000
#define		CRR_3_LPCG		0x5F0D0000
#define		CRR_2_LPCG		0x5F0C0000
#define		CRR_1_LPCG		0x5F0B0000
#define		CRR_0_LPCG		0x5F0A0000
#define		PHY_1_LPCG		0x5F090000
#define		PHY_2_LPCG		0x5F080000
#define		SATA_0_LPCG		0x5F070000
#define		PCIE_B_LPCG		0x5F060000
#define		PCIE_A_LPCG		0x5F050000

/* DMA SS */
#define		FLEX_CAN_2_LPCG		0x5ACF0000
#define		FLEX_CAN_1_LPCG		0x5ACE0000
#define		FLEX_CAN_0_LPCG		0x5ACD0000
#define		FTM_1_LPCG		0x5ACB0000
#define		FTM_0_LPCG		0x5ACA0000
#define		ADC_0_LPCG		0x5AC80000
#define		LPI2C_3_LPCG		0x5AC30000
#define		LPI2C_2_LPCG		0x5AC20000
#define		LPI2C_1_LPCG		0x5AC10000
#define		LPI2C_0_LPCG		0x5AC00000
#define		PWM_LPCG		0x5A590000
#define		LCD_LPCG		0x5A580000
#define		LPUART_3_LPCG		0x5A490000
#define		LPUART_2_LPCG		0x5A480000
#define		LPUART_1_LPCG		0x5A470000
#define		LPUART_0_LPCG		0x5A460000
#define		LPSPI_3_LPCG		0x5A430000
#define		LPSPI_2_LPCG		0x5A420000
#define		LPSPI_1_LPCG		0x5A410000
#define		LPSPI_0_LPCG		0x5A400000

/* Display SS */
#define		DC_0_LPCG		0x56010000
#define		DC_1_LPCG		0x57010000

/* LVDS */
#define		DI_LVDS_0_LPCG		0x56243000
#define		DI_LVDS_1_LPCG		0x57243000

/* DI HDMI */
#define		DI_HDMI_LPCG		0x56263000

/* RX-HDMI */
#define		RX_HDMI_LPCG		0x58263000

/* MIPI CSI SS */
#define		MIPI_CSI_0_LPCG		0x58223000
#define		MIPI_CSI_1_LPCG		0x58243000

/* PARALLEL CSI SS */
#define		PARALLEL_CSI_LPCG	0x58263000

/* Display MIPI SS */
#define		DI_MIPI0_LPCG		0x56223000
#define		DI_MIPI1_LPCG		0x56243000

/* Imaging SS */
#define IMG_JPEG_ENC_LPCG		0x585F0000
#define IMG_JPEG_DEC_LPCG		0x585D0000
#define IMG_PXL_LINK_DC1_LPCG	0x585C0000
#define IMG_PXL_LINK_DC0_LPCG	0x585B0000
#define IMG_PXL_LINK_HDMI_LPCG	0x585A0000
#define IMG_PXL_LINK_CSI1_LPCG	0x58590000
#define IMG_PXL_LINK_CSI0_LPCG	0x58580000
#define IMG_PDMA_7_LPCG			0x58570000
#define IMG_PDMA_6_LPCG			0x58560000
#define IMG_PDMA_5_LPCG			0x58550000
#define IMG_PDMA_4_LPCG			0x58540000
#define IMG_PDMA_3_LPCG			0x58530000
#define IMG_PDMA_2_LPCG			0x58520000
#define IMG_PDMA_1_LPCG			0x58510000
#define IMG_PDMA_0_LPCG			0x58500000

/* HSIO SS */
#define HSIO_GPIO_LPCG			0x5F100000
#define HSIO_MISC_LPCG			0x5F0F0000
#define HSIO_SATA_CRR4_LPCG		0x5F0E0000
#define HSIO_PCIE_X1_CRR3_LPCG	0x5F0D0000
#define HSIO_PCIE_X2_CRR2_LPCG	0x5F0C0000
#define HSIO_PHY_X1_CRR1_LPCG	0x5F0B0000
#define HSIO_PHY_X2_CRR0_LPCG	0x5F0A0000
#define HSIO_PHY_X1_LPCG		0x5F090000
#define HSIO_PHY_X2_LPCG		0x5F080000
#define HSIO_SATA_LPCG			0x5F070000
#define HSIO_PCIE_X1_LPCG		0x5F060000
#define HSIO_PCIE_X2_LPCG		0x5F050000

/* M4 SS */
#define		M4_0_I2C_LPCG		0x37630000
#define		M4_0_LPUART_LPCG	0x37620000
#define		M4_0_LPIT_LPCG		0x37610000
#define		M4_1_I2C_LPCG		0x3B630000
#define		M4_1_LPUART_LPCG	0x3B620000
#define		M4_1_LPIT_LPCG		0x3B610000

/* Audio SS */
#define     AUD_ASRC_0_LPCG         0x59400000
#define     AUD_ESAI_0_LPCG         0x59410000
#define     AUD_SPDIF_0_LPCG        0x59420000
#define     AUD_SAI_0_LPCG          0x59440000
#define     AUD_SAI_1_LPCG          0x59450000
#define     AUD_SAI_2_LPCG          0x59460000
#define     AUD_SAI_3_LPCG          0x59470000
#define     AUD_GPT_5_LPCG          0x594B0000
#define     AUD_GPT_6_LPCG          0x594C0000
#define     AUD_GPT_7_LPCG          0x594D0000
#define     AUD_GPT_8_LPCG          0x594E0000
#define     AUD_GPT_9_LPCG          0x594F0000
#define     AUD_GPT_10_LPCG         0x59500000
#define     AUD_DSP_LPCG            0x59580000
#define     AUD_OCRAM_LPCG          0x59590000
#define     AUD_EDMA_0_LPCG         0x595f0000
#define     AUD_ASRC_1_LPCG         0x59c00000
#define     AUD_SAI_4_LPCG          0x59c20000
#define     AUD_SAI_5_LPCG          0x59c30000
#define     AUD_AMIX_LPCG           0x59c40000
#define     AUD_MQS_LPCG            0x59c50000
#define     AUD_ACM_LPCG            0x59c60000
#define     AUD_REC_CLK0_LPCG       0x59d00000
#define     AUD_REC_CLK1_LPCG       0x59d10000
#define     AUD_PLL_CLK0_LPCG       0x59d20000
#define     AUD_PLL_CLK1_LPCG       0x59d30000
#define     AUD_MCLKOUT0_LPCG       0x59d50000
#define     AUD_MCLKOUT1_LPCG       0x59d60000
#define     AUD_EDMA_1_LPCG         0x59df0000


/* Connectivity SS */
#define     USDHC_0_LPCG        0x5B200000
#define     USDHC_1_LPCG        0x5B210000
#define     USDHC_2_LPCG        0x5B220000
#define     ENET_0_LPCG         0x5B230000
#define     ENET_1_LPCG         0x5B240000
#define     DTCP_LPCG           0x5B250000
#define     MLB_LPCG            0x5B260000
#define     USB_2_LPCG          0x5B270000
#define     USB_3_LPCG          0x5B280000
#define     NAND_LPCG           0x5B290000
#define     EDMA_LPCG           0x5B2A0000

/* CM40 SS */
#define     CM40_I2C_LPCG       0x37630000


#endif
