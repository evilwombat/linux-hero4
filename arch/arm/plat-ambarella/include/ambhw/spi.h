/*
 * ambhw/spi.h
 *
 * History:
 *	2006/12/27 - [Charles Chiou] created file
 *
 * Copyright (C) 2006-2008, Ambarella, Inc.
 */

#ifndef __AMBHW__SPI_H__
#define __AMBHW__SPI_H__

#include <ambhw/chip.h>
#include <ambhw/busaddr.h>

/****************************************************/
/* Capabilities based on chip revision              */
/****************************************************/
#if (CHIP_REV == A1)
#define SPI_MAX_SLAVE_ID 			3
#elif (CHIP_REV == I1)
#define SPI_AHB_MAX_SLAVE_ID			0
#define SPI_MAX_SLAVE_ID 			7
#else
#define SPI_MAX_SLAVE_ID 			7
#endif

#if (CHIP_REV == A1)
#define SPI_SUPPORT_TISSP_NSM			0
#else
#define SPI_SUPPORT_TISSP_NSM			1
#endif

#if ((CHIP_REV == A3) || (CHIP_REV == A5) || (CHIP_REV == A6) || \
     (CHIP_REV == A5S))
#define SPI_INSTANCES				2
#define SPI_AHB_INSTANCES			0
#define SPI_SUPPORT_TSSI_MODE			1

#elif (CHIP_REV == A7)
#define SPI_INSTANCES				3
#define SPI_AHB_INSTANCES			0
#define SPI_SUPPORT_TSSI_MODE			1

#elif (CHIP_REV == S2)
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_AHB64
#define SPI_INSTANCES				2
#else
#define SPI_INSTANCES				1
#endif
#define SPI_AHB_INSTANCES			0
#define SPI_SUPPORT_TSSI_MODE			0

#elif (CHIP_REV == I1)
#define SPI_INSTANCES				4
#define SPI_AHB_INSTANCES			1
#define SPI_SUPPORT_TSSI_MODE			1

#else
#define SPI_INSTANCES				1
#define SPI_AHB_INSTANCES			0
#define SPI_SUPPORT_TSSI_MODE			0
#endif

#if ((CHIP_REV == A1) || (CHIP_REV == A2) || (CHIP_REV == A3) || \
     (CHIP_REV == A5) || (CHIP_REV == A6))
#define SPI_EN2_EN3_ENABLED_BY_HOST_ENA_REG	1
#else
#define SPI_EN2_EN3_ENABLED_BY_HOST_ENA_REG	0
#endif

#if ((CHIP_REV == A2S) || (CHIP_REV == A2M) || (CHIP_REV == A2Q))
#define SPI_EN2_ENABLED_BY_GPIO2_AFSEL_REG	1
#else
#define SPI_EN2_ENABLED_BY_GPIO2_AFSEL_REG	0
#endif

#if (CHIP_REV == A5S) || (CHIP_REV == A7) || (CHIP_REV == I1)
#define SPI_EN4_7_ENABLED_BY_GPIO1_AFSEL_REG	1
#else
#define SPI_EN4_7_ENABLED_BY_GPIO1_AFSEL_REG	0
#endif

#if (CHIP_REV == A5S) || (CHIP_REV == A7) || (CHIP_REV == I1) || \
    (CHIP_REV == A7L) || (CHIP_REV == S2)
#define SPI_SLAVE_INSTANCES			1
#else
#define SPI_SLAVE_INSTANCES			0
#endif

#if (CHIP_REV == I1) || (CHIP_REV == A7L) || (CHIP_REV == S2) || \
    (CHIP_REV == A8)
#define SPI_SUPPORT_MASTER_CHANGE_ENA_POLARITY	1
#define SPI_SUPPORT_MASTER_DELAY_START_TIME	1
#define SPI_SUPPORT_NSM_SHAKE_START_BIT_CHSANGE	1
#else
#define SPI_SUPPORT_MASTER_CHANGE_ENA_POLARITY	0
#define SPI_SUPPORT_MASTER_DELAY_START_TIME	0
#define SPI_SUPPORT_NSM_SHAKE_START_BIT_CHSANGE	0
#endif

#if (CHIP_REV == A5L)
#define SPI_EN2_EN3_ENABLED_BY_GPIO2_AFSEL_REG	1
#else
#define SPI_EN2_EN3_ENABLED_BY_GPIO2_AFSEL_REG	0
#endif

