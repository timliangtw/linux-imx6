/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <dt-bindings/pinctrl/pins-imx8qxp.h>
#include <soc/imx8/sc/sci.h>

#include "pinctrl-imx.h"

extern sc_ipc_t pinctrl_ipcHandle;

static const struct pinctrl_pin_desc imx8qxp_pinctrl_pads[] = {
	IMX_PINCTRL_PIN(SC_P_PCIE_CTRL0_CLKREQ_B),
	IMX_PINCTRL_PIN(SC_P_PCIE_CTRL0_WAKE_B),
	IMX_PINCTRL_PIN(SC_P_PCIE_CTRL0_PERST_B),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_PCIESEP),
	IMX_PINCTRL_PIN(SC_P_USB_SS3_TC0),
	IMX_PINCTRL_PIN(SC_P_USB_SS3_TC1),
	IMX_PINCTRL_PIN(SC_P_USB_SS3_TC2),
	IMX_PINCTRL_PIN(SC_P_USB_SS3_TC3),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_3V3_USB3IO),
	IMX_PINCTRL_PIN(SC_P_EMMC0_CLK),
	IMX_PINCTRL_PIN(SC_P_EMMC0_CMD),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA0),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA1),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA2),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA3),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA4),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA5),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA6),
	IMX_PINCTRL_PIN(SC_P_EMMC0_DATA7),
	IMX_PINCTRL_PIN(SC_P_EMMC0_STROBE),
	IMX_PINCTRL_PIN(SC_P_EMMC0_RESET_B),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_SD1FIX),
	IMX_PINCTRL_PIN(SC_P_USDHC1_RESET_B),
	IMX_PINCTRL_PIN(SC_P_USDHC1_VSELECT),
	IMX_PINCTRL_PIN(SC_P_USDHC1_WP),
	IMX_PINCTRL_PIN(SC_P_USDHC1_CD_B),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_VSELSEP),
	IMX_PINCTRL_PIN(SC_P_USDHC1_CLK),
	IMX_PINCTRL_PIN(SC_P_USDHC1_CMD),
	IMX_PINCTRL_PIN(SC_P_USDHC1_DATA0),
	IMX_PINCTRL_PIN(SC_P_USDHC1_DATA1),
	IMX_PINCTRL_PIN(SC_P_USDHC1_DATA2),
	IMX_PINCTRL_PIN(SC_P_USDHC1_DATA3),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_VSEL3),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_TXC),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_TX_CTL),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_TXD0),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_TXD1),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_TXD2),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_TXD3),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_RXC),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_RX_CTL),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_RXD0),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_RXD1),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_RXD2),
	IMX_PINCTRL_PIN(SC_P_ENET0_RGMII_RXD3),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB),
	IMX_PINCTRL_PIN(SC_P_ENET0_REFCLK_125M_25M),
	IMX_PINCTRL_PIN(SC_P_ENET0_MDIO),
	IMX_PINCTRL_PIN(SC_P_ENET0_MDC),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_GPIOCT),
	IMX_PINCTRL_PIN(SC_P_FLEXCAN0_RX),
	IMX_PINCTRL_PIN(SC_P_FLEXCAN0_TX),
	IMX_PINCTRL_PIN(SC_P_FLEXCAN1_RX),
	IMX_PINCTRL_PIN(SC_P_FLEXCAN1_TX),
	IMX_PINCTRL_PIN(SC_P_UART0_RX),
	IMX_PINCTRL_PIN(SC_P_UART0_TX),
	IMX_PINCTRL_PIN(SC_P_UART0_RTS_B),
	IMX_PINCTRL_PIN(SC_P_UART0_CTS_B),
	IMX_PINCTRL_PIN(SC_P_UART1_TX),
	IMX_PINCTRL_PIN(SC_P_UART1_RX),
	IMX_PINCTRL_PIN(SC_P_UART1_RTS_B),
	IMX_PINCTRL_PIN(SC_P_UART1_CTS_B),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_GPIOLH),
	IMX_PINCTRL_PIN(SC_P_SPI0_SCK),
	IMX_PINCTRL_PIN(SC_P_SPI0_SDO),
	IMX_PINCTRL_PIN(SC_P_SPI0_SDI),
	IMX_PINCTRL_PIN(SC_P_SPI0_CS0),
	IMX_PINCTRL_PIN(SC_P_SPI0_CS1),
	IMX_PINCTRL_PIN(SC_P_SPI2_SCK),
	IMX_PINCTRL_PIN(SC_P_SPI2_SDO),
	IMX_PINCTRL_PIN(SC_P_SPI2_SDI),
	IMX_PINCTRL_PIN(SC_P_SPI2_CS0),
	IMX_PINCTRL_PIN(SC_P_SPI2_CS1),
	IMX_PINCTRL_PIN(SC_P_SAI1_RXC),
	IMX_PINCTRL_PIN(SC_P_SAI1_RXD),
	IMX_PINCTRL_PIN(SC_P_SAI1_RXFS),
	IMX_PINCTRL_PIN(SC_P_SAI1_TXC),
	IMX_PINCTRL_PIN(SC_P_SAI1_TXD),
	IMX_PINCTRL_PIN(SC_P_SAI1_TXFS),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_GPIORHT),
	IMX_PINCTRL_PIN(SC_P_ESAI0_FSR),
	IMX_PINCTRL_PIN(SC_P_ESAI0_FST),
	IMX_PINCTRL_PIN(SC_P_ESAI0_SCKR),
	IMX_PINCTRL_PIN(SC_P_ESAI0_SCKT),
	IMX_PINCTRL_PIN(SC_P_ESAI0_TX0),
	IMX_PINCTRL_PIN(SC_P_ESAI0_TX1),
	IMX_PINCTRL_PIN(SC_P_ESAI0_TX2_RX3),
	IMX_PINCTRL_PIN(SC_P_ESAI0_TX3_RX2),
	IMX_PINCTRL_PIN(SC_P_ESAI0_TX4_RX1),
	IMX_PINCTRL_PIN(SC_P_ESAI0_TX5_RX0),
	IMX_PINCTRL_PIN(SC_P_SPDIF0_RX),
	IMX_PINCTRL_PIN(SC_P_SPDIF0_TX),
	IMX_PINCTRL_PIN(SC_P_SPDIF0_EXT_CLK),
	IMX_PINCTRL_PIN(SC_P_SPI3_SCK),
	IMX_PINCTRL_PIN(SC_P_SPI3_SDO),
	IMX_PINCTRL_PIN(SC_P_SPI3_SDI),
	IMX_PINCTRL_PIN(SC_P_SPI3_CS0),
	IMX_PINCTRL_PIN(SC_P_SPI3_CS1),
	IMX_PINCTRL_PIN(SC_P_MCLK_IN0),
	IMX_PINCTRL_PIN(SC_P_MCLK_OUT0),
	IMX_PINCTRL_PIN(SC_P_FTM0),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_GPIORHB),
	IMX_PINCTRL_PIN(SC_P_ADC_IN1),
	IMX_PINCTRL_PIN(SC_P_ADC_IN0),
	IMX_PINCTRL_PIN(SC_P_ADC_IN3),
	IMX_PINCTRL_PIN(SC_P_ADC_IN2),
	IMX_PINCTRL_PIN(SC_P_CSI_D00),
	IMX_PINCTRL_PIN(SC_P_CSI_D01),
	IMX_PINCTRL_PIN(SC_P_CSI_D02),
	IMX_PINCTRL_PIN(SC_P_CSI_D03),
	IMX_PINCTRL_PIN(SC_P_CSI_D04),
	IMX_PINCTRL_PIN(SC_P_CSI_D05),
	IMX_PINCTRL_PIN(SC_P_CSI_D06),
	IMX_PINCTRL_PIN(SC_P_CSI_D07),
	IMX_PINCTRL_PIN(SC_P_CSI_HSYNC),
	IMX_PINCTRL_PIN(SC_P_CSI_VSYNC),
	IMX_PINCTRL_PIN(SC_P_CSI_PCLK),
	IMX_PINCTRL_PIN(SC_P_CSI_MCLK),
	IMX_PINCTRL_PIN(SC_P_CSI_EN),
	IMX_PINCTRL_PIN(SC_P_CSI_RESET),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_GPIORHD),
	IMX_PINCTRL_PIN(SC_P_PMIC_I2C_SCL),
	IMX_PINCTRL_PIN(SC_P_PMIC_I2C_SDA),
	IMX_PINCTRL_PIN(SC_P_PMIC_INT_B),
	IMX_PINCTRL_PIN(SC_P_SCU_GPIO0_00),
	IMX_PINCTRL_PIN(SC_P_SCU_GPIO0_01),
	IMX_PINCTRL_PIN(SC_P_SCU_BOOT_MODE0),
	IMX_PINCTRL_PIN(SC_P_SCU_BOOT_MODE1),
	IMX_PINCTRL_PIN(SC_P_SCU_BOOT_MODE2),
	IMX_PINCTRL_PIN(SC_P_SCU_BOOT_MODE3),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI0_I2C0_SCL),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI0_I2C0_SDA),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI0_GPIO0_00),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI0_GPIO0_01),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI1_I2C0_SCL),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI1_I2C0_SDA),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI1_GPIO0_00),
	IMX_PINCTRL_PIN(SC_P_MIPI_DSI1_GPIO0_01),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_MIPIDSIGPIO),
	IMX_PINCTRL_PIN(SC_P_MIPI_CSI0_MCLK_OUT),
	IMX_PINCTRL_PIN(SC_P_MIPI_CSI0_I2C0_SCL),
	IMX_PINCTRL_PIN(SC_P_MIPI_CSI0_I2C0_SDA),
	IMX_PINCTRL_PIN(SC_P_MIPI_CSI0_GPIO0_00),
	IMX_PINCTRL_PIN(SC_P_MIPI_CSI0_GPIO0_01),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_DATA0),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_DATA1),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_DATA2),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_DATA3),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_DQS),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_SS0_B),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_SS1_B),
	IMX_PINCTRL_PIN(SC_P_QSPI0A_SCLK),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_SCLK),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_DATA0),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_DATA1),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_DATA2),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_DATA3),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_DQS),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_SS0_B),
	IMX_PINCTRL_PIN(SC_P_QSPI0B_SS1_B),
	IMX_PINCTRL_PIN(SC_P_COMP_CTL_GPIO_1V8_3V3_QSPI0),
	IMX_PINCTRL_PIN(SC_P_XTALI),
	IMX_PINCTRL_PIN(SC_P_XTALO),
	IMX_PINCTRL_PIN(SC_P_ANA_TEST_OUT_P),
	IMX_PINCTRL_PIN(SC_P_ANA_TEST_OUT_N),
	IMX_PINCTRL_PIN(SC_P_RTC_XTALI),
	IMX_PINCTRL_PIN(SC_P_RTC_XTALO),
	IMX_PINCTRL_PIN(SC_P_PMIC_ON_REQ),
	IMX_PINCTRL_PIN(SC_P_ON_OFF_BUTTON),
};

