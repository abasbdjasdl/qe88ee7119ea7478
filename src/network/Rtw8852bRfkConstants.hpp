// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 by tools/import_rfk_reference.py.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace rfk {
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s32=int32_t;
constexpr u32 bit(unsigned n){return u32(1)<<n;}
constexpr u32 mask(unsigned hi,unsigned lo){return (u32(0xffffffff)>>(31-hi))&(u32(0xffffffff)<<lo);}
constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}
constexpr u32 fieldGet(u32 m,u32 v){return (v&m)>>shift(m);}
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
constexpr u32 RTW89_DACK_PATH_NR = 2;
constexpr u32 RTW89_DACK_IDX_NR = 2;
constexpr u32 RTW89_IQK_PATH_NR = 4;
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
struct rtw89_reg3_def {u32 addr,mask,data;};
struct rtw89_reg5_def {u8 flag,path;u32 addr,mask,data;};
struct rtw89_rfk_tbl {const rtw89_reg5_def *defs;u32 size;};
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
} }
