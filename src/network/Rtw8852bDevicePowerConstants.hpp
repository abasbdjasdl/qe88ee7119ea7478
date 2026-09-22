// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 d1fced1b8a741dc9f92b47c69489c24385945f6e; BSD option.
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace powerseq {
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;
constexpr u32 bit(unsigned n){return u32(1)<<n;}
constexpr u32 mask(unsigned hi,unsigned lo){return (0xffffffffu>>(31-hi))&(0xffffffffu<<lo);}
constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}
enum rtw89_cv {
	CHIP_CAV,
	CHIP_CBV,
	CHIP_CCV,
	CHIP_CDV,
	CHIP_CEV,
	CHIP_CFV,
	CHIP_CV_MAX,
	CHIP_CV_INVALID = CHIP_CV_MAX,
};
enum rtw89_mac_xtal_si_offset {
	XTAL0 = 0x0,
	XTAL3 = 0x3,
	XTAL_SI_XTAL_SC_XI = 0x04,
	XTAL_SI_XTAL_SC_XO = 0x05,
	XTAL_SI_XREF_MODE = 0x0B,
	XTAL_SI_PWR_CUT = 0x10,
	XTAL_SI_XTAL_DRV = 0x15,
	XTAL_SI_XTAL_PLL = 0x16,
	XTAL_SI_XTAL_XMD_2 = 0x24,
	XTAL_SI_XTAL_XMD_4 = 0x26,
	XTAL_SI_XREF_RF1 = 0x2D,
	XTAL_SI_XREF_RF2 = 0x2E,
	XTAL_SI_CV = 0x41,
	XTAL_SI_LOW_ADDR = 0x62,
	XTAL_SI_CTRL = 0x63,
	XTAL_SI_READ_VAL = 0x7A,
	XTAL_SI_WL_RFC_S0 = 0x80,
	XTAL_SI_WL_RFC_S1 = 0x81,
	XTAL_SI_ANAPAR_WL = 0x90,
	XTAL_SI_SRAM_CTRL = 0xA1,
	XTAL_SI_APBT = 0xD1,
	XTAL_SI_PLL = 0xE0,
	XTAL_SI_PLL_1 = 0xE1,
};
constexpr u32 R_AX_SPS_ANA_ON_CTRL2 = 0x0228;
constexpr u32 RTL8852B_RFE_05_SPS_ANA = 0x4A82;
constexpr u32 R_AX_SYS_PW_CTRL = 0x0004;
constexpr u32 B_AX_AFSM_WLSUS_EN = bit(11);
constexpr u32 B_AX_AFSM_PCIE_SUS_EN = bit(12);
constexpr u32 B_AX_DIS_WLBT_PDNSUSEN_SOPC = bit(18);
constexpr u32 R_AX_WLLPS_CTRL = 0x0090;
constexpr u32 B_AX_DIS_WLBT_LPSEN_LOPC = bit(1);
constexpr u32 B_AX_APDM_HPDN = bit(15);
constexpr u32 B_AX_APFM_SWLPS = bit(10);
constexpr u32 B_AX_RDY_SYSPWR = bit(17);
constexpr u32 R_AX_AFE_LDO_CTRL = 0x0020;
constexpr u32 B_AX_AON_OFF_PC_EN = bit(23);
constexpr u32 R_AX_SPS_DIG_OFF_CTRL0 = 0x0400;
constexpr u32 B_AX_C1_L1_MASK = mask(1, 0);
constexpr u32 B_AX_C3_L1_MASK = mask(5, 4);
constexpr u32 B_AX_EN_WLON = bit(16);
constexpr u32 B_AX_APFN_ONMAC = bit(8);
constexpr u32 R_AX_PLATFORM_ENABLE = 0x0088;
constexpr u32 B_AX_PLATFORM_EN = bit(0);
constexpr u32 R_AX_SYS_SDIO_CTRL = 0x0070;
constexpr u32 B_AX_PCIE_CALIB_EN_V1 = bit(12);
constexpr u32 R_AX_SYS_ADIE_PAD_PWR_CTRL = 0x0018;
constexpr u32 B_AX_SYM_PADPDN_WL_PTA_1P3 = bit(6);
constexpr u32 XTAL_SI_GND_SHDN_WL = bit(6);
constexpr u32 B_AX_SYM_PADPDN_WL_RFC_1P3 = bit(5);
constexpr u32 XTAL_SI_SHDN_WL = bit(5);
constexpr u32 XTAL_SI_OFF_WEI = bit(2);
constexpr u32 XTAL_SI_OFF_EI = bit(3);
constexpr u32 XTAL_SI_RFC2RF = bit(4);
constexpr u32 XTAL_SI_PON_WEI = bit(0);
constexpr u32 XTAL_SI_PON_EI = bit(1);
constexpr u32 XTAL_SI_SRAM2RFC = bit(7);
constexpr u32 XTAL_SI_SRAM_DIS = bit(1);
constexpr u32 XTAL_SI_LDO_LPS = mask(6, 4);
constexpr u32 XTAL_SI_LPS_CAP = mask(3, 0);
constexpr u32 R_AX_PMC_DBG_CTRL2 = 0x00CC;
constexpr u32 B_AX_SYSON_DIS_PMCR_AX_WRMSK = bit(2);
constexpr u32 R_AX_SYS_ISO_CTRL = 0x0000;
constexpr u32 B_AX_ISO_EB2CORE = bit(8);
constexpr u32 B_AX_PWC_EV2EF_B15 = bit(15);
constexpr u32 B_AX_PWC_EV2EF_B14 = bit(14);
constexpr u32 R_AX_SPS_DIG_ON_CTRL0 = 0x0200;
constexpr u32 B_AX_VOL_L1_MASK = mask(3, 0);
constexpr u32 B_AX_VREFPFM_L_MASK = mask(25, 22);
constexpr u32 R_AX_HCI_LDO_CTRL = 0x007A;
constexpr u32 B_AX_R_AX_VADJ_MASK = mask(3, 0);
constexpr u32 R_AX_DMAC_FUNC_EN = 0x8400;
constexpr u32 B_AX_MAC_FUNC_EN = bit(30);
constexpr u32 B_AX_DMAC_FUNC_EN = bit(29);
constexpr u32 B_AX_MPDU_PROC_EN = bit(28);
constexpr u32 B_AX_WD_RLS_EN = bit(27);
constexpr u32 B_AX_DLE_WDE_EN = bit(26);
constexpr u32 B_AX_TXPKT_CTRL_EN = bit(25);
constexpr u32 B_AX_STA_SCH_EN = bit(24);
constexpr u32 B_AX_DLE_PLE_EN = bit(23);
constexpr u32 B_AX_PKT_BUF_EN = bit(22);
constexpr u32 B_AX_DMAC_TBL_EN = bit(21);
constexpr u32 B_AX_PKT_IN_EN = bit(20);
constexpr u32 B_AX_DLE_CPUIO_EN = bit(19);
constexpr u32 B_AX_DISPATCHER_EN = bit(18);
constexpr u32 B_AX_BBRPT_EN = bit(17);
constexpr u32 B_AX_MAC_SEC_EN = bit(16);
constexpr u32 B_AX_DMACREG_GCKEN = bit(15);
constexpr u32 R_AX_CMAC_FUNC_EN = 0xC000;
constexpr u32 B_AX_CMAC_EN = bit(30);
constexpr u32 B_AX_CMAC_TXEN = bit(29);
constexpr u32 B_AX_CMAC_RXEN = bit(28);
constexpr u32 B_AX_FORCE_CMACREG_GCKEN = bit(15);
constexpr u32 B_AX_PHYINTF_EN = bit(5);
constexpr u32 B_AX_CMAC_DMA_EN = bit(4);
constexpr u32 B_AX_PTCLTOP_EN = bit(3);
constexpr u32 B_AX_SCHEDULER_EN = bit(2);
constexpr u32 B_AX_TMAC_EN = bit(1);
constexpr u32 B_AX_RMAC_EN = bit(0);
constexpr u32 R_AX_EECS_EESK_FUNC_SEL = 0x02D8;
constexpr u32 B_AX_PINMUX_EESK_FUNC_SEL_MASK = mask(7, 4);
constexpr u32 PINMUX_EESK_FUNC_SEL_BT_LOG = 0x1;
constexpr u32 XTAL_SI_RF00 = bit(0);
constexpr u32 XTAL_SI_RF10 = bit(0);
constexpr u32 R_AX_WLRF_CTRL = 0x02F0;
constexpr u32 B_AX_AFC_AFEDIG = bit(17);
constexpr u32 R_AX_SYS_FUNC_EN = 0x0002;
constexpr u32 B_AX_FEN_BB_GLB_RSTN = bit(1);
constexpr u32 B_AX_FEN_BBRSTB = bit(0);
constexpr u32 B_AX_APFM_OFFMAC = bit(9);
constexpr u32 SW_LPS_OPTION = 0x0001A0B2;
constexpr u32 R_AX_SYS_SWR_CTRL1 = 0x0010;
constexpr u32 B_AX_SYM_CTRL_SPS_PWMFREQ = bit(10);
constexpr u32 B_AX_REG_ZCDC_H_MASK = mask(18, 17);
constexpr u32 XTAL_SC_XI_MASK = mask(7, 0);
constexpr u32 XTAL_SC_XO_MASK = mask(7, 0);
constexpr u32 XTAL_SI_SMALL_PWR_CUT = bit(0);
constexpr u32 XTAL_SI_BIG_PWR_CUT = bit(1);
constexpr u32 XTAL_SI_DRV_LATCH = bit(4);
constexpr u32 XTAL_SI_ACV_MASK = mask(3, 0);
constexpr u32 XTAL_SI_LOW_ADDR_MASK = mask(7, 0);
constexpr u32 XTAL_SI_MODE_SEL_MASK = mask(7, 6);
constexpr u32 XTAL_SI_RDY = bit(5);
constexpr u32 XTAL_SI_HIGH_ADDR_MASK = mask(2, 0);
constexpr u32 XTAL_SI_RF00S_EN = mask(2, 0);
constexpr u32 XTAL_SI_RF10S_EN = mask(2, 0);
constexpr u32 FULL_BIT_MASK = mask(7, 0);
constexpr u32 R_AX_IC_PWR_STATE = 0x03F0;
constexpr u32 B_AX_WLMAC_PWR_STE_MASK = mask(9, 8);
constexpr u32 R_AX_WLAN_XTAL_SI_CTRL = 0x0270;
constexpr u32 B_AX_WL_XTAL_SI_CMD_POLL = bit(31);
constexpr u32 R_AX_SCOREBOARD = 0x00AC;
constexpr u32 MAC_AX_NOTIFY_TP_MAJOR = 0x81;
constexpr u32 MAC_AX_NOTIFY_PWR_MAJOR = 0x80;
constexpr u32 XTAL_SI_NORMAL_READ = 0x01;
constexpr u32 XTAL_SI_NORMAL_WRITE = 0x00;
} }
