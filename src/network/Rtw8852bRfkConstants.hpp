// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 by tools/import_rfk_reference.py.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace rfk {
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s8=int8_t;using s16=int16_t;using s32=int32_t;
constexpr u32 bit(unsigned n){return u32(1)<<n;}
constexpr u32 mask(unsigned hi,unsigned lo){return (u32(0xffffffff)>>(31-hi))&(u32(0xffffffff)<<lo);}
constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}
constexpr u32 fieldGet(u32 m,u32 v){return (v&m)>>shift(m);}
constexpr u32 fieldPrep(u32 m,u32 v){return (v<<shift(m))&m;}
constexpr s32 clampS32(s32 v,s32 low,s32 high){return v<low?low:v>high?high:v;}
constexpr u32 tssiExtraGroup(u32 n){return bit(31)|n;}
constexpr bool isTssiExtraGroup(u32 n){return n&bit(31);}
constexpr u32 tssiExtraIndex1(u32 n){return n&~bit(31);}
constexpr u32 tssiExtraIndex2(u32 n){return tssiExtraIndex1(n)+1;}
inline u32 thermalWord(const s8 *p,unsigned i){u32 v=0;for(unsigned j=0;j<4;++j)v|=u32(u8(p[i+j]))<<(j*8);return v;}
constexpr s32 sign_extend32(u32 v,unsigned sign){return (v&(u32(1)<<sign))?s32(v&((u32(1)<<sign)-1))-s32(u32(1)<<sign):s32(v);}
template<class T,size_t N> constexpr size_t arraySize(const T (&)[N]){return N;}
enum rtw89_rf_path {
	RF_PATH_A = 0,
	RF_PATH_B = 1,
	RF_PATH_C = 2,
	RF_PATH_D = 3,
	RF_PATH_AB,
	RF_PATH_AC,
	RF_PATH_AD,
	RF_PATH_BC,
	RF_PATH_BD,
	RF_PATH_CD,
	RF_PATH_ABC,
	RF_PATH_ABD,
	RF_PATH_ACD,
	RF_PATH_BCD,
	RF_PATH_ABCD,
};
enum rtw89_rf_path_bit {
	RF_A	= bit(0),
	RF_B	= bit(1),
	RF_C	= bit(2),
	RF_D	= bit(3),

	RF_AB	= (RF_A | RF_B),
	RF_AC	= (RF_A | RF_C),
	RF_AD	= (RF_A | RF_D),
	RF_BC	= (RF_B | RF_C),
	RF_BD	= (RF_B | RF_D),
	RF_CD	= (RF_C | RF_D),

	RF_ABC	= (RF_A | RF_B | RF_C),
	RF_ABD	= (RF_A | RF_B | RF_D),
	RF_ACD	= (RF_A | RF_C | RF_D),
	RF_BCD	= (RF_B | RF_C | RF_D),

	RF_ABCD	= (RF_A | RF_B | RF_C | RF_D),
};
enum rtw89_phy_idx {
	RTW89_PHY_0 = 0,
	RTW89_PHY_1 = 1,
	RTW89_PHY_MAX
};
enum rtw89_sub_entity_idx {
	RTW89_SUB_ENTITY_0 = 0,
	RTW89_SUB_ENTITY_1 = 1,

	NUM_OF_RTW89_SUB_ENTITY,
	RTW89_SUB_ENTITY_IDLE = NUM_OF_RTW89_SUB_ENTITY,
};
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
enum rtw89_subband {
	RTW89_CH_2G = 0,
	RTW89_CH_5G_BAND_1 = 1,
	/* RTW89_CH_5G_BAND_2 = 2, unused */
	RTW89_CH_5G_BAND_3 = 3,
	RTW89_CH_5G_BAND_4 = 4,

	RTW89_CH_6G_BAND_IDX0, /* Low */
	RTW89_CH_6G_BAND_IDX1, /* Low */
	RTW89_CH_6G_BAND_IDX2, /* Mid */
	RTW89_CH_6G_BAND_IDX3, /* Mid */
	RTW89_CH_6G_BAND_IDX4, /* High */
	RTW89_CH_6G_BAND_IDX5, /* High */
	RTW89_CH_6G_BAND_IDX6, /* Ultra-high */
	RTW89_CH_6G_BAND_IDX7, /* Ultra-high */

	RTW89_SUBBAND_NR,
	RTW89_SUBBAND_2GHZ_5GHZ_NR = RTW89_CH_5G_BAND_4 + 1,
};
enum rtw89_gain_offset {
	RTW89_GAIN_OFFSET_2G_CCK,
	RTW89_GAIN_OFFSET_2G_OFDM,
	RTW89_GAIN_OFFSET_5G_LOW,
	RTW89_GAIN_OFFSET_5G_MID,
	RTW89_GAIN_OFFSET_5G_HIGH,
	RTW89_GAIN_OFFSET_6G_L0,
	RTW89_GAIN_OFFSET_6G_L1,
	RTW89_GAIN_OFFSET_6G_M0,
	RTW89_GAIN_OFFSET_6G_M1,
	RTW89_GAIN_OFFSET_6G_H0,
	RTW89_GAIN_OFFSET_6G_H1,
	RTW89_GAIN_OFFSET_6G_UH0,
	RTW89_GAIN_OFFSET_6G_UH1,