/* SPI_FIFO_SIZE */
#define SPI_DATA_FIFO_SIZE_16		0x10
#define SPI_DATA_FIFO_SIZE_32		0x20
#define SPI_DATA_FIFO_SIZE_64		0x40
#define SPI_DATA_FIFO_SIZE_128		0x80

/****************************************************/
/* Controller registers definitions                 */
/****************************************************/

#define SPI_CTRLR0_OFFSET		0x00
#define SPI_CTRLR1_OFFSET		0x04
#define SPI_SSIENR_OFFSET		0x08
#define SPI_MWCR_OFFSET			0x0c
#define SPI_SER_OFFSET			0x10
#define SPI_BAUDR_OFFSET		0x14
#define SPI_TXFTLR_OFFSET		0x18
#define SPI_RXFTLR_OFFSET		0x1c
#define SPI_TXFLR_OFFSET		0x20
#define SPI_RXFLR_OFFSET		0x24
#define SPI_SR_OFFSET			0x28
#define SPI_IMR_OFFSET			0x2c
#define SPI_ISR_OFFSET			0x30
#define SPI_RISR_OFFSET			0x34
#define SPI_TXOICR_OFFSET		0x38
#define SPI_RXOICR_OFFSET		0x3c
#define SPI_RXUICR_OFFSET		0x40
#define SPI_MSTICR_OFFSET		0x44
#define SPI_ICR_OFFSET			0x48
#if (SPI_AHB_INSTANCES >= 1)
#define SPI_DMAC_OFFSET			0x4c
#endif
#define SPI_IDR_OFFSET			0x58
#define SPI_VERSION_ID_OFFSET		0x5c
#define SPI_DR_OFFSET			0x60

#if (SPI_SUPPORT_MASTER_CHANGE_ENA_POLARITY == 1)
#define SPI_SSIENPOLR_OFFSET		0x260
#endif
#if (SPI_SUPPORT_MASTER_DELAY_START_TIME == 1)
#define SPI_SCLK_OUT_DLY_OFFSET		0x264
#endif
#if (SPI_SUPPORT_NSM_SHAKE_START_BIT_CHSANGE == 1)
#define SPI_START_BIT_OFFSET		0x268
#endif

#define TSSI_CTRL_OFFSET		0x00
#define TSSI_SSR_OFFSET			0x04
#define TSSI_INDEX_OFFSET		0x08
#define TSSI_DATA_OFFSET		0x0c
#define TSSI_POLARITY_INVERT		0x10

#define SPI_CTRLR0_REG			SPI_REG(SPI_CTRLR0_OFFSET)
#define SPI_CTRLR1_REG			SPI_REG(SPI_CTRLR1_OFFSET)
#define SPI_SSIENR_REG			SPI_REG(SPI_SSIENR_OFFSET)
#define SPI_MWCR_REG			SPI_REG(SPI_MWCR_OFFSET)
#define SPI_SER_REG			SPI_REG(SPI_SER_OFFSET)
#define SPI_BAUDR_REG			SPI_REG(SPI_BAUDR_OFFSET)
#define SPI_TXFTLR_REG			SPI_REG(SPI_TXFTLR_OFFSET)
#define SPI_RXFTLR_REG			SPI_REG(SPI_RXFTLR_OFFSET)
#define SPI_TXFLR_REG			SPI_REG(SPI_TXFLR_OFFSET)
#define SPI_RXFLR_REG			SPI_REG(SPI_RXFLR_OFFSET)
#define SPI_SR_REG			SPI_REG(SPI_SR_OFFSET)
#define SPI_IMR_REG			SPI_REG(SPI_IMR_OFFSET)
#define SPI_ISR_REG			SPI_REG(SPI_ISR_OFFSET)
#define SPI_RISR_REG			SPI_REG(SPI_RISR_OFFSET)
#define SPI_TXOICR_REG			SPI_REG(SPI_TXOICR_OFFSET)
#define SPI_RXOICR_REG			SPI_REG(SPI_RXOICR_OFFSET)
#define SPI_RXUICR_REG			SPI_REG(SPI_RXUICR_OFFSET)
#define SPI_MSTICR_REG			SPI_REG(SPI_MSTICR_OFFSET)
#define SPI_ICR_REG			SPI_REG(SPI_ICR_OFFSET)
#define SPI_IDR_REG			SPI_REG(SPI_IDR_OFFSET)
#define SPI_VERSION_ID_REG		SPI_REG(SPI_VERSION_ID_OFFSET)
#define SPI_DR_REG			SPI_REG(SPI_DR_OFFSET)