static struct imx_pinctrl_soc_info imx8qxp_pinctrl_info = {
	.pins = imx8qxp_pinctrl_pads,
	.npins = ARRAY_SIZE(imx8qxp_pinctrl_pads),
	.flags = IMX8_USE_SCU | SHARE_MUX_CONF_REG
		| IMX8_ENABLE_MUX_CONFIG | IMX8_ENABLE_PAD_CONFIG,
};

static struct of_device_id imx8qxp_pinctrl_of_match[] = {
	{ .compatible = "fsl,imx8qxp-iomuxc", },
	{ /* sentinel */ }
};

static int imx8qxp_pinctrl_probe(struct platform_device *pdev)
{
	uint32_t mu_id;
	sc_err_t sciErr = SC_ERR_NONE;

	sciErr = sc_ipc_getMuID(&mu_id);
	if (sciErr != SC_ERR_NONE) {
		pr_info("pinctrl: Cannot obtain MU ID\n");
		return sciErr;
	}

	sciErr = sc_ipc_open(&pinctrl_ipcHandle, mu_id);

	if (sciErr != SC_ERR_NONE) {
		pr_info("pinctrl: Cannot open MU channel to SCU\n");
		return sciErr;
	};

	return imx_pinctrl_probe(pdev, &imx8qxp_pinctrl_info);
}

static struct platform_driver imx8qxp_pinctrl_driver = {
	.driver = {
		.name = "imx8qxp-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(imx8qxp_pinctrl_of_match),
	},
	.probe = imx8qxp_pinctrl_probe,
};

static int __init imx8qxp_pinctrl_init(void)
{
	return platform_driver_register(&imx8qxp_pinctrl_driver);
}
arch_initcall(imx8qxp_pinctrl_init);

static void __exit imx8qxp_pinctrl_exit(void)
{
	platform_driver_unregister(&imx8qxp_pinctrl_driver);
}
module_exit(imx8qxp_pinctrl_exit);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("Freescale imx8qxp pinctrl driver");
MODULE_LICENSE("GPL v2");