	RTW89_GAIN_OFFSET_NR,
};
enum rtw89_tssi_alimk_band {
	TSSI_ALIMK_2G = 0,
	TSSI_ALIMK_5GL,
	TSSI_ALIMK_5GM,
	TSSI_ALIMK_5GH,
	TSSI_ALIMK_MAX
};
enum rtw8852b_iqk_type {
	ID_TXAGC = 0x0,
	ID_FLOK_COARSE = 0x1,
	ID_FLOK_FINE = 0x2,
	ID_TXK = 0x3,
	ID_RXAGC = 0x4,
	ID_RXK = 0x5,
	ID_NBTXK = 0x6,
	ID_NBRXK = 0x7,
	ID_FLOK_VBUFFER = 0x8,
	ID_A_FLOK_COARSE = 0x9,
	ID_G_FLOK_COARSE = 0xa,
	ID_A_FLOK_FINE = 0xb,
	ID_G_FLOK_FINE = 0xc,
	ID_IQK_RESTORE = 0x10,
};
enum rtw8852b_pmac_mode {
	NONE_TEST,
	PKTS_TX,
	PKTS_RX,
	CONT_TX
};
enum btc_wl_rfk_type {
	BTC_WRFKT_IQK = 0,
	BTC_WRFKT_LCK = 1,
	BTC_WRFKT_DPK = 2,
	BTC_WRFKT_TXGAPK = 3,
	BTC_WRFKT_DACK = 4,
	BTC_WRFKT_RXDCK = 5,
	BTC_WRFKT_TSSI = 6,
	BTC_WRFKT_CHLK = 7,
};
enum btc_wl_rfk_state {
	BTC_WRFK_STOP = 0,
	BTC_WRFK_START = 1,
	BTC_WRFK_ONESHOT_START = 2,
	BTC_WRFK_ONESHOT_STOP = 3,
};
constexpr u32 MASKDWORD = 0xffffffff;
constexpr u32 RFREG_MASK = 0xfffff;
constexpr u32 MASKBYTE0 = 0xff;
constexpr u32 R_NCTL_RPT = 0x8008;
constexpr u32 B_NCTL_RPT_FLG = bit(26);
constexpr u32 R_NCTL_N1 = 0x8010;
constexpr u32 RR_DCK1 = 0x93;
constexpr u32 RR_DCK1_CLR = mask(3, 0);
constexpr u32 RR_DCK = 0x92;
constexpr u32 RR_DCK_LV = bit(0);
constexpr u32 RTW8852B_RXDCK_VER = 0x1;
constexpr u32 RF_PATH_NUM_8852B = 2;
constexpr u32 RR_RSV1 = 0x05;
constexpr u32 RR_DCK_FINE = bit(1);
constexpr u32 R_P0_TSSI_TRK = 0x5818;
constexpr u32 B_P0_TSSI_TRK_EN = bit(30);
constexpr u32 RR_RSV1_RST = bit(0);
constexpr u32 RR_MOD = 0x00;
constexpr u32 RR_MOD_MASK = mask(19, 16);
constexpr u32 RR_MOD_V_RX = 0x3;
constexpr u32 RR_RCKC = 0x1b;
constexpr u32 RR_RCKS = 0x1c;
constexpr u32 RR_RCKC_CA = mask(14, 10);
constexpr u32 R_AX_PHYREG_SET = 0x8040;
constexpr u32 R_DRCK_V1 = 0xC0CC;
constexpr u32 B_DRCK_V1_KICK = bit(6);
constexpr u32 R_DRCK_RS = 0xC0D0;
constexpr u32 B_DRCK_RS_DONE = bit(3);
constexpr u32 R_DRCK_FH = 0xC094;
constexpr u32 B_DRCK_LAT = bit(9);
constexpr u32 B_DRCK_RS_LPS = mask(19, 15);
constexpr u32 B_DRCK_V1_SEL = bit(9);
constexpr u32 B_DRCK_V1_CV = mask(4, 0);
constexpr u32 R_ADDCK0 = 0xC0F4;
constexpr u32 B_ADDCK0 = mask(9, 8);
constexpr u32 R_ADDCKR0 = 0xC0FC;
constexpr u32 B_ADDCKR0_A0 = mask(19, 10);
constexpr u32 B_ADDCKR0_A1 = mask(9, 0);
constexpr u32 R_ADDCK1 = 0xC1F4;
constexpr u32 B_ADDCK1 = mask(9, 8);
constexpr u32 R_ADDCKR1 = 0xC1fC;
constexpr u32 B_ADDCKR1_A0 = mask(19, 10);
constexpr u32 B_ADDCKR1_A1 = mask(9, 0);
constexpr u32 R_ADDCK0D = 0xC0F0;
constexpr u32 B_ADDCK0D_VAL = mask(25, 16);
constexpr u32 B_ADDCK0_VAL = mask(3, 0);
constexpr u32 B_ADDCK0D_VAL2 = mask(31, 26);
constexpr u32 B_ADDCK0_MAN = mask(5, 4);
constexpr u32 R_ADDCK1D = 0xC1F0;
constexpr u32 B_ADDCK1D_VAL = mask(25, 16);
constexpr u32 B_ADDCK1D_VAL2 = mask(31, 26);
constexpr u32 B_ADDCK1_MAN = mask(5, 4);
constexpr u32 R_P0_NRBW = 0x12B8;
constexpr u32 B_P0_NRBW_DBG = bit(30);
constexpr u32 RTW89_DACK_MSBK_NR = 16;
constexpr u32 R_DCOF0 = 0xC000;
constexpr u32 B_DCOF0_V = mask(4, 1);
constexpr u32 R_DACK_S0P2 = 0xC05C;
constexpr u32 B_DACK_S0M0 = mask(31, 24);
constexpr u32 R_DCOF8 = 0xC020;
constexpr u32 B_DCOF8_V = mask(4, 1);
constexpr u32 R_DACK_S0P3 = 0xC080;
constexpr u32 B_DACK_S0M1 = mask(31, 24);
constexpr u32 R_DACK_BIAS00 = 0xc048;
constexpr u32 B_DACK_BIAS00 = mask(11, 2);
constexpr u32 R_DACK_BIAS01 = 0xC06C;
constexpr u32 B_DACK_BIAS01 = mask(11, 2);
constexpr u32 R_DACK_DADCK00 = 0xC060;
constexpr u32 B_DACK_DADCK00 = mask(31, 24);
constexpr u32 R_DACK_DADCK01 = 0xC084;
constexpr u32 B_DACK_DADCK01 = mask(31, 24);
constexpr u32 R_P1_DBGMOD = 0x32B8;
constexpr u32 B_P1_DBGMOD_ON = bit(30);
constexpr u32 R_DACK10 = 0xC100;
constexpr u32 B_DACK10 = mask(4, 1);
constexpr u32 R_DACK10S = 0xC15C;
constexpr u32 B_DACK10S = mask(31, 24);
constexpr u32 R_DACK11 = 0xC120;
constexpr u32 B_DACK11 = mask(4, 1);
constexpr u32 R_DACK11S = 0xC180;
constexpr u32 B_DACK11S = mask(31, 24);
constexpr u32 R_DACK_BIAS10 = 0xC148;
constexpr u32 B_DACK_BIAS10 = mask(11, 2);
constexpr u32 R_DACK_BIAS11 = 0xC16C;
constexpr u32 B_DACK_BIAS11 = mask(11, 2);
constexpr u32 R_DACK_DADCK10 = 0xC160;
constexpr u32 B_DACK_DADCK10 = mask(31, 24);
constexpr u32 R_DACK_DADCK11 = 0xC184;
constexpr u32 B_DACK_DADCK11 = mask(31, 24);
constexpr u32 ADDC_T_AVG = 100;
constexpr u32 R_DBG32_D = 0x1730;
constexpr u32 R_PATH1_SAMPL_DLY_T_V1 = 0xC1D4;
constexpr u32 R_ANAPAR = 0x032C;
constexpr u32 B_ANAPAR_ADCCLK = bit(30);
constexpr u32 B_ANAPAR_FLTRST = bit(22);
constexpr u32 R_ANAPAR_PW15 = 0x030C;
constexpr u32 B_ANAPAR_PW15_H = mask(27, 24);
constexpr u32 B_ANAPAR_EN = bit(16);
constexpr u32 R_PATH0_SAMPL_DLY_T_V1 = 0xC0D4;
constexpr u32 B_ADDCK0_TRG = bit(11);
constexpr u32 B_ADDCK1_TRG = bit(11);
constexpr u32 R_DACK_S0P0 = 0xC040;
constexpr u32 B_DACK_S0P0_OK = bit(31);
constexpr u32 R_DACK_S0P1 = 0xC064;
constexpr u32 B_DACK_S0P1_OK = bit(31);
constexpr u32 B_DACK_S0P2_OK = bit(2);
constexpr u32 B_DACK_S0P3_OK = bit(2);
constexpr u32 R_DACK_S1P0 = 0xC140;
constexpr u32 B_DACK_S1P0_OK = bit(31);
constexpr u32 R_DACK_S1P1 = 0xC164;
constexpr u32 B_DACK_S1P1_OK = bit(31);
constexpr u32 B_DACK_S1P2_OK = bit(2);
constexpr u32 B_DACK_S1P3_OK = bit(2);
constexpr u32 RR_MODOPT = 0x01;
constexpr u32 RR_RXK = 0x20;
constexpr u32 RR_RXK_SEL2G = bit(8);
constexpr u32 RR_CFGCH = 0x18;
constexpr u32 RR_RSV4 = 0x1f;
constexpr u32 RR_RXK_SEL5G = bit(7);
constexpr u32 R_P0_RFCTM = 0x5864;
constexpr u32 B_P0_RFCTM_EN = bit(29);
constexpr u32 R_IQK_DIF4 = 0x802C;
constexpr u32 B_IQK_DIF4_TXT = mask(11, 0);
constexpr u32 B_IQK_DIF4_RXT = mask(27, 16);
constexpr u32 R_NCTL_CFG = 0x8000;
constexpr u32 RTW8852B_RXK_GROUP_NR = 4;
constexpr u32 RR_MOD_RGM = mask(13, 4);
constexpr u32 RR_RXBB = 0x83;
constexpr u32 RR_RXBB_C2G = mask(16, 10);
constexpr u32 RR_RXBB_C1G = mask(9, 8);
constexpr u32 RR_RXA2 = 0x8c;
constexpr u32 RR_RXA2_HATT = mask(6, 0);
constexpr u32 RR_RXA2_CC2 = mask(8, 7);
constexpr u32 R_CFIR_LUT = 0x8154;
constexpr u32 B_CFIR_LUT_SEL = bit(8);
constexpr u32 B_CFIR_LUT_SET = bit(4);
constexpr u32 B_CFIR_LUT_GP_V1 = mask(2, 0);
constexpr u32 R_IQKINF = 0x9FE0;
constexpr u32 R_IQK_RES = 0x8124;
constexpr u32 B_IQK_RES_RXCFIR = mask(3, 0);
constexpr u32 RR_RXKPLL = 0x1e;
constexpr u32 R_RXIQC = 0x813c;
constexpr u32 B_ANAPAR_PW15 = mask(31, 24);
constexpr u32 B_ANAPAR_15 = mask(31, 16);
constexpr u32 R_P0_RXCK = 0x12A0;
constexpr u32 B_P0_RXCK_VAL = mask(18, 16);
constexpr u32 B_P0_RXCK_ON = bit(19);
constexpr u32 R_P1_RXCK = 0x32A0;
constexpr u32 B_P1_RXCK_VAL = mask(18, 16);
constexpr u32 B_P1_RXCK_ON = bit(19);
constexpr u32 R_UPD_CLK_ADC = 0x0700;
constexpr u32 B_UPD_CLK_ADC_ON = bit(24);
constexpr u32 B_UPD_CLK_ADC_VAL = mask(26, 25);
constexpr u32 RR_TXIG = 0x11;
constexpr u32 RR_TXIG_GR0 = mask(1, 0);
constexpr u32 RR_TXIG_GR1 = mask(6, 4);
constexpr u32 RR_TXIG_TG = mask(16, 12);
constexpr u32 R_KIP_IQP = 0x81CC;
constexpr u32 B_CFIR_LUT_G2 = bit(2);
constexpr u32 B_CFIR_LUT_GP = mask(1, 0);
constexpr u32 B_NCTL_N1_CIP = mask(7, 0);
constexpr u32 B_IQK_RES_TXCFIR = mask(11, 8);
constexpr u32 R_TXIQC = 0x8138;
constexpr u32 RR_LUTWE = 0xef;
constexpr u32 RR_LUTWA = 0x33;
constexpr u32 RR_LUTWD0 = 0x3f;
constexpr u32 RR_TXVBUF = 0x7c;
constexpr u32 RR_TXVBUF_DACEN = bit(5);
constexpr u32 RR_TXMO = 0x58;
constexpr u32 RR_TXMO_COI = mask(19, 15);
constexpr u32 RR_TXMO_COQ = mask(14, 10);
constexpr u32 RTW89_IQK_CHS_NR = 2;
constexpr u32 RR_LOKVB = 0x0a;
constexpr u32 RR_LOKVB_COI = mask(19, 14);
constexpr u32 RR_LOKVB_COQ = mask(9, 4);
constexpr u32 RR_XALNA2 = 0x90;
constexpr u32 RR_XALNA2_SW2 = mask(9, 8);
constexpr u32 RR_TXG1 = 0x51;
constexpr u32 RR_TXG1_ATT2 = bit(19);
constexpr u32 RR_TXG1_ATT1 = bit(11);
constexpr u32 RR_TXG2 = 0x52;
constexpr u32 RR_TXG2_ATT0 = bit(11);
constexpr u32 RR_TXGA = 0x55;
constexpr u32 RR_TXGA_LOK_EXT = mask(4, 0);
constexpr u32 RR_LUTWE_LOK = bit(2);
constexpr u32 RR_LUTWA_M1 = mask(7, 0);
constexpr u32 RR_MOD_IQK = mask(19, 4);
constexpr u32 RR_XGLNA2 = 0x85;
constexpr u32 RR_XGLNA2_SW = mask(1, 0);
constexpr u32 RR_BIASA = 0x60;
constexpr u32 RR_BIASA_A = mask(2, 0);
constexpr u32 B_IQKINF_FCOR = bit(0);
constexpr u32 B_IQKINF_FFIN = bit(1);
constexpr u32 B_IQKINF_FTX = bit(2);
constexpr u32 B_IQKINF_F_RX = bit(3);
constexpr u32 R_IQKINF2 = 0x9FE8;
constexpr u32 B_IQKINF2_KCNT = mask(15, 8);
constexpr u32 B_IQKINF_FAIL = mask(3, 0);
constexpr u32 B_IQKINF2_FCNT = mask(23, 16);
constexpr u32 R_CIRST = 0x035c;
constexpr u32 B_CIRST_SYN = mask(11, 10);
constexpr u32 B_IQKINF_VER = mask(31, 24);
constexpr u32 RTW8852B_IQK_VER = 0x2a;
constexpr u32 R_IQKCH = 0x9FE4;
constexpr u32 B_IQKCH_BAND = mask(3, 0);
constexpr u32 B_IQKCH_BW = mask(7, 4);
constexpr u32 B_IQKCH_CH = mask(15, 8);
constexpr u32 R_KIP_SYSCFG = 0x8088;
constexpr u32 R_CFIR_SYS = 0x8120;
constexpr u32 B_IQK_RES_K = bit(28);
constexpr u32 R_IQRSN = 0x8220;
constexpr u32 B_IQRSN_K1 = bit(28);
constexpr u32 B_IQRSN_K2 = bit(16);
constexpr u32 RR_BBDC = 0x10005;
constexpr u32 RR_BBDC_SEL = bit(0);
constexpr u32 R_COEF_SEL = 0x8104;
constexpr u32 B_COEF_SEL_IQC = bit(0);
constexpr u32 B_CFIR_LUT_G3 = bit(3);
constexpr u32 RTW8852B_IQK_SS = 2;
constexpr u32 RF_PATH_MAX = 4;
constexpr u32 R_DPD_BF = 0x44a0;
constexpr u32 B_DPD_BF_OFDM = mask(16, 12);
constexpr u32 B_DPD_BF_SCA = mask(6, 0);
constexpr u32 R_DPD_CH0A = 0x81BC;
constexpr u32 B_DPD_CFG = mask(22, 0);
constexpr u32 RR_TXPOW = 0x7f;
constexpr u32 RR_TXPOW_TXG = bit(1);
constexpr u32 RR_TXPOW_TXA = bit(8);
constexpr u32 R_P0_TMETER = 0x5810;
constexpr u32 B_P0_TMETER_DIS = bit(16);
constexpr u32 B_P0_TMETER_TRK = bit(24);
constexpr u32 B_P0_TMETER = mask(15, 10);
constexpr u32 B_P0_RFCTM_VAL = mask(25, 20);
constexpr u32 R_P0_TSSI_BASE = 0x5C00;
constexpr u32 DELTA_SWINGIDX_SIZE = 30;
constexpr u32 R_P0_RFCTM_RDY = bit(26);
constexpr u32 R_P1_TMETER = 0x7810;
constexpr u32 B_P1_TMETER_DIS = bit(16);
constexpr u32 B_P1_TMETER_TRK = bit(24);
constexpr u32 B_P1_TMETER = mask(15, 10);
constexpr u32 R_P1_RFCTM = 0x7864;
constexpr u32 B_P1_RFCTM_VAL = mask(25, 20);
constexpr u32 R_TSSI_THOF = 0x7C00;
constexpr u32 R_P1_RFCTM_RDY = bit(26);
constexpr u32 R_P0_TSSIC = 0x5814;
constexpr u32 B_P0_TSSIC_BYPASS = bit(11);
constexpr u32 R_P1_TSSIC = 0x7814;
constexpr u32 B_P1_TSSIC_BYPASS = bit(11);
constexpr u32 R_P0_TSSI_MV_AVG = 0x58E4;
constexpr u32 B_P0_TSSI_MV_MIX = mask(19, 11);
constexpr u32 R_P1_TSSI_MV_AVG = 0x78E4;
constexpr u32 B_P1_RFCTM_DEL = mask(19, 11);
constexpr u32 B_P0_TSSI_MV_CLR = bit(14);
constexpr u32 R_P0_TSSI_AVG = 0x5820;
constexpr u32 B_P0_TSSI_EN = bit(31);
constexpr u32 RR_TXGA_V1 = 0x10055;
constexpr u32 RR_TXGA_V1_TRK_EN = bit(7);
constexpr u32 B_P0_TSSI_RFC = mask(28, 27);
constexpr u32 B_P0_TSSI_OFT = mask(7, 0);
constexpr u32 B_P0_TSSI_OFT_EN = bit(28);
constexpr u32 B_P1_TSSI_MV_CLR = bit(14);
constexpr u32 R_P1_TSSI_AVG = 0x7820;
constexpr u32 B_P1_TSSI_EN = bit(31);
constexpr u32 R_P1_TSSI_TRK = 0x7818;
constexpr u32 B_P1_TSSI_RFC = mask(28, 27);
constexpr u32 B_P1_TSSI_OFT = mask(7, 0);
constexpr u32 B_P1_TSSI_OFT_EN = bit(28);
constexpr u32 _TSSI_DE_MASK = mask(21, 12);
constexpr u32 R_TSSI_PA_K1 = 0x5600;
constexpr u32 R_TSSI_PA_K2 = 0x5604;
constexpr u32 R_P0_TSSI_ALIM1 = 0x5630;
constexpr u32 R_P0_TSSI_ALIM3 = 0x5634;
constexpr u32 R_TSSI_PA_K5 = 0x5638;
constexpr u32 R_P0_TSSI_ALIM2 = 0x563c;
constexpr u32 R_P0_TSSI_ALIM4 = 0x5640;
constexpr u32 R_TSSI_PA_K8 = 0x5644;
constexpr u32 RTW8852B_TSSI_PATH_NR = 2;
constexpr u32 R_TX_COUNTER = 0x1A40;
constexpr u32 MASKLWORD = 0x0000ffff;
constexpr u32 B_TSSI_CWRPT_RDY = bit(16);
constexpr u32 B_TSSI_CWRPT = mask(8, 0);
constexpr u32 B_P0_TSSI_AVG = mask(15, 12);
constexpr u32 B_P1_TSSI_AVG = mask(15, 12);
constexpr u32 B_P0_TSSI_MV_AVG = mask(13, 11);
constexpr u32 B_P1_TSSI_MV_AVG = mask(13, 11);
constexpr u32 B_P1_TSSI_ALIM11 = mask(29, 20);
constexpr u32 B_P1_TSSI_ALIM12 = mask(19, 10);
constexpr u32 B_P1_TSSI_ALIM13 = mask(9, 0);
constexpr u32 B_P0_TSSI_ALIM1 = mask(29, 0);
constexpr u32 B_P0_TSSI_ALIM2 = mask(29, 0);
constexpr u32 B_P0_TSSI_ALIM31 = mask(9, 0);
constexpr u32 B_P0_TSSI_ALIM11 = mask(29, 20);
constexpr u32 B_P0_TSSI_ALIM12 = mask(19, 10);
constexpr u32 B_P0_TSSI_ALIM13 = mask(9, 0);
constexpr u32 R_P1_TSSI_ALIM1 = 0x7630;
constexpr u32 B_P1_TSSI_ALIM1 = mask(29, 0);
constexpr u32 R_P1_TSSI_ALIM2 = 0x763c;
constexpr u32 B_P1_TSSI_ALIM2 = mask(29, 0);
constexpr u32 R_P1_TSSI_ALIM3 = 0x7634;
constexpr u32 B_P1_TSSI_ALIM31 = mask(9, 0);
constexpr u32 R_P0_AGC_RSVD = 0x4ACC;
constexpr u32 R_P1_AGC_RSVD = 0x4AD8;
constexpr u32 R_PATH0_G_TIA1_LNA6_OP1DB_V1 = 0x4694;
constexpr u32 R_PATH1_G_TIA1_LNA6_OP1DB_V1 = 0x4778;
constexpr u32 BB_PATH_NUM_8852B = 2;
constexpr u32 B_PATH0_R_G_OFST_MASK = mask(23, 16);
constexpr u32 R_P0_RPL1 = 0x49B0;
constexpr u32 B_P0_RPL1_BIAS_MASK = mask(7, 0);
constexpr u32 R_P1_RPL1 = 0x4A00;
constexpr u32 R_RX_RPL_OFST = 0x23AC;
constexpr u32 B_RX_RPL_OFST_CCK_MASK = mask(6, 0);
constexpr u32 R_PMAC_TX_PRD = 0x09C4;
constexpr u32 B_PMAC_CTX_EN = bit(0);
constexpr u32 B_PMAC_PTX_EN = bit(4);
constexpr u32 B_PMAC_TX_PRD_MSK = mask(31, 8);
constexpr u32 R_PMAC_TX_CNT = 0x09C8;
constexpr u32 B_PMAC_TX_CNT_MSK = mask(31, 0);
constexpr u32 R_PMAC_TX_CTRL = 0x09C0;
constexpr u32 B_PMAC_TXEN_DIS = bit(0);
constexpr u32 R_PD_CTRL = 0x0C3C;
constexpr u32 B_PD_HIT_DIS = bit(9);
constexpr u32 R_RXCCA = 0x2344;
constexpr u32 B_RXCCA_DIS = bit(31);
constexpr u32 R_PMAC_GNT = 0x0980;
constexpr u32 B_PMAC_GNT_TXEN = bit(0);
constexpr u32 B_PMAC_GNT_RXEN = bit(16);
constexpr u32 R_PMAC_RX_CFG1 = 0x0988;
constexpr u32 B_PMAC_OPT1_MSK = mask(11, 0);
constexpr u32 R_RSTB_ASYNC = 0x0704;
constexpr u32 B_RSTB_ASYNC_ALL = bit(1);
constexpr u32 R_MAC_SEL = 0x09A4;
constexpr u32 B_MAC_SEL_PWR_EN = bit(16);
constexpr u32 R_TXPWR = 0x4594;
constexpr u32 B_TXPWR_MSK = mask(30, 22);
constexpr u32 B_MAC_SEL_MOD = mask(4, 2);
constexpr u32 R_TXPATH_SEL = 0x458C;
constexpr u32 B_TXPATH_SEL_MSK = mask(31, 28);
constexpr u32 R_TXNSS_MAP = 0x45B4;
constexpr u32 B_TXNSS_MAP_MSK = mask(20, 17);
constexpr u32 R_PMAC_RXMOD = 0x0994;
constexpr u32 B_PMAC_RXMOD_MSK = mask(7, 4);
constexpr u32 B_MAC_SEL_DPD_EN = bit(10);
constexpr u32 R_CHBW_MOD_V1 = 0x49C4;
constexpr u32 B_ANT_RX_SEG0 = mask(3, 0);
constexpr u32 R_P0_RFMODE = 0x12AC;
constexpr u32 R_P0_RFMODE_FTM_RX = 0x12B0;
constexpr u32 R_P1_RFMODE = 0x32AC;
constexpr u32 R_P1_RFMODE_FTM_RX = 0x32B0;
constexpr u32 R_PATH0_BT_SHARE_V1 = 0x4738;
constexpr u32 B_PATH0_BT_SHARE_V1 = bit(19);
constexpr u32 R_PATH0_BTG_PATH_V1 = 0x4738;
constexpr u32 B_PATH0_BTG_PATH_V1 = bit(22);
constexpr u32 R_PATH1_G_LNA6_OP1DB_V1 = 0x476C;
constexpr u32 B_PATH1_G_LNA6_OP1DB_V1 = mask(31, 24);
constexpr u32 R_PATH1_G_TIA0_LNA6_OP1DB_V1 = 0x4778;
constexpr u32 B_PATH1_G_TIA0_LNA6_OP1DB_V1 = mask(7, 0);
constexpr u32 R_PATH1_BT_SHARE_V1 = 0x4AA4;
constexpr u32 B_PATH1_BT_SHARE_V1 = bit(19);
constexpr u32 R_PATH1_BTG_PATH_V1 = 0x4AA4;
constexpr u32 B_PATH1_BTG_PATH_V1 = bit(22);
constexpr u32 B_PMAC_GNT_P1 = mask(20, 17);
constexpr u32 B_BT_SHARE = bit(14);
constexpr u32 R_FC0_BW_V1 = 0x49C0;
constexpr u32 B_ANT_RX_BT_SEG0 = mask(25, 22);
constexpr u32 R_BT_DYN_DC_EST_EN_V1 = 0x4420;
constexpr u32 B_BT_DYN_DC_EST_EN_MSK = bit(31);
constexpr u32 R_GNT_BT_WGT_EN = 0x0C6C;
constexpr u32 B_GNT_BT_WGT_EN = bit(21);
constexpr u32 B_ANT_RX_1RCCA_SEG0 = mask(17, 14);
constexpr u32 B_ANT_RX_1RCCA_SEG1 = mask(21, 18);
constexpr u32 R_RXHT_MCS_LIMIT = 0x0D18;
constexpr u32 B_RXHT_MCS_LIMIT = mask(9, 8);
constexpr u32 R_RXVHT_MCS_LIMIT = 0x0D18;
constexpr u32 B_RXVHT_MCS_LIMIT = mask(22, 21);
constexpr u32 R_RXHE = 0x0D80;
constexpr u32 B_RXHE_USER_MAX = mask(13, 6);
constexpr u32 B_RXHE_MAX_NSS = mask(16, 14);
constexpr u32 B_RXHETB_MAX_NSS = mask(25, 23);
constexpr u32 B_P0_TXPW_RSTB_MANON = bit(30);
constexpr u32 B_P0_TXPW_RSTB_TSSI = bit(31);
constexpr u32 B_P1_TXPW_RSTB_MANON = bit(30);
constexpr u32 B_P1_TXPW_RSTB_TSSI = bit(31);
constexpr u32 R_P0_TXPW_RSTB = 0x58DC;
constexpr u32 R_P1_TXPW_RSTB = 0x78DC;
constexpr u32 RTW89_DACK_PATH_NR = 2;
constexpr u32 RTW89_DACK_IDX_NR = 2;
constexpr u32 RTW89_IQK_PATH_NR = 4;
constexpr u32 TSSI_TRIM_CH_GROUP_NUM = 8;
constexpr u32 TSSI_TRIM_CH_GROUP_NUM_6G = 16;
constexpr u32 TSSI_CCK_CH_GROUP_NUM = 6;
constexpr u32 TSSI_MCS_2G_CH_GROUP_NUM = 5;
constexpr u32 TSSI_MCS_5G_CH_GROUP_NUM = 14;
constexpr u32 TSSI_MCS_CH_GROUP_NUM = (TSSI_MCS_2G_CH_GROUP_NUM + TSSI_MCS_5G_CH_GROUP_NUM);
constexpr u32 TSSI_MCS_6G_CH_GROUP_NUM = 32;
constexpr u32 TSSI_MAX_CH_NUM = 67;
constexpr u32 TSSI_ALIMK_VALUE_NUM = 8;
constexpr u32 R_AX_WCPU_FW_CTRL = 0x01E0;
constexpr u32 B_AX_WCPU_FWDL_STS_MASK = mask(7, 5);
constexpr u32 R_AX_CMAC_FUNC_EN = 0xC000;
constexpr u32 B_AX_CMAC_EN = bit(30);
constexpr u32 R_AX_SYS_FUNC_EN = 0x0002;
constexpr u32 B_AX_FEN_BBRSTB = bit(0);
constexpr u32 B_AX_FEN_BB_GLB_RSTN = bit(1);
constexpr u32 R_AX_CTN_TXEN = 0xC348;
constexpr u32 B_AX_CTN_TXEN_ALL_MASK = mask(15, 0);
constexpr u32 BTC_RFK_PATH_MAP = mask(3, 0);
constexpr u32 BTC_RFK_PHY_MAP = mask(5, 4);
constexpr u32 BTC_RFK_BAND_MAP = mask(7, 6);
struct rtw89_dack_info {
	bool dack_done;
	u8 msbk_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR][RTW89_DACK_MSBK_NR];
	u8 dadck_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR];
	u16 addck_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR];
	u16 biask_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR];
	u32 dack_cnt;
	bool addck_timeout[RTW89_DACK_PATH_NR];
	bool dadck_timeout[RTW89_DACK_PATH_NR];
	bool msbk_timeout[RTW89_DACK_PATH_NR];
};
struct rtw89_iqk_info {
	bool lok_cor_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool lok_fin_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool lok_fail[RTW89_IQK_PATH_NR];
	bool iqk_tx_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool iqk_rx_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u32 iqk_fail_cnt;
	bool is_iqk_init;
	u32 iqk_channel[RTW89_IQK_CHS_NR];
	u8 iqk_band[RTW89_IQK_PATH_NR];
	u8 iqk_ch[RTW89_IQK_PATH_NR];
	u8 iqk_bw[RTW89_IQK_PATH_NR];
	u8 iqk_times;
	u8 version;
	u32 nb_txcfir[RTW89_IQK_PATH_NR];
	u32 nb_rxcfir[RTW89_IQK_PATH_NR];
	u32 bp_txkresult[RTW89_IQK_PATH_NR];
	u32 bp_rxkresult[RTW89_IQK_PATH_NR];
	u32 bp_iqkenable[RTW89_IQK_PATH_NR];
	bool is_wb_txiqk[RTW89_IQK_PATH_NR];
	bool is_wb_rxiqk[RTW89_IQK_PATH_NR];
	bool is_nbiqk;
	bool iqk_fft_en;
	bool iqk_xym_en;
	bool iqk_sram_en;
	bool iqk_cfir_en;
	u32 syn1to2;
	u8 iqk_mcc_ch[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u8 iqk_table_idx[RTW89_IQK_PATH_NR];
	u32 lok_idac[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u32 lok_vbuf[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
};
struct rtw89_tssi_info {
	u8 thermal[RF_PATH_MAX];
	s8 tssi_trim[RF_PATH_MAX][TSSI_TRIM_CH_GROUP_NUM];
	s8 tssi_trim_6g[RF_PATH_MAX][TSSI_TRIM_CH_GROUP_NUM_6G];
	s8 tssi_cck[RF_PATH_MAX][TSSI_CCK_CH_GROUP_NUM];
	s8 tssi_mcs[RF_PATH_MAX][TSSI_MCS_CH_GROUP_NUM];
	s8 tssi_6g_mcs[RF_PATH_MAX][TSSI_MCS_6G_CH_GROUP_NUM];
	s8 extra_ofst[RF_PATH_MAX];
	bool tssi_tracking_check[RF_PATH_MAX];
	u8 default_txagc_offset[RF_PATH_MAX];
	u32 base_thermal[RF_PATH_MAX];
	bool check_backup_aligmk[RF_PATH_MAX][TSSI_MAX_CH_NUM];
	u32 alignment_backup_by_ch[RF_PATH_MAX][TSSI_MAX_CH_NUM][TSSI_ALIMK_VALUE_NUM];
	u32 alignment_value[RF_PATH_MAX][TSSI_ALIMK_MAX][TSSI_ALIMK_VALUE_NUM];
	bool alignment_done[RF_PATH_MAX][TSSI_ALIMK_MAX];
	u32 tssi_alimk_time;
};
struct rtw89_phy_efuse_gain {
	bool offset_valid;
	bool comp_valid;
	s8 offset[RF_PATH_MAX][RTW89_GAIN_OFFSET_NR]; /* S(8, 0) */
	s8 offset_base[RTW89_PHY_MAX]; /* S(8, 4) */
	s8 rssi_base[RTW89_PHY_MAX]; /* S(8, 4) */
	s8 comp[RF_PATH_MAX][RTW89_SUBBAND_NR]; /* S(8, 0) */
};
struct rtw89_txpwr_track_cfg {
	const s8 (*delta_swingidx_6gb_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_6gb_p)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_6ga_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_6ga_p)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5gb_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5gb_p)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5ga_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5ga_p)[DELTA_SWINGIDX_SIZE];
	const s8 *delta_swingidx_2gb_n;
	const s8 *delta_swingidx_2gb_p;
	const s8 *delta_swingidx_2ga_n;
	const s8 *delta_swingidx_2ga_p;
	const s8 *delta_swingidx_2g_cck_b_n;
	const s8 *delta_swingidx_2g_cck_b_p;
	const s8 *delta_swingidx_2g_cck_a_n;
	const s8 *delta_swingidx_2g_cck_a_p;
};
struct rtw8852b_bb_pmac_info {
	u8 en_pmac_tx:1;
	u8 is_cck:1;
	u8 mode:3;
	u8 rsvd:3;
	u16 tx_cnt;
	u16 period;
	u16 tx_time;
	u8 duty_cycle;
};
struct rtw8852b_bb_tssi_bak {
	u8 tx_path;
	u8 rx_path;
	u32 p0_rfmode;
	u32 p0_rfmode_ftm;
	u32 p1_rfmode;
	u32 p1_rfmode_ftm;
	s16 tx_pwr; /* S9 */
};
struct rtw89_reg3_def {u32 addr,mask,data;};
struct rtw89_reg5_def {u8 flag,path;u32 addr,mask,data;};
struct rtw89_rfk_tbl {const rtw89_reg5_def *defs;u32 size;};
static const u32 _tssi_trigger[RTW8852B_TSSI_PATH_NR] = {0x5820, 0x7820};
static const u32 _tssi_cw_rpt_addr[RTW8852B_TSSI_PATH_NR] = {0x1c18, 0x3c18};
static const u32 _tssi_cw_default_addr[RTW8852B_TSSI_PATH_NR][4] = {
	{0x5634, 0x5630, 0x5630, 0x5630},
	{0x7634, 0x7630, 0x7630, 0x7630} };