#if (SPI_INSTANCES >= 2)
#define SPI2_CTRLR0_REG			SPI2_REG(SPI_CTRLR0_OFFSET)
#define SPI2_CTRLR1_REG			SPI2_REG(SPI_CTRLR1_OFFSET)
#define SPI2_SSIENR_REG			SPI2_REG(SPI_SSIENR_OFFSET)
#define SPI2_MWCR_REG			SPI2_REG(SPI_MWCR_OFFSET)
#define SPI2_SER_REG			SPI2_REG(SPI_SER_OFFSET)
#define SPI2_BAUDR_REG			SPI2_REG(SPI_BAUDR_OFFSET)
#define SPI2_TXFTLR_REG			SPI2_REG(SPI_TXFTLR_OFFSET)
#define SPI2_RXFTLR_REG			SPI2_REG(SPI_RXFTLR_OFFSET)
#define SPI2_TXFLR_REG			SPI2_REG(SPI_TXFLR_OFFSET)
#define SPI2_RXFLR_REG			SPI2_REG(SPI_RXFLR_OFFSET)
#define SPI2_SR_REG			SPI2_REG(SPI_SR_OFFSET)
#define SPI2_IMR_REG			SPI2_REG(SPI_IMR_OFFSET)
#define SPI2_ISR_REG			SPI2_REG(SPI_ISR_OFFSET)
#define SPI2_RISR_REG			SPI2_REG(SPI_RISR_OFFSET)
#define SPI2_TXOICR_REG			SPI2_REG(SPI_TXOICR_OFFSET)
#define SPI2_RXOICR_REG			SPI2_REG(SPI_RXOICR_OFFSET)
#define SPI2_RXUICR_REG			SPI2_REG(SPI_RXUICR_OFFSET)
#define SPI2_MSTICR_REG			SPI2_REG(SPI_MSTICR_OFFSET)
#define SPI2_ICR_REG			SPI2_REG(SPI_ICR_OFFSET)
#define SPI2_IDR_REG			SPI2_REG(SPI_IDR_OFFSET)
#define SPI2_VERSION_ID_REG		SPI2_REG(SPI_VERSION_ID_OFFSET)
#define SPI2_DR_REG			SPI2_REG(SPI_DR_OFFSET)
#endif

#if (SPI_INSTANCES >= 3)
#define SPI3_CTRLR0_REG			SPI3_REG(SPI_CTRLR0_OFFSET)
#define SPI3_CTRLR1_REG			SPI3_REG(SPI_CTRLR1_OFFSET)
#define SPI3_SSIENR_REG			SPI3_REG(SPI_SSIENR_OFFSET)
#define SPI3_MWCR_REG			SPI3_REG(SPI_MWCR_OFFSET)
#define SPI3_SER_REG			SPI3_REG(SPI_SER_OFFSET)
#define SPI3_BAUDR_REG			SPI3_REG(SPI_BAUDR_OFFSET)
#define SPI3_TXFTLR_REG			SPI3_REG(SPI_TXFTLR_OFFSET)
#define SPI3_RXFTLR_REG			SPI3_REG(SPI_RXFTLR_OFFSET)
#define SPI3_TXFLR_REG			SPI3_REG(SPI_TXFLR_OFFSET)
#define SPI3_RXFLR_REG			SPI3_REG(SPI_RXFLR_OFFSET)
#define SPI3_SR_REG			SPI3_REG(SPI_SR_OFFSET)
#define SPI3_IMR_REG			SPI3_REG(SPI_IMR_OFFSET)
#define SPI3_ISR_REG			SPI3_REG(SPI_ISR_OFFSET)
#define SPI3_RISR_REG			SPI3_REG(SPI_RISR_OFFSET)
#define SPI3_TXOICR_REG			SPI3_REG(SPI_TXOICR_OFFSET)
#define SPI3_RXOICR_REG			SPI3_REG(SPI_RXOICR_OFFSET)
#define SPI3_RXUICR_REG			SPI3_REG(SPI_RXUICR_OFFSET)
#define SPI3_MSTICR_REG			SPI3_REG(SPI_MSTICR_OFFSET)
#define SPI3_ICR_REG			SPI3_REG(SPI_ICR_OFFSET)
#define SPI3_IDR_REG			SPI3_REG(SPI_IDR_OFFSET)
#define SPI3_VERSION_ID_REG		SPI3_REG(SPI_VERSION_ID_OFFSET)
#define SPI3_DR_REG			SPI3_REG(SPI_DR_OFFSET)
#endif

