// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 d1fced1b8a741dc9f92b47c69489c24385945f6e; BSD option. Do not edit.
#pragma once
#include "Rtw8852bRfkConstants.hpp"
namespace rtl8852be { namespace channel {
using namespace rfk;
constexpr u32 R_AX_WMAC_RFMOD = 0xC010;
constexpr u32 R_AX_TX_SUB_CARRIER_VALUE = 0xC088;
constexpr u32 R_AX_TXRATE_CHK = 0xC628;
constexpr u32 B_AX_WMAC_RFMOD_MASK = mask(1, 0);
constexpr u32 B_AX_BAND_MODE = bit(4);
constexpr u32 B_AX_CHECK_CCK_EN = bit(0);
constexpr u32 B_AX_RTS_LIMIT_IN_OFDM6 = bit(1);
constexpr u32 R_RXSCOBC = 0x23B0;
constexpr u32 B_RXSCOBC_TH = mask(18, 0);
constexpr u32 R_RXSCOCCK = 0x23B4;
constexpr u32 B_RXSCOCCK_TH = mask(18, 0);
constexpr u32 LNA_GAIN_NUM = 7;
constexpr u32 TIA_GAIN_NUM = 2;
constexpr u32 B_P0_RPL1_20_MASK = mask(15, 8);
constexpr u32 B_P0_RPL1_40_MASK = mask(23, 16);
constexpr u32 B_P0_RPL1_41_MASK = mask(31, 24);
constexpr u32 B_P0_RPL1_SHIFT = 8;
constexpr u32 B_P0_RPL1_MASK = (B_P0_RPL1_41_MASK | B_P0_RPL1_40_MASK | B_P0_RPL1_20_MASK);
constexpr u32 B_P0_RTL2_42_MASK = mask(7, 0);
constexpr u32 B_P0_RTL2_80_MASK = mask(15, 8);
constexpr u32 B_P0_RTL2_81_MASK = mask(23, 16);
constexpr u32 B_P0_RTL2_8A_MASK = mask(31, 24);
constexpr u32 R_P0_RPL2 = 0x49B4;
constexpr u32 R_P1_RPL2 = 0x4A04;
constexpr u32 B_P0_RTL3_82_MASK = mask(7, 0);
constexpr u32 B_P0_RTL3_83_MASK = mask(15, 8);
constexpr u32 B_P0_RTL3_84_MASK = mask(23, 16);
constexpr u32 B_P0_RTL3_89_MASK = mask(31, 24);
constexpr u32 R_P0_RPL3 = 0x49B8;
constexpr u32 R_P1_RPL3 = 0x4A08;
constexpr u32 R_PATH0_BAND_SEL_V1 = 0x4738;
constexpr u32 B_PATH0_BAND_SEL_MSK_V1 = bit(17);
constexpr u32 R_PATH1_BAND_SEL_V1 = 0x4AA4;
constexpr u32 B_PATH1_BAND_SEL_MSK_V1 = bit(17);
constexpr u32 B_FC0_BW_INV = mask(6, 0);
constexpr u32 R_TXFIR0 = 0x2300;
constexpr u32 B_TXFIR_C01 = mask(23, 0);
constexpr u32 R_TXFIR2 = 0x2304;
constexpr u32 B_TXFIR_C23 = mask(23, 0);
constexpr u32 R_TXFIR4 = 0x2308;
constexpr u32 B_TXFIR_C45 = mask(23, 0);
constexpr u32 R_TXFIR6 = 0x230c;
constexpr u32 B_TXFIR_C67 = mask(23, 0);
constexpr u32 R_TXFIR8 = 0x2310;
constexpr u32 B_TXFIR_C89 = mask(23, 0);
constexpr u32 R_TXFIRA = 0x2314;
constexpr u32 B_TXFIR_CAB = mask(23, 0);
constexpr u32 R_TXFIRC = 0x2318;
constexpr u32 B_TXFIR_CCD = mask(23, 0);
constexpr u32 R_TXFIRE = 0x231c;
constexpr u32 B_TXFIR_CEF = mask(23, 0);
constexpr u32 B_FC0_BW_SET = mask(31, 30);
constexpr u32 B_CHBW_MOD_SBW = mask(13, 12);
constexpr u32 B_CHBW_MOD_PRICH = mask(11, 8);
constexpr u32 R_P0_RFMODE_ORI_RX = 0x12AC;
constexpr u32 B_P0_RFMODE_ORI_RX_ALL = mask(23, 12);
constexpr u32 R_P1_RFMODE_ORI_RX = 0x32AC;
constexpr u32 B_P1_RFMODE_ORI_RX_ALL = mask(23, 12);
constexpr u32 R_RXSC = 0x237C;
constexpr u32 B_RXSC_EN = bit(0);
constexpr u32 B_ENABLE_CCK = bit(5);
constexpr u32 R_PATH0_5MDET_V1 = 0x46F8;
constexpr u32 B_PATH0_5MDET_EN = bit(12);
constexpr u32 R_PATH1_5MDET_V1 = 0x47B8;
constexpr u32 B_PATH1_5MDET_EN = bit(12);
constexpr u32 R_ASSIGN_SBD_OPT_V1 = 0x4440;
constexpr u32 B_ASSIGN_SBD_OPT_EN_V1 = bit(31);
constexpr u32 B_PATH0_5MDET_TH = mask(5, 0);
constexpr u32 B_PATH0_5MDET_SB2 = bit(8);
constexpr u32 B_PATH0_5MDET_SB0 = bit(6);
constexpr u32 B_PATH1_5MDET_TH = mask(5, 0);
constexpr u32 B_PATH1_5MDET_SB2 = bit(8);
constexpr u32 B_PATH1_5MDET_SB0 = bit(6);
constexpr u32 R_S0_HW_SI_DIS = 0x1200;
constexpr u32 B_S0_HW_SI_DIS_W_R_TRIG = mask(30, 28);
constexpr u32 R_S1_HW_SI_DIS = 0x3200;
constexpr u32 B_S1_HW_SI_DIS_W_R_TRIG = mask(30, 28);
constexpr u32 R_PKT_CTRL = 0x47D4;
constexpr u32 B_PKT_POP_EN = bit(8);
constexpr u32 R_MAC_PIN_SEL = 0x0734;
constexpr u32 B_CH_IDX_SEG0 = mask(23, 16);
constexpr u32 R_ADC_FIFO = 0x20fc;
constexpr u32 B_ADC_FIFO_RST = mask(31, 24);
constexpr u32 RR_CFGCH_V1 = 0x10018;
constexpr u32 INV_RF_DATA = 0xffffffff;
constexpr u32 RR_CFGCH_BW = mask(11, 10);
constexpr u32 CFGCH_BW_20M = 3;
constexpr u32 CFGCH_BW_40M = 2;
constexpr u32 CFGCH_BW_80M = 1;
constexpr u32 RR_CFGCH_POW_LCK = bit(15);
constexpr u32 RR_CFGCH_TRX_AH = bit(14);
constexpr u32 RR_CFGCH_BCN = bit(13);
constexpr u32 RR_CFGCH_BW2 = bit(12);
constexpr u32 RR_LDO = 0xb1;
constexpr u32 RR_LDO_SEL = mask(8, 6);
constexpr u32 RR_LPF = 0xb7;
constexpr u32 RR_LPF_BUSY = bit(8);
constexpr u32 RR_SYNFB = 0xc5;
constexpr u32 RR_SYNFB_LK = bit(15);
constexpr u32 RR_MMD = 0xd5;
constexpr u32 RR_MMD_RST_EN = bit(8);
constexpr u32 RR_MMD_RST_SYN = bit(6);
constexpr u32 RR_LCK_TRG = 0xd3;
constexpr u32 RR_LCK_TRGSEL = bit(8);
constexpr u32 RR_POW = 0xa0;
constexpr u32 RR_SX = 0xaf;
constexpr u32 RR_SYNLUT = 0xdd;
constexpr u32 RR_SYNLUT_MOD = bit(4);
constexpr u32 RR_POW_SYN = mask(3, 2);
constexpr u32 RR_VCO = 0xb2;
constexpr u32 RR_CFGCH_BAND1 = mask(17, 16);
constexpr u32 RR_CFGCH_BAND0 = mask(9, 8);
constexpr u32 RR_CFGCH_CH = mask(7, 0);
constexpr u32 CFGCH_BAND1_5G = 1;
constexpr u32 CFGCH_BAND0_5G = 1;
constexpr u32 RR_LCKST = 0xcf;
constexpr u32 RR_LCKST_BIN = bit(0);
constexpr u32 RR_LUTWE2 = 0xee;
constexpr u32 RR_LUTWE2_RTXBW = bit(2);
constexpr u32 RR_LUTWA_M2 = mask(4, 0);
constexpr u32 RR_LUTWD0_LB = mask(5, 0);
constexpr u32 RTW89_CH_BASE_IDX_MASK = mask(7, 4);
constexpr u32 RTW89_CH_BASE_IDX_2G = 0;
constexpr u32 RTW89_CH_OFFSET_MASK = mask(3, 0);
constexpr u32 RTW89_CH_BASE_IDX_5G_FIRST = 2;
constexpr u32 RTW89_CH_BASE_IDX_5G_LAST = 5;
constexpr u32 RTW89_CH_BASE_IDX_6G_FIRST = 7;
constexpr u32 RTW89_CH_BASE_IDX_6G_LAST = 14;
constexpr u32 R_AX_PPDU_STAT = 0xCE40;
constexpr u32 B_AX_PPDU_STAT_RPT_EN = bit(0);
constexpr u32 B_AX_APP_MAC_INFO_RPT = bit(1);
constexpr u32 B_AX_APP_RX_CNT_RPT = bit(2);
constexpr u32 B_AX_APP_PLCP_HDR_RPT = bit(3);
constexpr u32 B_AX_PPDU_STAT_RPT_CRC32 = bit(5);
constexpr u32 R_AX_HW_RPT_FWD = 0x9C18;
constexpr u32 B_AX_FWD_PPDU_STAT_MASK = mask(1, 0);
constexpr u32 RTW89_PRPT_DEST_HOST = 1;
enum rtw89_mac_idx {
	RTW89_MAC_0 = 0,
	RTW89_MAC_1 = 1,
	RTW89_MAC_NUM,
};
enum rtw89_sc_offset {
	RTW89_SC_DONT_CARE	= 0,
	RTW89_SC_20_UPPER	= 1,
	RTW89_SC_20_LOWER	= 2,
	RTW89_SC_20_UPMOST	= 3,
	RTW89_SC_20_LOWEST	= 4,
	RTW89_SC_20_UP2X	= 5,
	RTW89_SC_20_LOW2X	= 6,
	RTW89_SC_20_UP3X	= 7,
	RTW89_SC_20_LOW3X	= 8,
	RTW89_SC_40_UPPER	= 9,
	RTW89_SC_40_LOWER	= 10,
};
enum rtw89_phy_bb_gain_band {
	RTW89_BB_GAIN_BAND_2G = 0,
	RTW89_BB_GAIN_BAND_5G_L = 1,
	RTW89_BB_GAIN_BAND_5G_M = 2,
	RTW89_BB_GAIN_BAND_5G_H = 3,
	RTW89_BB_GAIN_BAND_6G_L = 4,
	RTW89_BB_GAIN_BAND_6G_M = 5,
	RTW89_BB_GAIN_BAND_6G_H = 6,
	RTW89_BB_GAIN_BAND_6G_UH = 7,

