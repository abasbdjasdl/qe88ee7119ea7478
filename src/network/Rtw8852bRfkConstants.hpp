// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 by tools/import_rfk_reference.py.
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace rfk {
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s32=int32_t;
constexpr u32 bit(unsigned n){return u32(1)<<n;}
constexpr u32 mask(unsigned hi,unsigned lo){return (u32(0xffffffff)>>(31-hi))&(u32(0xffffffff)<<lo);}
constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}
constexpr u32 fieldGet(u32 m,u32 v){return (v&m)>>shift(m);}
constexpr s32 sign_extend32(u32 v,unsigned sign){return (v&(u32(1)<<sign))?s32(v&((u32(1)<<sign)-1))-s32(u32(1)<<sign):s32(v);}
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
constexpr u32 RR_DCK1 = 0x93;
constexpr u32 RR_DCK1_CLR = mask(3, 0);
constexpr u32 RR_DCK = 0x92;
constexpr u32 RR_DCK_LV = bit(0);
constexpr u32 RTW8852B_RXDCK_VER = 0x1;
constexpr u32 RF_PATH_NUM_8852B = 2;
constexpr u32 RR_RSV1 = 0x05;
constexpr u32 RFREG_MASK = 0xfffff;
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
constexpr u32 MASKDWORD = 0xffffffff;
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
constexpr u32 RF_PATH_MAX = 4;
constexpr u32 R_DPD_BF = 0x44a0;
constexpr u32 B_DPD_BF_OFDM = mask(16, 12);
constexpr u32 B_DPD_BF_SCA = mask(6, 0);
constexpr u32 R_DPD_CH0A = 0x81BC;
constexpr u32 B_DPD_CFG = mask(22, 0);
constexpr u32 RTW89_DACK_PATH_NR = 2;
constexpr u32 RTW89_DACK_IDX_NR = 2;
constexpr u32 R_AX_WCPU_FW_CTRL = 0x01E0;
constexpr u32 B_AX_WCPU_FWDL_STS_MASK = mask(7, 5);
constexpr u32 R_AX_CMAC_FUNC_EN = 0xC000;
constexpr u32 B_AX_CMAC_EN = bit(30);
constexpr u32 R_AX_SYS_FUNC_EN = 0x0002;
constexpr u32 B_AX_FEN_BBRSTB = bit(0);
constexpr u32 B_AX_FEN_BB_GLB_RSTN = bit(1);
constexpr u32 R_AX_CTN_TXEN = 0xC348;
constexpr u32 B_AX_CTN_TXEN_ALL_MASK = mask(15, 0);
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
struct rtw89_reg5_def {u8 flag,path;u32 addr,mask,data;};
struct rtw89_rfk_tbl {const rtw89_reg5_def *defs;u32 size;};
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