#if (SPI_INSTANCES >= 4)
#define SPI4_CTRLR0_REG			SPI4_REG(SPI_CTRLR0_OFFSET)
#define SPI4_CTRLR1_REG			SPI4_REG(SPI_CTRLR1_OFFSET)
#define SPI4_SSIENR_REG			SPI4_REG(SPI_SSIENR_OFFSET)
#define SPI4_MWCR_REG			SPI4_REG(SPI_MWCR_OFFSET)
#define SPI4_SER_REG			SPI4_REG(SPI_SER_OFFSET)
#define SPI4_BAUDR_REG			SPI4_REG(SPI_BAUDR_OFFSET)
#define SPI4_TXFTLR_REG			SPI4_REG(SPI_TXFTLR_OFFSET)
#define SPI4_RXFTLR_REG			SPI4_REG(SPI_RXFTLR_OFFSET)
#define SPI4_TXFLR_REG			SPI4_REG(SPI_TXFLR_OFFSET)
#define SPI4_RXFLR_REG			SPI4_REG(SPI_RXFLR_OFFSET)
#define SPI4_SR_REG			SPI4_REG(SPI_SR_OFFSET)
#define SPI4_IMR_REG			SPI4_REG(SPI_IMR_OFFSET)
#define SPI4_ISR_REG			SPI4_REG(SPI_ISR_OFFSET)
#define SPI4_RISR_REG			SPI4_REG(SPI_RISR_OFFSET)
#define SPI4_TXOICR_REG			SPI4_REG(SPI_TXOICR_OFFSET)
#define SPI4_RXOICR_REG			SPI4_REG(SPI_RXOICR_OFFSET)
#define SPI4_RXUICR_REG			SPI4_REG(SPI_RXUICR_OFFSET)
#define SPI4_MSTICR_REG			SPI4_REG(SPI_MSTICR_OFFSET)
#define SPI4_ICR_REG			SPI4_REG(SPI_ICR_OFFSET)
#define SPI4_IDR_REG			SPI4_REG(SPI_IDR_OFFSET)
#define SPI4_VERSION_ID_REG		SPI4_REG(SPI_VERSION_ID_OFFSET)
#define SPI4_DR_REG			SPI4_REG(SPI_DR_OFFSET)
#endif

#if (SPI_AHB_INSTANCES >= 1)
#define SSI_DMA_CTRLR0_REG		SSI_DMA_REG(SPI_CTRLR0_OFFSET)
#define SSI_DMA_CTRLR1_REG		SSI_DMA_REG(SPI_CTRLR1_OFFSET)
#define SSI_DMA_SSIENR_REG		SSI_DMA_REG(SPI_SSIENR_OFFSET)
#define SSI_DMA_MWCR_REG		SSI_DMA_REG(SPI_MWCR_OFFSET)
#define SSI_DMA_SER_REG			SSI_DMA_REG(SPI_SER_OFFSET)
#define SSI_DMA_BAUDR_REG		SSI_DMA_REG(SPI_BAUDR_OFFSET)
#define SSI_DMA_TXFTLR_REG		SSI_DMA_REG(SPI_TXFTLR_OFFSET)
#define SSI_DMA_RXFTLR_REG		SSI_DMA_REG(SPI_RXFTLR_OFFSET)
#define SSI_DMA_TXFLR_REG		SSI_DMA_REG(SPI_TXFLR_OFFSET)
#define SSI_DMA_RXFLR_REG		SSI_DMA_REG(SPI_RXFLR_OFFSET)
#define SSI_DMA_SR_REG			SSI_DMA_REG(SPI_SR_OFFSET)
#define SSI_DMA_IMR_REG			SSI_DMA_REG(SPI_IMR_OFFSET)
#define SSI_DMA_ISR_REG			SSI_DMA_REG(SPI_ISR_OFFSET)
#define SSI_DMA_RISR_REG		SSI_DMA_REG(SPI_RISR_OFFSET)
#define SSI_DMA_TXOICR_REG		SSI_DMA_REG(SPI_TXOICR_OFFSET)
#define SSI_DMA_RXOICR_REG		SSI_DMA_REG(SPI_RXOICR_OFFSET)
#define SSI_DMA_RXUICR_REG		SSI_DMA_REG(SPI_RXUICR_OFFSET)
#define SSI_DMA_MSTICR_REG		SSI_DMA_REG(SPI_MSTICR_OFFSET)
#define SSI_DMA_ICR_REG			SSI_DMA_REG(SPI_ICR_OFFSET)
#define SSI_DMA_DMAC_REG		SSI_DMA_REG(SPI_DMAC_OFFSET)
#define SSI_DMA_IDR_REG			SSI_DMA_REG(SPI_IDR_OFFSET)
#define SSI_DMA_VERSION_ID_REG		SSI_DMA_REG(SPI_VERSION_ID_OFFSET)
#define SSI_DMA_DR_REG			SSI_DMA_REG(SPI_DR_OFFSET)
#endif