	RTW89_BB_GAIN_BAND_NR,
};
enum rtw89_mac_hwmod_sel {
	RTW89_DMAC_SEL = 0,
	RTW89_CMAC_SEL = 1,

	RTW89_MAC_INVALID,
};
struct rtw8852b_bb_gain {
	u32 gain_g[BB_PATH_NUM_8852B];
	u32 gain_a[BB_PATH_NUM_8852B];
	u32 gain_mask;
};
struct rtw89_channel_help_params {
	u32 tx_en;
};
static const u32 rtw8852b_sco_barker_threshold[14] = {
	0x1cfea, 0x1d0e1, 0x1d1d7, 0x1d2cd, 0x1d3c3, 0x1d4b9, 0x1d5b0, 0x1d6a6,
	0x1d79c, 0x1d892, 0x1d988, 0x1da7f, 0x1db75, 0x1ddc4
};
static const u32 rtw8852b_sco_cck_threshold[14] = {
	0x27de3, 0x27f35, 0x28088, 0x281da, 0x2832d, 0x2847f, 0x285d2, 0x28724,
	0x28877, 0x289c9, 0x28b1c, 0x28c6e, 0x28dc1, 0x290ed
};
static const struct rtw8852b_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
	{ {0x4678, 0x475C}, {0x45DC, 0x4740},
	  0x00ff0000 },
	{ {0x4678, 0x475C}, {0x45DC, 0x4740},
	  0xff000000 },
	{ {0x467C, 0x4760}, {0x4660, 0x4744},
	  0x000000ff },
	{ {0x467C, 0x4760}, {0x4660, 0x4744},
	  0x0000ff00 },
	{ {0x467C, 0x4760}, {0x4660, 0x4744},
	  0x00ff0000 },
	{ {0x467C, 0x4760}, {0x4660, 0x4744},
	  0xff000000 },
	{ {0x4680, 0x4764}, {0x4664, 0x4748},
	  0x000000ff },
};
static const struct rtw8852b_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ {0x4680, 0x4764}, {0x4664, 0x4748},
	  0x00ff0000 },
	{ {0x4680, 0x4764}, {0x4664, 0x4748},
	  0xff000000 },
};
static
const u8 rtw89_ch_base_table[16] = {1, 0xff,
				    36, 100, 132, 149, 0xff,
				    1, 33, 65, 97, 129, 161, 193, 225, 0xff};
inline bool macByteAddress(u32 a){return a==R_AX_WMAC_RFMOD||a==R_AX_TXRATE_CHK;}
inline bool macWordAddress(u32 a,bool write){return a==R_AX_TX_SUB_CARRIER_VALUE||a==R_AX_PPDU_STAT||a==R_AX_HW_RPT_FWD||(!write&&a==R_AX_CMAC_FUNC_EN);}
} }
