// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 d1fced1b8a741dc9f92b47c69489c24385945f6e; BSD option. Do not edit.
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace power {
using u8=uint8_t;using s8=int8_t;using u32=uint32_t;
enum rtw89_band {
	RTW89_BAND_2G = 0,
	RTW89_BAND_5G = 1,
	RTW89_BAND_6G = 2,
	RTW89_BAND_NUM,
};
enum rtw89_bandwidth {
	RTW89_CHANNEL_WIDTH_20	= 0,
	RTW89_CHANNEL_WIDTH_40	= 1,
	RTW89_CHANNEL_WIDTH_80	= 2,
	RTW89_CHANNEL_WIDTH_160	= 3,
	RTW89_CHANNEL_WIDTH_320	= 4,

	/* keep index order above */
	RTW89_CHANNEL_WIDTH_ORDINARY_NUM = 5,

	RTW89_CHANNEL_WIDTH_80_80 = 5,
	RTW89_CHANNEL_WIDTH_5 = 6,
	RTW89_CHANNEL_WIDTH_10 = 7,
};
enum rtw89_rate_section {
	RTW89_RS_CCK,
	RTW89_RS_OFDM,
	RTW89_RS_MCS, /* for HT/VHT/HE */
	RTW89_RS_HEDCM,
	RTW89_RS_OFFSET,
	RTW89_RS_NUM,
	RTW89_RS_LMT_NUM = RTW89_RS_MCS + 1,
	RTW89_RS_TX_SHAPE_NUM = RTW89_RS_OFDM + 1,
};
enum rtw89_rate_offset_indexes {
	RTW89_RATE_OFFSET_HE,
	RTW89_RATE_OFFSET_VHT,
	RTW89_RATE_OFFSET_HT,
	RTW89_RATE_OFFSET_OFDM,
	RTW89_RATE_OFFSET_CCK,
	RTW89_RATE_OFFSET_DLRU_EHT,
	RTW89_RATE_OFFSET_DLRU_HE,
	RTW89_RATE_OFFSET_EHT,
	__RTW89_RATE_OFFSET_NUM,

	RTW89_RATE_OFFSET_NUM_AX = RTW89_RATE_OFFSET_CCK + 1,
	RTW89_RATE_OFFSET_NUM_BE = RTW89_RATE_OFFSET_EHT + 1,
};
enum rtw89_rate_num {
	RTW89_RATE_CCK_NUM	= 4,
	RTW89_RATE_OFDM_NUM	= 8,
	RTW89_RATE_HEDCM_NUM	= 4, /* for HEDCM MCS0/1/3/4 */

	RTW89_RATE_MCS_NUM_AX	= 12,
	RTW89_RATE_MCS_NUM_BE	= 16,
	__RTW89_RATE_MCS_NUM	= 16,
};
enum rtw89_nss {
	RTW89_NSS_1		= 0,
	RTW89_NSS_2		= 1,
	/* HE DCM only support 1ss and 2ss */
	RTW89_NSS_HEDCM_NUM	= RTW89_NSS_2 + 1,
	RTW89_NSS_3		= 2,
	RTW89_NSS_4		= 3,
	RTW89_NSS_NUM,
};
enum rtw89_ntx {
	RTW89_1TX	= 0,
	RTW89_2TX	= 1,
	RTW89_NTX_NUM,
};
enum rtw89_beamforming_type {
	RTW89_NONBF	= 0,
	RTW89_BF	= 1,
	RTW89_BF_NUM,
};
enum rtw89_regulation_type {
	RTW89_WW	= 0,
	RTW89_ETSI	= 1,
	RTW89_FCC	= 2,
	RTW89_MKK	= 3,
	RTW89_NA	= 4,
	RTW89_IC	= 5,
	RTW89_KCC	= 6,
	RTW89_ACMA	= 7,
	RTW89_NCC	= 8,
	RTW89_MEXICO	= 9,
	RTW89_CHILE	= 10,
	RTW89_UKRAINE	= 11,
	RTW89_CN	= 12,
	RTW89_QATAR	= 13,
	RTW89_UK	= 14,
	RTW89_THAILAND	= 15,
	RTW89_REGD_NUM,
};
enum rtw89_ru_bandwidth {
	RTW89_RU26 = 0,
	RTW89_RU52 = 1,
	RTW89_RU106 = 2,
	RTW89_RU52_26 = 3,
	RTW89_RU106_26 = 4,
	RTW89_RU_NUM,
};
enum rtw89_bandwidth_section_num_ax {
	RTW89_BW20_SEC_NUM_AX = 8,
	RTW89_BW40_SEC_NUM_AX = 4,
	RTW89_BW80_SEC_NUM_AX = 2,
};
constexpr u32 R_AX_PWR_BY_RATE = 0xd2c0;
constexpr u32 R_AX_PWR_RATE_OFST_CTRL = 0xd204;
constexpr u32 R_AX_PWR_LMT = 0xd2ec;
constexpr u32 R_AX_PWR_RU_LMT = 0xd33c;
constexpr u32 R_AX_PWR_RATE_CTRL = 0xd200;
constexpr u32 B_AX_PWR_REF = 0xffffc00;
constexpr u32 R_TXFIR0 = 0x2300;
constexpr u32 R_DCFO_OPT = 0x4494;
constexpr u32 B_TXSHAPE_TRIANGULAR_CFG = 0x3000000;
constexpr u32 B_DPD_TSSI_CW = 0x7fc0000;
constexpr u32 B_DPD_PWR_CW = 0x3fe00;
constexpr u32 B_DPD_REF = 0x1ff;
constexpr unsigned RTW89_RU_SEC_NUM_AX = 8;
constexpr unsigned RTW89_TXPWR_LMT_PAGE_SIZE_AX = 40;
constexpr unsigned RTW89_TXPWR_LMT_RU_PAGE_SIZE_AX = 24;
struct rtw89_txpwr_limit_ax {
	s8 cck_20m[RTW89_BF_NUM];
	s8 cck_40m[RTW89_BF_NUM];
	s8 ofdm[RTW89_BF_NUM];
	s8 mcs_20m[RTW89_BW20_SEC_NUM_AX][RTW89_BF_NUM];
	s8 mcs_40m[RTW89_BW40_SEC_NUM_AX][RTW89_BF_NUM];
	s8 mcs_80m[RTW89_BW80_SEC_NUM_AX][RTW89_BF_NUM];
	s8 mcs_160m[RTW89_BF_NUM];
	s8 mcs_40m_0p5[RTW89_BF_NUM];
	s8 mcs_40m_2p5[RTW89_BF_NUM];
};
struct rtw89_txpwr_limit_ru_ax {
	s8 ru26[RTW89_RU_SEC_NUM_AX];
	s8 ru52[RTW89_RU_SEC_NUM_AX];
	s8 ru106[RTW89_RU_SEC_NUM_AX];
};
extern const s8 tx_shape_lmt[96];
constexpr unsigned tx_shape_lmt_dimensions[] = {3,2,16};
extern const s8 tx_shape_lmt_ru[48];
constexpr unsigned tx_shape_lmt_ru_dimensions[] = {3,16};
extern const s8 txpwr_lmt_2g[5376];
constexpr unsigned txpwr_lmt_2g_dimensions[] = {2,2,3,2,16,14};
extern const s8 txpwr_lmt_5g[40704];
constexpr unsigned txpwr_lmt_5g_dimensions[] = {4,2,3,2,16,53};
extern const s8 txpwr_lmt_ru_2g[2240];
constexpr unsigned txpwr_lmt_ru_2g_dimensions[] = {5,2,16,14};
extern const s8 txpwr_lmt_ru_5g[8480];
constexpr unsigned txpwr_lmt_ru_5g_dimensions[] = {5,2,16,53};
extern const s8 byrate[240];
extern const u32 dfir[3][8];
inline bool macAddress(u32 a){return a==R_AX_PWR_RATE_CTRL||a==R_AX_PWR_RATE_OFST_CTRL||(!(a&3)&&((a>=R_AX_PWR_BY_RATE&&a<R_AX_PWR_BY_RATE+44)||(a>=R_AX_PWR_LMT&&a<R_AX_PWR_LMT+80)||(a>=R_AX_PWR_RU_LMT&&a<R_AX_PWR_RU_LMT+48)));}
} }