static const u32 _tssi_cw_default_mask[4] = {
	0x000003ff, 0x3ff00000, 0x000ffc00, 0x000003ff};
static const u32 _tssi_de_cck_long[RF_PATH_NUM_8852B] = {0x5858, 0x7858};
static const u32 _tssi_de_cck_short[RF_PATH_NUM_8852B] = {0x5860, 0x7860};
static const u32 _tssi_de_mcs_20m[RF_PATH_NUM_8852B] = {0x5838, 0x7838};
static const u32 _tssi_de_mcs_40m[RF_PATH_NUM_8852B] = {0x5840, 0x7840};
static const u32 _tssi_de_mcs_80m[RF_PATH_NUM_8852B] = {0x5848, 0x7848};
static const u32 _tssi_de_mcs_80m_80m[RF_PATH_NUM_8852B] = {0x5850, 0x7850};
static const u32 _tssi_de_mcs_5m[RF_PATH_NUM_8852B] = {0x5828, 0x7828};
static const u32 _tssi_de_mcs_10m[RF_PATH_NUM_8852B] = {0x5830, 0x7830};
static const u32 _a_idxrxgain[RTW8852B_RXK_GROUP_NR] = {0x190, 0x198, 0x350, 0x352};
static const u32 _a_idxattc2[RTW8852B_RXK_GROUP_NR] = {0x0f, 0x0f, 0x3f, 0x7f};
static const u32 _a_idxattc1[RTW8852B_RXK_GROUP_NR] = {0x3, 0x1, 0x0, 0x0};
static const u32 _g_idxrxgain[RTW8852B_RXK_GROUP_NR] = {0x212, 0x21c, 0x350, 0x360};
static const u32 _g_idxattc2[RTW8852B_RXK_GROUP_NR] = {0x00, 0x00, 0x28, 0x5f};
static const u32 _g_idxattc1[RTW8852B_RXK_GROUP_NR] = {0x3, 0x3, 0x2, 0x1};
static const u32 _a_power_range[RTW8852B_RXK_GROUP_NR] = {0x0, 0x0, 0x0, 0x0};
static const u32 _a_track_range[RTW8852B_RXK_GROUP_NR] = {0x3, 0x3, 0x6, 0x6};
static const u32 _a_gain_bb[RTW8852B_RXK_GROUP_NR] = {0x08, 0x0e, 0x06, 0x0e};
static const u32 _a_itqt[RTW8852B_RXK_GROUP_NR] = {0x12, 0x12, 0x12, 0x1b};
static const u32 _g_power_range[RTW8852B_RXK_GROUP_NR] = {0x0, 0x0, 0x0, 0x0};
static const u32 _g_track_range[RTW8852B_RXK_GROUP_NR] = {0x4, 0x4, 0x6, 0x6};
static const u32 _g_gain_bb[RTW8852B_RXK_GROUP_NR] = {0x08, 0x0e, 0x06, 0x0e};
static const u32 _g_itqt[RTW8852B_RXK_GROUP_NR] = {0x09, 0x12, 0x1b, 0x24};
static const u32 rtw8852b_backup_bb_regs[] = {0x2344, 0x5800, 0x7800};
static const u32 rtw8852b_backup_rf_regs[] = {
	0xde, 0xdf, 0x8b, 0x90, 0x97, 0x85, 0x1e, 0x0, 0x2, 0x5, 0x10005
};
static const struct rtw89_reg3_def rtw8852b_set_nondbcc_path01[] = {
	{0x20fc, 0xffff0000, 0x0303},
	{0x5864, 0x18000000, 0x3},
	{0x7864, 0x18000000, 0x3},
	{0x12b8, 0x40000000, 0x1},
	{0x32b8, 0x40000000, 0x1},
	{0x030c, 0xff000000, 0x13},
	{0x032c, 0xffff0000, 0x0041},
	{0x12b8, 0x10000000, 0x1},
	{0x58c8, 0x01000000, 0x1},
	{0x78c8, 0x01000000, 0x1},
	{0x5864, 0xc0000000, 0x3},
	{0x7864, 0xc0000000, 0x3},
	{0x2008, 0x01ffffff, 0x1ffffff},
	{0x0c1c, 0x00000004, 0x1},
	{0x0700, 0x08000000, 0x1},
	{0x0c70, 0x000003ff, 0x3ff},
	{0x0c60, 0x00000003, 0x3},
	{0x0c6c, 0x00000001, 0x1},
	{0x58ac, 0x08000000, 0x1},
	{0x78ac, 0x08000000, 0x1},
	{0x0c3c, 0x00000200, 0x1},
	{0x2344, 0x80000000, 0x1},
	{0x4490, 0x80000000, 0x1},
	{0x12a0, 0x00007000, 0x7},
	{0x12a0, 0x00008000, 0x1},
	{0x12a0, 0x00070000, 0x3},
	{0x12a0, 0x00080000, 0x1},
	{0x32a0, 0x00070000, 0x3},
	{0x32a0, 0x00080000, 0x1},
	{0x0700, 0x01000000, 0x1},
	{0x0700, 0x06000000, 0x2},
	{0x20fc, 0xffff0000, 0x3333},
};
static const struct rtw89_reg3_def rtw8852b_restore_nondbcc_path01[] = {
	{0x20fc, 0xffff0000, 0x0303},
	{0x12b8, 0x40000000, 0x0},
	{0x32b8, 0x40000000, 0x0},
	{0x5864, 0xc0000000, 0x0},
	{0x7864, 0xc0000000, 0x0},
	{0x2008, 0x01ffffff, 0x0000000},
	{0x0c1c, 0x00000004, 0x0},
	{0x0700, 0x08000000, 0x0},
	{0x0c70, 0x0000001f, 0x03},
	{0x0c70, 0x000003e0, 0x03},
	{0x12a0, 0x000ff000, 0x00},
	{0x32a0, 0x000ff000, 0x00},
	{0x0700, 0x07000000, 0x0},
	{0x20fc, 0xffff0000, 0x0000},
	{0x58c8, 0x01000000, 0x0},
	{0x78c8, 0x01000000, 0x0},
	{0x0c3c, 0x00000200, 0x0},
	{0x2344, 0x80000000, 0x0},
};
static const struct rtw89_reg3_def rtw8852b_pmac_ht20_mcs7_tbl[] = {
	{0x4580, 0x0000ffff, 0x0},
	{0x4580, 0xffff0000, 0x0},
	{0x4584, 0x0000ffff, 0x0},
	{0x4584, 0xffff0000, 0x0},
	{0x4580, 0x0000ffff, 0x1},
	{0x4578, 0x00ffffff, 0x2018b},
	{0x4570, 0x03ffffff, 0x7},
	{0x4574, 0x03ffffff, 0x32407},
	{0x45b8, 0x00000010, 0x0},
	{0x45b8, 0x00000100, 0x0},
	{0x45b8, 0x00000080, 0x0},
	{0x45b8, 0x00000008, 0x0},
	{0x45a0, 0x0000ff00, 0x0},
	{0x45a0, 0xff000000, 0x1},
	{0x45a4, 0x0000ff00, 0x2},
	{0x45a4, 0xff000000, 0x3},
	{0x45b8, 0x00000020, 0x0},
	{0x4568, 0xe0000000, 0x0},
	{0x45b8, 0x00000002, 0x1},
	{0x456c, 0xe0000000, 0x0},
	{0x45b4, 0x00006000, 0x0},
	{0x45b4, 0x00001800, 0x1},
	{0x45b8, 0x00000040, 0x0},
	{0x45b8, 0x00000004, 0x0},
	{0x45b8, 0x00000200, 0x0},
	{0x4598, 0xf8000000, 0x0},
	{0x45b8, 0x00100000, 0x0},
	{0x45a8, 0x00000fc0, 0x0},
	{0x45b8, 0x00200000, 0x0},
	{0x45b0, 0x00000038, 0x0},
	{0x45b0, 0x000001c0, 0x0},
	{0x45a0, 0x000000ff, 0x0},
	{0x45b8, 0x00400000, 0x0},
	{0x4590, 0x000007ff, 0x0},
	{0x45b0, 0x00000e00, 0x0},
	{0x45ac, 0x0000001f, 0x0},
	{0x45b8, 0x00800000, 0x0},
	{0x45a8, 0x0003f000, 0x0},
	{0x45b8, 0x01000000, 0x0},
	{0x45b0, 0x00007000, 0x0},
	{0x45b0, 0x00038000, 0x0},
	{0x45a0, 0x00ff0000, 0x0},
	{0x45b8, 0x02000000, 0x0},
	{0x4590, 0x003ff800, 0x0},
	{0x45b0, 0x001c0000, 0x0},
	{0x45ac, 0x000003e0, 0x0},
	{0x45b8, 0x04000000, 0x0},
	{0x45a8, 0x00fc0000, 0x0},
	{0x45b8, 0x08000000, 0x0},
	{0x45b0, 0x00e00000, 0x0},
	{0x45b0, 0x07000000, 0x0},
	{0x45a4, 0x000000ff, 0x0},
	{0x45b8, 0x10000000, 0x0},
	{0x4594, 0x000007ff, 0x0},
	{0x45b0, 0x38000000, 0x0},
	{0x45ac, 0x00007c00, 0x0},
	{0x45b8, 0x20000000, 0x0},
	{0x45a8, 0x3f000000, 0x0},
	{0x45b8, 0x40000000, 0x0},
	{0x45b4, 0x00000007, 0x0},
	{0x45b4, 0x00000038, 0x0},
	{0x45a4, 0x00ff0000, 0x0},
	{0x45b8, 0x80000000, 0x0},
	{0x4594, 0x003ff800, 0x0},
	{0x45b4, 0x000001c0, 0x0},
	{0x4598, 0xf8000000, 0x0},
	{0x45b8, 0x00100000, 0x0},
	{0x45a8, 0x00000fc0, 0x7},
	{0x45b8, 0x00200000, 0x0},
	{0x45b0, 0x00000038, 0x0},
	{0x45b0, 0x000001c0, 0x0},
	{0x45a0, 0x000000ff, 0x0},
	{0x45b4, 0x06000000, 0x0},
	{0x45b0, 0x00000007, 0x0},
	{0x45b8, 0x00080000, 0x0},
	{0x45a8, 0x0000003f, 0x0},
	{0x457c, 0xffe00000, 0x1},
	{0x4530, 0xffffffff, 0x0},
	{0x4588, 0x00003fff, 0x0},
	{0x4598, 0x000001ff, 0x0},
	{0x4534, 0xffffffff, 0x0},
	{0x4538, 0xffffffff, 0x0},
	{0x453c, 0xffffffff, 0x0},
	{0x4588, 0x0fffc000, 0x0},
	{0x4598, 0x0003fe00, 0x0},
	{0x4540, 0xffffffff, 0x0},
	{0x4544, 0xffffffff, 0x0},
	{0x4548, 0xffffffff, 0x0},
	{0x458c, 0x00003fff, 0x0},
	{0x4598, 0x07fc0000, 0x0},
	{0x454c, 0xffffffff, 0x0},
	{0x4550, 0xffffffff, 0x0},
	{0x4554, 0xffffffff, 0x0},
	{0x458c, 0x0fffc000, 0x0},
	{0x459c, 0x000001ff, 0x0},
	{0x4558, 0xffffffff, 0x0},
	{0x455c, 0xffffffff, 0x0},
	{0x4530, 0xffffffff, 0x4e790001},
	{0x4588, 0x00003fff, 0x0},
	{0x4598, 0x000001ff, 0x1},
	{0x4534, 0xffffffff, 0x0},
	{0x4538, 0xffffffff, 0x4b},
	{0x45ac, 0x38000000, 0x7},
	{0x4588, 0xf0000000, 0x0},
	{0x459c, 0x7e000000, 0x0},
	{0x45b8, 0x00040000, 0x0},
	{0x45b8, 0x00020000, 0x0},
	{0x4590, 0xffc00000, 0x0},
	{0x45b8, 0x00004000, 0x0},
	{0x4578, 0xff000000, 0x0},
	{0x45b8, 0x00000400, 0x0},
	{0x45b8, 0x00000800, 0x0},
	{0x45b8, 0x00001000, 0x0},
	{0x45b8, 0x00002000, 0x0},
	{0x45b4, 0x00018000, 0x0},
	{0x45ac, 0x07800000, 0x0},
	{0x45b4, 0x00000600, 0x2},
	{0x459c, 0x0001fe00, 0x80},
	{0x45ac, 0x00078000, 0x3},
	{0x459c, 0x01fe0000, 0x1},
};
static const s8 _txpwr_track_delta_swingidx_5gb_n[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
	 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8},
	{0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5,
	 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 8},
	{0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7,
	 7, 8, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12},
};
static const s8 _txpwr_track_delta_swingidx_5gb_p[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5,
	 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 8},
	{0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
	 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8},
	{0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5,
	 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9},
};
static const s8 _txpwr_track_delta_swingidx_5ga_n[][DELTA_SWINGIDX_SIZE] = {
	{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
	 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4},
	{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
	 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3},
	{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
	 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3},
};
static const s8 _txpwr_track_delta_swingidx_5ga_p[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
	 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7},
	{0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5,
	 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9},
	{0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5,
	 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9},
};
static const s8 _txpwr_track_delta_swingidx_2gb_n[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2};
static const s8 _txpwr_track_delta_swingidx_2gb_p[] = {
	0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
	 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6};