#if (SPI_SLAVE_INSTANCES >= 1)
#define SPI_SLAVE_CTRLR0_REG		SPI_SLAVE_REG(SPI_CTRLR0_OFFSET)
#define SPI_SLAVE_CTRLR1_REG		SPI_SLAVE_REG(SPI_CTRLR1_OFFSET)
#define SPI_SLAVE_SSIENR_REG		SPI_SLAVE_REG(SPI_SSIENR_OFFSET)
#define SPI_SLAVE_MWCR_REG		SPI_SLAVE_REG(SPI_MWCR_OFFSET)
#define SPI_SLAVE_SER_REG		SPI_SLAVE_REG(SPI_SER_OFFSET)
#define SPI_SLAVE_BAUDR_REG		SPI_SLAVE_REG(SPI_BAUDR_OFFSET)
#define SPI_SLAVE_TXFTLR_REG		SPI_SLAVE_REG(SPI_TXFTLR_OFFSET)
#define SPI_SLAVE_RXFTLR_REG		SPI_SLAVE_REG(SPI_RXFTLR_OFFSET)
#define SPI_SLAVE_TXFLR_REG		SPI_SLAVE_REG(SPI_TXFLR_OFFSET)
#define SPI_SLAVE_RXFLR_REG		SPI_SLAVE_REG(SPI_RXFLR_OFFSET)
#define SPI_SLAVE_SR_REG		SPI_SLAVE_REG(SPI_SR_OFFSET)
#define SPI_SLAVE_IMR_REG		SPI_SLAVE_REG(SPI_IMR_OFFSET)
#define SPI_SLAVE_ISR_REG		SPI_SLAVE_REG(SPI_ISR_OFFSET)
#define SPI_SLAVE_RISR_REG		SPI_SLAVE_REG(SPI_RISR_OFFSET)
#define SPI_SLAVE_TXOICR_REG		SPI_SLAVE_REG(SPI_TXOICR_OFFSET)
#define SPI_SLAVE_RXOICR_REG		SPI_SLAVE_REG(SPI_RXOICR_OFFSET)
#define SPI_SLAVE_RXUICR_REG		SPI_SLAVE_REG(SPI_RXUICR_OFFSET)
#define SPI_SLAVE_MSTICR_REG		SPI_SLAVE_REG(SPI_MSTICR_OFFSET)
#define SPI_SLAVE_ICR_REG		SPI_SLAVE_REG(SPI_ICR_OFFSET)
#define SPI_SLAVE_IDR_REG		SPI_SLAVE_REG(SPI_IDR_OFFSET)
#define SPI_SLAVE_VERSION_ID_REG	SPI_SLAVE_REG(SPI_VERSION_ID_OFFSET)
#define SPI_SLAVE_DR_REG		SPI_SLAVE_REG(SPI_DR_OFFSET)
#endif

#define TSSI_CTRL_REG			TSSI_REG(TSSI_CTRL_OFFSET)
#define TSSI_SSR_REG			TSSI_REG(TSSI_SSR_OFFSET)
#define TSSI_INDEX_REG			TSSI_REG(TSSI_INDEX_OFFSET)
#define TSSI_DATA_REG			TSSI_REG(TSSI_DATA_OFFSET)
#define TSSI_POLARITY_INVERT_REG	TSSI_REG(TSSI_POLARITY_INVERT)

#endif