static const s8 _txpwr_track_delta_swingidx_2ga_n[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const s8 _txpwr_track_delta_swingidx_2ga_p[] = {
	0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3,
	 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5};
static const s8 _txpwr_track_delta_swingidx_2g_cck_b_n[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static const s8 _txpwr_track_delta_swingidx_2g_cck_b_p[] = {
	0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
	 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6};
static const s8 _txpwr_track_delta_swingidx_2g_cck_a_n[] = {
	0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -2, -2,
	 -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3};
static const s8 _txpwr_track_delta_swingidx_2g_cck_a_p[] = {
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const rtw89_txpwr_track_cfg rtw89_8852b_trk_cfg={nullptr,nullptr,nullptr,nullptr,_txpwr_track_delta_swingidx_5gb_n,_txpwr_track_delta_swingidx_5gb_p,_txpwr_track_delta_swingidx_5ga_n,_txpwr_track_delta_swingidx_5ga_p,_txpwr_track_delta_swingidx_2gb_n,_txpwr_track_delta_swingidx_2gb_p,_txpwr_track_delta_swingidx_2ga_n,_txpwr_track_delta_swingidx_2ga_p,_txpwr_track_delta_swingidx_2g_cck_b_n,_txpwr_track_delta_swingidx_2g_cck_b_p,_txpwr_track_delta_swingidx_2g_cck_a_n,_txpwr_track_delta_swingidx_2g_cck_a_p};
constexpr u32 BACKUP_BB_REGS_NR = arraySize(rtw8852b_backup_bb_regs);
constexpr u32 BACKUP_RF_REGS_NR = arraySize(rtw8852b_backup_rf_regs);
static const rtw89_reg5_def rtw8852b_afe_init_defs[]={{1,0,0xC0D4,0xffffffff,0x4486888c},
{1,0,0xC0D8,0xffffffff,0xc6ba10e0},
{1,0,0xc0dc,0xffffffff,0x30c52868},
{1,0,0xc0e0,0xffffffff,0x05008128},
{1,0,0xc0e4,0xffffffff,0x0000272b},
{1,0,0xC1D4,0xffffffff,0x4486888c},
{1,0,0xC1D8,0xffffffff,0xc6ba10e0},
{1,0,0xc1dc,0xffffffff,0x30c52868},
{1,0,0xc1e0,0xffffffff,0x05008128},
{1,0,0xc1e4,0xffffffff,0x0000272b}};
static const rtw89_rfk_tbl rtw8852b_afe_init_defs_tbl={rtw8852b_afe_init_defs,sizeof(rtw8852b_afe_init_defs)/sizeof(rtw8852b_afe_init_defs[0])};
static const rtw89_reg5_def rtw8852b_check_addc_defs_a[]={{1,0,0x20f4,bit(24),0x0},
{1,0,0x20f8,0x80000000,0x1},
{1,0,0x20f0,0xff0000,0x1},
{1,0,0x20f0,0xf00,0x2},
{1,0,0x20f0,0xf,0x0},
{1,0,0x20f0,0xc0,0x2}};
static const rtw89_rfk_tbl rtw8852b_check_addc_defs_a_tbl={rtw8852b_check_addc_defs_a,sizeof(rtw8852b_check_addc_defs_a)/sizeof(rtw8852b_check_addc_defs_a[0])};
static const rtw89_reg5_def rtw8852b_check_addc_defs_b[]={{1,0,0x20f4,bit(24),0x0},
{1,0,0x20f8,0x80000000,0x1},
{1,0,0x20f0,0xff0000,0x1},
{1,0,0x20f0,0xf00,0x2},
{1,0,0x20f0,0xf,0x0},
{1,0,0x20f0,0xc0,0x3}};
static const rtw89_rfk_tbl rtw8852b_check_addc_defs_b_tbl={rtw8852b_check_addc_defs_b,sizeof(rtw8852b_check_addc_defs_b)/sizeof(rtw8852b_check_addc_defs_b[0])};
static const rtw89_reg5_def rtw8852b_check_dadc_dis_defs_a[]={{1,0,0x12dc,bit(0),0x0},
{1,0,0x12e8,bit(2),0x0},
{0,RF_PATH_A,0x8f,bit(13),0x0},
{1,0,0x032C,bit(16),0x1}};
static const rtw89_rfk_tbl rtw8852b_check_dadc_dis_defs_a_tbl={rtw8852b_check_dadc_dis_defs_a,sizeof(rtw8852b_check_dadc_dis_defs_a)/sizeof(rtw8852b_check_dadc_dis_defs_a[0])};
static const rtw89_reg5_def rtw8852b_check_dadc_dis_defs_b[]={{1,0,0x32dc,bit(0),0x0},
{1,0,0x32e8,bit(2),0x0},
{0,RF_PATH_B,0x8f,bit(13),0x0},
{1,0,0x032C,bit(16),0x1}};
static const rtw89_rfk_tbl rtw8852b_check_dadc_dis_defs_b_tbl={rtw8852b_check_dadc_dis_defs_b,sizeof(rtw8852b_check_dadc_dis_defs_b)/sizeof(rtw8852b_check_dadc_dis_defs_b[0])};
static const rtw89_reg5_def rtw8852b_check_dadc_en_defs_a[]={{1,0,0x032C,bit(30),0x0},
{1,0,0x030C,0x0f000000,0xf},
{1,0,0x030C,0x0f000000,0x3},
{1,0,0x032C,bit(16),0x0},
{1,0,0x12dc,bit(0),0x1},
{1,0,0x12e8,bit(2),0x1},
{0,RF_PATH_A,0x8f,bit(13),0x1}};
static const rtw89_rfk_tbl rtw8852b_check_dadc_en_defs_a_tbl={rtw8852b_check_dadc_en_defs_a,sizeof(rtw8852b_check_dadc_en_defs_a)/sizeof(rtw8852b_check_dadc_en_defs_a[0])};
static const rtw89_reg5_def rtw8852b_check_dadc_en_defs_b[]={{1,0,0x032C,bit(30),0x0},
{1,0,0x030C,0x0f000000,0xf},
{1,0,0x030C,0x0f000000,0x3},
{1,0,0x032C,bit(16),0x0},
{1,0,0x32dc,bit(0),0x1},
{1,0,0x32e8,bit(2),0x1},
{0,RF_PATH_B,0x8f,bit(13),0x1}};
static const rtw89_rfk_tbl rtw8852b_check_dadc_en_defs_b_tbl={rtw8852b_check_dadc_en_defs_b,sizeof(rtw8852b_check_dadc_en_defs_b)/sizeof(rtw8852b_check_dadc_en_defs_b[0])};
static const rtw89_reg5_def rtw8852b_dack_s0_1_defs[]={{1,0,0x12A0,bit(15),0x1},
{1,0,0x12A0,0x00007000,0x3},
{1,0,0x12B8,bit(30),0x1},
{1,0,0x030C,bit(28),0x1},
{1,0,0x032C,0x80000000,0x0},
{1,0,0xC0D8,bit(16),0x1},
{1,0,0xc0dc,0x0c000000,0x3},
{1,0,0xC004,bit(30),0x0},
{1,0,0xc024,bit(30),0x0},
{1,0,0xC004,0x3ff00000,0x30},
{1,0,0xC004,0xc0000000,0x0},
{1,0,0xC004,bit(17),0x1},
{1,0,0xc024,bit(17),0x1},
{1,0,0xc00c,bit(2),0x0},
{1,0,0xc02c,bit(2),0x0},
{1,0,0xC004,bit(0),0x1},
{1,0,0xc024,bit(0),0x1},
{4,0,0,0,1}};
static const rtw89_rfk_tbl rtw8852b_dack_s0_1_defs_tbl={rtw8852b_dack_s0_1_defs,sizeof(rtw8852b_dack_s0_1_defs)/sizeof(rtw8852b_dack_s0_1_defs[0])};
static const rtw89_reg5_def rtw8852b_dack_s0_2_defs[]={{1,0,0xc0dc,0x0c000000,0x0},
{1,0,0xc00c,bit(2),0x1},
{1,0,0xc02c,bit(2),0x1}};
static const rtw89_rfk_tbl rtw8852b_dack_s0_2_defs_tbl={rtw8852b_dack_s0_2_defs,sizeof(rtw8852b_dack_s0_2_defs)/sizeof(rtw8852b_dack_s0_2_defs[0])};
static const rtw89_reg5_def rtw8852b_dack_s0_3_defs[]={{1,0,0xC004,bit(0),0x0},
{1,0,0xc024,bit(0),0x0},
{1,0,0xC0D8,bit(16),0x0},
{1,0,0x12A0,bit(15),0x0},
{1,0,0x12A0,0x00007000,0x7}};
static const rtw89_rfk_tbl rtw8852b_dack_s0_3_defs_tbl={rtw8852b_dack_s0_3_defs,sizeof(rtw8852b_dack_s0_3_defs)/sizeof(rtw8852b_dack_s0_3_defs[0])};
static const rtw89_reg5_def rtw8852b_dack_s1_1_defs[]={{1,0,0x32a0,bit(15),0x1},
{1,0,0x32a0,0x7000,0x3},
{1,0,0x32B8,bit(30),0x1},
{1,0,0x030C,bit(28),0x1},
{1,0,0x032C,0x80000000,0x0},
{1,0,0xC1D8,bit(16),0x1},
{1,0,0xc1dc,0x0c000000,0x3},
{1,0,0xc104,bit(30),0x0},
{1,0,0xc124,bit(30),0x0},
{1,0,0xc104,0x3ff00000,0x30},
{1,0,0xc104,0xc0000000,0x0},
{1,0,0xc104,bit(17),0x1},
{1,0,0xc124,bit(17),0x1},
{1,0,0xc10c,bit(2),0x0},
{1,0,0xc12c,bit(2),0x0},
{1,0,0xc104,bit(0),0x1},
{1,0,0xc124,bit(0),0x1},
{4,0,0,0,1}};
static const rtw89_rfk_tbl rtw8852b_dack_s1_1_defs_tbl={rtw8852b_dack_s1_1_defs,sizeof(rtw8852b_dack_s1_1_defs)/sizeof(rtw8852b_dack_s1_1_defs[0])};
static const rtw89_reg5_def rtw8852b_dack_s1_2_defs[]={{1,0,0xc1dc,0x0c000000,0x0},
{1,0,0xc10c,bit(2),0x1},
{1,0,0xc12c,bit(2),0x1},
{4,0,0,0,1}};
static const rtw89_rfk_tbl rtw8852b_dack_s1_2_defs_tbl={rtw8852b_dack_s1_2_defs,sizeof(rtw8852b_dack_s1_2_defs)/sizeof(rtw8852b_dack_s1_2_defs[0])};
static const rtw89_reg5_def rtw8852b_dack_s1_3_defs[]={{1,0,0xc104,bit(0),0x0},
{1,0,0xc124,bit(0),0x0},
{1,0,0xC1D8,bit(16),0x0},
{1,0,0x32a0,bit(15),0x0},
{1,0,0x32a0,0x7000,0x7}};
static const rtw89_rfk_tbl rtw8852b_dack_s1_3_defs_tbl={rtw8852b_dack_s1_3_defs,sizeof(rtw8852b_dack_s1_3_defs)/sizeof(rtw8852b_dack_s1_3_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_2g_all_defs[]={{1,0,0x5604,0x80000000,0x1},
{1,0,0x5600,0x3fffffff,0x3f2d2721},
{1,0,0x5604,0x003fffff,0x010101},
{1,0,0x5630,0x3fffffff,0x01ef27af},
{1,0,0x5634,0x3fffffff,0x00000075},
{1,0,0x5638,0x000fffff,0x00000},
{1,0,0x563c,0x3fffffff,0x017f13ae},
{1,0,0x5640,0x3fffffff,0x0000006e},
{1,0,0x5644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_2g_all_defs_tbl={rtw8852b_tssi_align_a_2g_all_defs,sizeof(rtw8852b_tssi_align_a_2g_all_defs)/sizeof(rtw8852b_tssi_align_a_2g_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_2g_part_defs[]={{1,0,0x5630,0x3fffffff,0x01ef27af},
{1,0,0x5634,0x3fffffff,0x00000075},
{1,0,0x563c,0x3fffffff,0x017f13ae},
{1,0,0x5640,0x3fffffff,0x0000006e}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_2g_part_defs_tbl={rtw8852b_tssi_align_a_2g_part_defs,sizeof(rtw8852b_tssi_align_a_2g_part_defs)/sizeof(rtw8852b_tssi_align_a_2g_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_5g1_all_defs[]={{1,0,0x5604,0x80000000,0x1},
{1,0,0x5600,0x3fffffff,0x3f2d2721},
{1,0,0x5604,0x003fffff,0x010101},
{1,0,0x5630,0x3fffffff,0x016037e7},
{1,0,0x5634,0x3fffffff,0x0000006f},
{1,0,0x5638,0x000fffff,0x00000},
{1,0,0x563c,0x3fffffff,0x00000000},
{1,0,0x5640,0x3fffffff,0x00000000},
{1,0,0x5644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_5g1_all_defs_tbl={rtw8852b_tssi_align_a_5g1_all_defs,sizeof(rtw8852b_tssi_align_a_5g1_all_defs)/sizeof(rtw8852b_tssi_align_a_5g1_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_5g1_part_defs[]={{1,0,0x5630,0x3fffffff,0x016037e7},
{1,0,0x5634,0x3fffffff,0x0000006f},
{1,0,0x563c,0x3fffffff,0x00000000},
{1,0,0x5640,0x3fffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_5g1_part_defs_tbl={rtw8852b_tssi_align_a_5g1_part_defs,sizeof(rtw8852b_tssi_align_a_5g1_part_defs)/sizeof(rtw8852b_tssi_align_a_5g1_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_5g2_all_defs[]={{1,0,0x5604,0x80000000,0x1},
{1,0,0x5600,0x3fffffff,0x3f2d2721},
{1,0,0x5604,0x003fffff,0x010101},
{1,0,0x5630,0x3fffffff,0x01f053f1},
{1,0,0x5634,0x3fffffff,0x00000070},
{1,0,0x5638,0x000fffff,0x00000},
{1,0,0x563c,0x3fffffff,0x00000000},
{1,0,0x5640,0x3fffffff,0x00000000},
{1,0,0x5644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_5g2_all_defs_tbl={rtw8852b_tssi_align_a_5g2_all_defs,sizeof(rtw8852b_tssi_align_a_5g2_all_defs)/sizeof(rtw8852b_tssi_align_a_5g2_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_5g2_part_defs[]={{1,0,0x5630,0x3fffffff,0x01f053f1},
{1,0,0x5634,0x3fffffff,0x00000070},
{1,0,0x563c,0x3fffffff,0x00000000},
{1,0,0x5640,0x3fffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_5g2_part_defs_tbl={rtw8852b_tssi_align_a_5g2_part_defs,sizeof(rtw8852b_tssi_align_a_5g2_part_defs)/sizeof(rtw8852b_tssi_align_a_5g2_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_5g3_all_defs[]={{1,0,0x5604,0x80000000,0x1},
{1,0,0x5600,0x3fffffff,0x3f2d2721},
{1,0,0x5604,0x003fffff,0x010101},
{1,0,0x5630,0x3fffffff,0x01c047ee},
{1,0,0x5634,0x3fffffff,0x00000070},
{1,0,0x5638,0x000fffff,0x00000},
{1,0,0x563c,0x3fffffff,0x00000000},
{1,0,0x5640,0x3fffffff,0x00000000},
{1,0,0x5644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_5g3_all_defs_tbl={rtw8852b_tssi_align_a_5g3_all_defs,sizeof(rtw8852b_tssi_align_a_5g3_all_defs)/sizeof(rtw8852b_tssi_align_a_5g3_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_a_5g3_part_defs[]={{1,0,0x5630,0x3fffffff,0x01c047ee},
{1,0,0x5634,0x3fffffff,0x00000070},
{1,0,0x563c,0x3fffffff,0x00000000},
{1,0,0x5640,0x3fffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_a_5g3_part_defs_tbl={rtw8852b_tssi_align_a_5g3_part_defs,sizeof(rtw8852b_tssi_align_a_5g3_part_defs)/sizeof(rtw8852b_tssi_align_a_5g3_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_2g_all_defs[]={{1,0,0x7604,0x80000000,0x1},
{1,0,0x7600,0x3fffffff,0x3f2d2721},
{1,0,0x7604,0x003fffff,0x010101},
{1,0,0x7630,0x3fffffff,0x01ff2bb5},
{1,0,0x7634,0x3fffffff,0x00000078},
{1,0,0x7638,0x000fffff,0x00000},
{1,0,0x763c,0x3fffffff,0x018f2bb0},
{1,0,0x7640,0x3fffffff,0x00000072},
{1,0,0x7644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_2g_all_defs_tbl={rtw8852b_tssi_align_b_2g_all_defs,sizeof(rtw8852b_tssi_align_b_2g_all_defs)/sizeof(rtw8852b_tssi_align_b_2g_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_2g_part_defs[]={{1,0,0x7630,0x3fffffff,0x01ff2bb5},
{1,0,0x7634,0x3fffffff,0x00000078},
{1,0,0x763c,0x3fffffff,0x018f2bb0},
{1,0,0x7640,0x3fffffff,0x00000072}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_2g_part_defs_tbl={rtw8852b_tssi_align_b_2g_part_defs,sizeof(rtw8852b_tssi_align_b_2g_part_defs)/sizeof(rtw8852b_tssi_align_b_2g_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_5g1_all_defs[]={{1,0,0x7604,0x80000000,0x1},
{1,0,0x7600,0x3fffffff,0x3f2d2721},
{1,0,0x7604,0x003fffff,0x010101},
{1,0,0x7630,0x3fffffff,0x009003da},
{1,0,0x7634,0x3fffffff,0x00000069},
{1,0,0x7638,0x000fffff,0x00000},
{1,0,0x763c,0x3fffffff,0x00000000},
{1,0,0x7640,0x3fffffff,0x00000000},
{1,0,0x7644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_5g1_all_defs_tbl={rtw8852b_tssi_align_b_5g1_all_defs,sizeof(rtw8852b_tssi_align_b_5g1_all_defs)/sizeof(rtw8852b_tssi_align_b_5g1_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_5g1_part_defs[]={{1,0,0x7630,0x3fffffff,0x009003da},
{1,0,0x7634,0x3fffffff,0x00000069},
{1,0,0x763c,0x3fffffff,0x00000000},
{1,0,0x7640,0x3fffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_5g1_part_defs_tbl={rtw8852b_tssi_align_b_5g1_part_defs,sizeof(rtw8852b_tssi_align_b_5g1_part_defs)/sizeof(rtw8852b_tssi_align_b_5g1_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_5g2_all_defs[]={{1,0,0x7604,0x80000000,0x1},
{1,0,0x7600,0x3fffffff,0x3f2d2721},
{1,0,0x7604,0x003fffff,0x010101},
{1,0,0x7630,0x3fffffff,0x013027e6},
{1,0,0x7634,0x3fffffff,0x00000069},
{1,0,0x7638,0x000fffff,0x00000},
{1,0,0x763c,0x3fffffff,0x00000000},
{1,0,0x7640,0x3fffffff,0x00000000},
{1,0,0x7644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_5g2_all_defs_tbl={rtw8852b_tssi_align_b_5g2_all_defs,sizeof(rtw8852b_tssi_align_b_5g2_all_defs)/sizeof(rtw8852b_tssi_align_b_5g2_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_5g2_part_defs[]={{1,0,0x7630,0x3fffffff,0x013027e6},
{1,0,0x7634,0x3fffffff,0x00000069},
{1,0,0x763c,0x3fffffff,0x00000000},
{1,0,0x7640,0x3fffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_5g2_part_defs_tbl={rtw8852b_tssi_align_b_5g2_part_defs,sizeof(rtw8852b_tssi_align_b_5g2_part_defs)/sizeof(rtw8852b_tssi_align_b_5g2_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_5g3_all_defs[]={{1,0,0x7604,0x80000000,0x1},
{1,0,0x7600,0x3fffffff,0x3f2d2721},
{1,0,0x7604,0x003fffff,0x010101},
{1,0,0x7630,0x3fffffff,0x009003da},
{1,0,0x7634,0x3fffffff,0x00000069},
{1,0,0x7638,0x000fffff,0x00000},
{1,0,0x763c,0x3fffffff,0x00000000},
{1,0,0x7640,0x3fffffff,0x00000000},
{1,0,0x7644,0x000fffff,0x00000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_5g3_all_defs_tbl={rtw8852b_tssi_align_b_5g3_all_defs,sizeof(rtw8852b_tssi_align_b_5g3_all_defs)/sizeof(rtw8852b_tssi_align_b_5g3_all_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_align_b_5g3_part_defs[]={{1,0,0x7630,0x3fffffff,0x009003da},
{1,0,0x7634,0x3fffffff,0x00000069},
{1,0,0x763c,0x3fffffff,0x00000000},
{1,0,0x7640,0x3fffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_align_b_5g3_part_defs_tbl={rtw8852b_tssi_align_b_5g3_part_defs,sizeof(rtw8852b_tssi_align_b_5g3_part_defs)/sizeof(rtw8852b_tssi_align_b_5g3_part_defs[0])};
static const rtw89_reg5_def rtw8852b_tssi_dac_gain_defs_a[]={{1,0,0x58b0,0x00000400,0x1},
{1,0,0x58b0,0x00000fff,0x000},
{1,0,0x58b0,0x00000800,0x1},
{1,0,0x5a00,0xffffffff,0x00000000},
{1,0,0x5a04,0xffffffff,0x00000000},
{1,0,0x5a08,0xffffffff,0x00000000},
{1,0,0x5a0c,0xffffffff,0x00000000},
{1,0,0x5a10,0xffffffff,0x00000000},
{1,0,0x5a14,0xffffffff,0x00000000},
{1,0,0x5a18,0xffffffff,0x00000000},
{1,0,0x5a1c,0xffffffff,0x00000000},
{1,0,0x5a20,0xffffffff,0x00000000},
{1,0,0x5a24,0xffffffff,0x00000000},
{1,0,0x5a28,0xffffffff,0x00000000},
{1,0,0x5a2c,0xffffffff,0x00000000},
{1,0,0x5a30,0xffffffff,0x00000000},
{1,0,0x5a34,0xffffffff,0x00000000},
{1,0,0x5a38,0xffffffff,0x00000000},
{1,0,0x5a3c,0xffffffff,0x00000000},
{1,0,0x5a40,0xffffffff,0x00000000},
{1,0,0x5a44,0xffffffff,0x00000000},
{1,0,0x5a48,0xffffffff,0x00000000},
{1,0,0x5a4c,0xffffffff,0x00000000},
{1,0,0x5a50,0xffffffff,0x00000000},
{1,0,0x5a54,0xffffffff,0x00000000},
{1,0,0x5a58,0xffffffff,0x00000000},
{1,0,0x5a5c,0xffffffff,0x00000000},
{1,0,0x5a60,0xffffffff,0x00000000},
{1,0,0x5a64,0xffffffff,0x00000000},
{1,0,0x5a68,0xffffffff,0x00000000},
{1,0,0x5a6c,0xffffffff,0x00000000},
{1,0,0x5a70,0xffffffff,0x00000000},
{1,0,0x5a74,0xffffffff,0x00000000},
{1,0,0x5a78,0xffffffff,0x00000000},
{1,0,0x5a7c,0xffffffff,0x00000000},
{1,0,0x5a80,0xffffffff,0x00000000},
{1,0,0x5a84,0xffffffff,0x00000000},
{1,0,0x5a88,0xffffffff,0x00000000},
{1,0,0x5a8c,0xffffffff,0x00000000},
{1,0,0x5a90,0xffffffff,0x00000000},
{1,0,0x5a94,0xffffffff,0x00000000},
{1,0,0x5a98,0xffffffff,0x00000000},
{1,0,0x5a9c,0xffffffff,0x00000000},
{1,0,0x5aa0,0xffffffff,0x00000000},
{1,0,0x5aa4,0xffffffff,0x00000000},
{1,0,0x5aa8,0xffffffff,0x00000000},
{1,0,0x5aac,0xffffffff,0x00000000},
{1,0,0x5ab0,0xffffffff,0x00000000},
{1,0,0x5ab4,0xffffffff,0x00000000},
{1,0,0x5ab8,0xffffffff,0x00000000},
{1,0,0x5abc,0xffffffff,0x00000000},
{1,0,0x5ac0,0xffffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_dac_gain_defs_a_tbl={rtw8852b_tssi_dac_gain_defs_a,sizeof(rtw8852b_tssi_dac_gain_defs_a)/sizeof(rtw8852b_tssi_dac_gain_defs_a[0])};
static const rtw89_reg5_def rtw8852b_tssi_dac_gain_defs_b[]={{1,0,0x78b0,0x00000fff,0x000},
{1,0,0x78b0,0x00000800,0x1},
{1,0,0x7a00,0xffffffff,0x00000000},
{1,0,0x7a04,0xffffffff,0x00000000},
{1,0,0x7a08,0xffffffff,0x00000000},
{1,0,0x7a0c,0xffffffff,0x00000000},
{1,0,0x7a10,0xffffffff,0x00000000},
{1,0,0x7a14,0xffffffff,0x00000000},
{1,0,0x7a18,0xffffffff,0x00000000},
{1,0,0x7a1c,0xffffffff,0x00000000},
{1,0,0x7a20,0xffffffff,0x00000000},
{1,0,0x7a24,0xffffffff,0x00000000},
{1,0,0x7a28,0xffffffff,0x00000000},
{1,0,0x7a2c,0xffffffff,0x00000000},
{1,0,0x7a30,0xffffffff,0x00000000},
{1,0,0x7a34,0xffffffff,0x00000000},
{1,0,0x7a38,0xffffffff,0x00000000},
{1,0,0x7a3c,0xffffffff,0x00000000},
{1,0,0x7a40,0xffffffff,0x00000000},
{1,0,0x7a44,0xffffffff,0x00000000},
{1,0,0x7a48,0xffffffff,0x00000000},
{1,0,0x7a4c,0xffffffff,0x00000000},
{1,0,0x7a50,0xffffffff,0x00000000},
{1,0,0x7a54,0xffffffff,0x00000000},
{1,0,0x7a58,0xffffffff,0x00000000},
{1,0,0x7a5c,0xffffffff,0x00000000},
{1,0,0x7a60,0xffffffff,0x00000000},
{1,0,0x7a64,0xffffffff,0x00000000},
{1,0,0x7a68,0xffffffff,0x00000000},
{1,0,0x7a6c,0xffffffff,0x00000000},
{1,0,0x7a70,0xffffffff,0x00000000},
{1,0,0x7a74,0xffffffff,0x00000000},
{1,0,0x7a78,0xffffffff,0x00000000},
{1,0,0x7a7c,0xffffffff,0x00000000},
{1,0,0x7a80,0xffffffff,0x00000000},
{1,0,0x7a84,0xffffffff,0x00000000},
{1,0,0x7a88,0xffffffff,0x00000000},
{1,0,0x7a8c,0xffffffff,0x00000000},
{1,0,0x7a90,0xffffffff,0x00000000},
{1,0,0x7a94,0xffffffff,0x00000000},
{1,0,0x7a98,0xffffffff,0x00000000},
{1,0,0x7a9c,0xffffffff,0x00000000},
{1,0,0x7aa0,0xffffffff,0x00000000},
{1,0,0x7aa4,0xffffffff,0x00000000},
{1,0,0x7aa8,0xffffffff,0x00000000},
{1,0,0x7aac,0xffffffff,0x00000000},
{1,0,0x7ab0,0xffffffff,0x00000000},
{1,0,0x7ab4,0xffffffff,0x00000000},
{1,0,0x7ab8,0xffffffff,0x00000000},
{1,0,0x7abc,0xffffffff,0x00000000},
{1,0,0x7ac0,0xffffffff,0x00000000}};
static const rtw89_rfk_tbl rtw8852b_tssi_dac_gain_defs_b_tbl={rtw8852b_tssi_dac_gain_defs_b,sizeof(rtw8852b_tssi_dac_gain_defs_b)/sizeof(rtw8852b_tssi_dac_gain_defs_b[0])};
static const rtw89_reg5_def rtw8852b_tssi_dck_defs_a[]={{1,0,0x580c,0x0fff0000,0x000},
{1,0,0x5814,0x003ff000,0x0ef},
{1,0,0x5814,0x18000000,0x0}};
static const rtw89_rfk_tbl rtw8852b_tssi_dck_defs_a_tbl={rtw8852b_tssi_dck_defs_a,sizeof(rtw8852b_tssi_dck_defs_a)/sizeof(rtw8852b_tssi_dck_defs_a[0])};
static const rtw89_reg5_def rtw8852b_tssi_dck_defs_b[]={{1,0,0x780c,0x0fff0000,0x000},
{1,0,0x7814,0x003ff000,0x0ef},
{1,0,0x7814,0x18000000,0x0}};
static const rtw89_rfk_tbl rtw8852b_tssi_dck_defs_b_tbl={rtw8852b_tssi_dck_defs_b,sizeof(rtw8852b_tssi_dck_defs_b)/sizeof(rtw8852b_tssi_dck_defs_b[0])};
static const rtw89_reg5_def rtw8852b_tssi_init_txpwr_defs_a[]={{1,0,0x566c,0x00001000,0x0},
{1,0,0x5800,0xffffffff,0x003f807f},
{1,0,0x580c,0x0000007f,0x40},
{1,0,0x580c,0x0fffff00,0x00040},
{1,0,0x5810,0xffffffff,0x59010000},
{1,0,0x5814,0x01ffffff,0x002d000},
{1,0,0x5814,0xf8000000,0x00},
{1,0,0x5818,0xffffffff,0x002c1800},
{1,0,0x581c,0x3fffffff,0x1dc80280},
{1,0,0x5820,0xffffffff,0x00002080},
{1,0,0x580c,0x10000000,0x1},
{1,0,0x580c,0x40000000,0x1},
{1,0,0x5834,0x3fffffff,0x000115f2},
{1,0,0x5838,0x7fffffff,0x0000121},
{1,0,0x5854,0x3fffffff,0x000115f2},
{1,0,0x5858,0x7fffffff,0x0000121},
{1,0,0x5860,0x80000000,0x0},
{1,0,0x5864,0x07ffffff,0x00801ff},
{1,0,0x5898,0xffffffff,0x00000000},
{1,0,0x589c,0xffffffff,0x00000000},
{1,0,0x58a4,0x000000ff,0x16},
{1,0,0x58b0,0xffffffff,0x00000000},
{1,0,0x58b4,0x7fffffff,0x0a002000},
{1,0,0x58b8,0x7fffffff,0x00007628},
{1,0,0x58bc,0x07ffffff,0x7a7807f},
{1,0,0x58c0,0xfffe0000,0x003f},
{1,0,0x58c4,0xffffffff,0x0003ffff},
{1,0,0x58c8,0x00ffffff,0x000000},
{1,0,0x58c8,0xf0000000,0x0},
{1,0,0x58cc,0xffffffff,0x00000000},
{1,0,0x58d0,0x07ffffff,0x2008101},
{1,0,0x58d4,0x000000ff,0x00},
{1,0,0x58d4,0x0003fe00,0x0ff},
{1,0,0x58d4,0x07fc0000,0x100},
{1,0,0x58d8,0xffffffff,0x8008016c},
{1,0,0x58dc,0x0001ffff,0x0807f},
{1,0,0x58dc,0xfff00000,0x800},
{1,0,0x58f0,0x0003ffff,0x001ff},
{1,0,0x58f4,0x000fffff,0x000}};
static const rtw89_rfk_tbl rtw8852b_tssi_init_txpwr_defs_a_tbl={rtw8852b_tssi_init_txpwr_defs_a,sizeof(rtw8852b_tssi_init_txpwr_defs_a)/sizeof(rtw8852b_tssi_init_txpwr_defs_a[0])};
static const rtw89_reg5_def rtw8852b_tssi_init_txpwr_defs_b[]={{1,0,0x566c,0x00001000,0x0},
{1,0,0x7800,0xffffffff,0x003f807f},
{1,0,0x780c,0x0000007f,0x40},
{1,0,0x780c,0x0fffff00,0x00040},
{1,0,0x7810,0xffffffff,0x59010000},
{1,0,0x7814,0x01ffffff,0x002d000},
{1,0,0x7814,0xf8000000,0x00},
{1,0,0x7818,0xffffffff,0x002c1800},
{1,0,0x781c,0x3fffffff,0x1dc80280},
{1,0,0x7820,0xffffffff,0x00002080},
{1,0,0x780c,0x10000000,0x1},
{1,0,0x780c,0x40000000,0x1},
{1,0,0x7834,0x3fffffff,0x000115f2},
{1,0,0x7838,0x7fffffff,0x0000121},
{1,0,0x7854,0x3fffffff,0x000115f2},
{1,0,0x7858,0x7fffffff,0x0000121},
{1,0,0x7860,0x80000000,0x0},
{1,0,0x7864,0x07ffffff,0x00801ff},
{1,0,0x7898,0xffffffff,0x00000000},
{1,0,0x789c,0xffffffff,0x00000000},
{1,0,0x78a4,0x000000ff,0x16},
{1,0,0x78b0,0xffffffff,0x00000000},
{1,0,0x78b4,0x7fffffff,0x0a002000},
{1,0,0x78b8,0x7fffffff,0x00007628},
{1,0,0x78bc,0x07ffffff,0x7a7807f},
{1,0,0x78c0,0xfffe0000,0x003f},
{1,0,0x78c4,0xffffffff,0x0003ffff},
{1,0,0x78c8,0x00ffffff,0x000000},
{1,0,0x78c8,0xf0000000,0x0},
{1,0,0x78cc,0xffffffff,0x00000000},
{1,0,0x78d0,0x07ffffff,0x2008101},
{1,0,0x78d4,0x000000ff,0x00},
{1,0,0x78d4,0x0003fe00,0x0ff},
{1,0,0x78d4,0x07fc0000,0x100},
{1,0,0x78d8,0xffffffff,0x8008016c},
{1,0,0x78dc,0x0001ffff,0x0807f},
{1,0,0x78dc,0xfff00000,0x800},
{1,0,0x78f0,0x0003ffff,0x001ff},
{1,0,0x78f4,0x000fffff,0x000}};
static const rtw89_rfk_tbl rtw8852b_tssi_init_txpwr_defs_b_tbl={rtw8852b_tssi_init_txpwr_defs_b,sizeof(rtw8852b_tssi_init_txpwr_defs_b)/sizeof(rtw8852b_tssi_init_txpwr_defs_b[0])};
static const rtw89_reg5_def rtw8852b_tssi_init_txpwr_he_tb_defs_a[]={{1,0,0x58a0,0xffffffff,0x000000fe},
{1,0,0x58e4,0x0000007f,0x1f}};
static const rtw89_rfk_tbl rtw8852b_tssi_init_txpwr_he_tb_defs_a_tbl={rtw8852b_tssi_init_txpwr_he_tb_defs_a,sizeof(rtw8852b_tssi_init_txpwr_he_tb_defs_a)/sizeof(rtw8852b_tssi_init_txpwr_he_tb_defs_a[0])};
static const rtw89_reg5_def rtw8852b_tssi_init_txpwr_he_tb_defs_b[]={{1,0,0x78a0,0xffffffff,0x000000fe},
{1,0,0x78e4,0x0000007f,0x1f}};
static const rtw89_rfk_tbl rtw8852b_tssi_init_txpwr_he_tb_defs_b_tbl={rtw8852b_tssi_init_txpwr_he_tb_defs_b,sizeof(rtw8852b_tssi_init_txpwr_he_tb_defs_b)/sizeof(rtw8852b_tssi_init_txpwr_he_tb_defs_b[0])};
static const rtw89_reg5_def rtw8852b_tssi_slope_a_defs_2g[]={{1,0,0x5608,0x07ffffff,0x0801008},
{1,0,0x560c,0x07ffffff,0x0201020},
{1,0,0x5610,0x07ffffff,0x0201008},
{1,0,0x5614,0x07ffffff,0x0804008},
{1,0,0x5618,0x07ffffff,0x0201008},
{1,0,0x561c,0x000001ff,0x008},
{1,0,0x561c,0xffff0000,0x0808},
{1,0,0x5620,0xffffffff,0x08081e28},
{1,0,0x5624,0xffffffff,0x08080808},
{1,0,0x5628,0xffffffff,0x08081e28},
{1,0,0x562c,0x0000ffff,0x0808},
{1,0,0x581c,0x00100000,0x1}};
static const rtw89_rfk_tbl rtw8852b_tssi_slope_a_defs_2g_tbl={rtw8852b_tssi_slope_a_defs_2g,sizeof(rtw8852b_tssi_slope_a_defs_2g)/sizeof(rtw8852b_tssi_slope_a_defs_2g[0])};
static const rtw89_reg5_def rtw8852b_tssi_slope_a_defs_5g[]={{1,0,0x5608,0x07ffffff,0x0201008},
{1,0,0x560c,0x07ffffff,0x0201020},
{1,0,0x5610,0x07ffffff,0x0201008},
{1,0,0x5614,0x07ffffff,0x0201008},
{1,0,0x5618,0x07ffffff,0x0201008},
{1,0,0x561c,0x000001ff,0x008},
{1,0,0x561c,0xffff0000,0x0808},
{1,0,0x5620,0xffffffff,0x08081e08},
{1,0,0x5624,0xffffffff,0x08080808},
{1,0,0x5628,0xffffffff,0x08080808},
{1,0,0x562c,0x0000ffff,0x0808},
{1,0,0x581c,0x00100000,0x1}};
static const rtw89_rfk_tbl rtw8852b_tssi_slope_a_defs_5g_tbl={rtw8852b_tssi_slope_a_defs_5g,sizeof(rtw8852b_tssi_slope_a_defs_5g)/sizeof(rtw8852b_tssi_slope_a_defs_5g[0])};
static const rtw89_reg5_def rtw8852b_tssi_slope_b_defs_2g[]={{1,0,0x7608,0x07ffffff,0x0801008},
{1,0,0x760c,0x07ffffff,0x0201020},
{1,0,0x7610,0x07ffffff,0x0201008},
{1,0,0x7614,0x07ffffff,0x0804008},
{1,0,0x7618,0x07ffffff,0x0201008},
{1,0,0x761c,0x000001ff,0x008},
{1,0,0x761c,0xffff0000,0x0808},
{1,0,0x7620,0xffffffff,0x08081e28},
{1,0,0x7624,0xffffffff,0x08080808},
{1,0,0x7628,0xffffffff,0x08081e28},
{1,0,0x762c,0x0000ffff,0x0808},
{1,0,0x781c,0x00100000,0x1}};
static const rtw89_rfk_tbl rtw8852b_tssi_slope_b_defs_2g_tbl={rtw8852b_tssi_slope_b_defs_2g,sizeof(rtw8852b_tssi_slope_b_defs_2g)/sizeof(rtw8852b_tssi_slope_b_defs_2g[0])};
static const rtw89_reg5_def rtw8852b_tssi_slope_b_defs_5g[]={{1,0,0x7608,0x07ffffff,0x0201008},
{1,0,0x760c,0x07ffffff,0x0201020},
{1,0,0x7610,0x07ffffff,0x0201008},
{1,0,0x7614,0x07ffffff,0x0201008},
{1,0,0x7618,0x07ffffff,0x0201008},
{1,0,0x761c,0x000001ff,0x008},
{1,0,0x761c,0xffff0000,0x0808},
{1,0,0x7620,0xffffffff,0x08081e08},
{1,0,0x7624,0xffffffff,0x08080808},
{1,0,0x7628,0xffffffff,0x08080808},
{1,0,0x762c,0x0000ffff,0x0808},
{1,0,0x781c,0x00100000,0x1}};
static const rtw89_rfk_tbl rtw8852b_tssi_slope_b_defs_5g_tbl={rtw8852b_tssi_slope_b_defs_5g,sizeof(rtw8852b_tssi_slope_b_defs_5g)/sizeof(rtw8852b_tssi_slope_b_defs_5g[0])};
static const rtw89_reg5_def rtw8852b_tssi_slope_defs_a[]={{1,0,0x5814,0x00000800,0x1},
{1,0,0x581c,0x20000000,0x1},
{1,0,0x5814,0x20000000,0x1}};
static const rtw89_rfk_tbl rtw8852b_tssi_slope_defs_a_tbl={rtw8852b_tssi_slope_defs_a,sizeof(rtw8852b_tssi_slope_defs_a)/sizeof(rtw8852b_tssi_slope_defs_a[0])};
static const rtw89_reg5_def rtw8852b_tssi_slope_defs_b[]={{1,0,0x7814,0x00000800,0x1},
{1,0,0x781c,0x20000000,0x1},
{1,0,0x7814,0x20000000,0x1}};
static const rtw89_rfk_tbl rtw8852b_tssi_slope_defs_b_tbl={rtw8852b_tssi_slope_defs_b,sizeof(rtw8852b_tssi_slope_defs_b)/sizeof(rtw8852b_tssi_slope_defs_b[0])};
static const rtw89_reg5_def rtw8852b_tssi_sys_a_defs_2g[]={{1,0,0x120c,0x000000ff,0x33},
{1,0,0x12c0,0x0ff00000,0x33},
{1,0,0x58f8,0x40000000,0x1},
{1,0,0x0304,0x0000ff00,0x1e}};
static const rtw89_rfk_tbl rtw8852b_tssi_sys_a_defs_2g_tbl={rtw8852b_tssi_sys_a_defs_2g,sizeof(rtw8852b_tssi_sys_a_defs_2g)/sizeof(rtw8852b_tssi_sys_a_defs_2g[0])};
static const rtw89_reg5_def rtw8852b_tssi_sys_a_defs_5g[]={{1,0,0x120c,0x000000ff,0x44},
{1,0,0x12c0,0x0ff00000,0x44},
{1,0,0x58f8,0x40000000,0x0},
{1,0,0x0304,0x0000ff00,0x1d}};
static const rtw89_rfk_tbl rtw8852b_tssi_sys_a_defs_5g_tbl={rtw8852b_tssi_sys_a_defs_5g,sizeof(rtw8852b_tssi_sys_a_defs_5g)/sizeof(rtw8852b_tssi_sys_a_defs_5g[0])};
static const rtw89_reg5_def rtw8852b_tssi_sys_b_defs_2g[]={{1,0,0x32c0,0x0ff00000,0x33},
{1,0,0x320c,0x000000ff,0x33},
{1,0,0x78f8,0x40000000,0x1},
{1,0,0x0304,0x0000ff00,0x1e}};
static const rtw89_rfk_tbl rtw8852b_tssi_sys_b_defs_2g_tbl={rtw8852b_tssi_sys_b_defs_2g,sizeof(rtw8852b_tssi_sys_b_defs_2g)/sizeof(rtw8852b_tssi_sys_b_defs_2g[0])};
static const rtw89_reg5_def rtw8852b_tssi_sys_b_defs_5g[]={{1,0,0x32c0,0x0ff00000,0x44},
{1,0,0x320c,0x000000ff,0x44},
{1,0,0x78f8,0x40000000,0x0},
{1,0,0x0304,0x0000ff00,0x1d}};
static const rtw89_rfk_tbl rtw8852b_tssi_sys_b_defs_5g_tbl={rtw8852b_tssi_sys_b_defs_5g,sizeof(rtw8852b_tssi_sys_b_defs_5g)/sizeof(rtw8852b_tssi_sys_b_defs_5g[0])};
static const rtw89_reg5_def rtw8852b_tssi_sys_defs[]={{1,0,0x12a8,0x0000000f,0x5},
{1,0,0x32a8,0x0000000f,0x5},
{1,0,0x12bc,0x000ffff0,0x5555},
{1,0,0x32bc,0x000ffff0,0x5555},
{1,0,0x0300,0xff000000,0x16},
{1,0,0x0304,0x000000ff,0x19},
{1,0,0x0314,0xffff0000,0x2041},
{1,0,0x0318,0xffffffff,0x2041},
{1,0,0x0318,0xffffffff,0x20012041},
{1,0,0x0020,0x00006000,0x3},
{1,0,0x0024,0x00006000,0x3},
{1,0,0x0704,0xffff0000,0x601e},
{1,0,0x2704,0xffff0000,0x601e},
{1,0,0x0700,0xf0000000,0x4},
{1,0,0x2700,0xf0000000,0x4},
{1,0,0x0650,0x3c000000,0x0},
{1,0,0x2650,0x3c000000,0x0}};
static const rtw89_rfk_tbl rtw8852b_tssi_sys_defs_tbl={rtw8852b_tssi_sys_defs,sizeof(rtw8852b_tssi_sys_defs)/sizeof(rtw8852b_tssi_sys_defs[0])};
} }
