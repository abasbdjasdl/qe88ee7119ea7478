// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 by tools/import_rfk_reference.py.
static void _rfk_backup_bb_reg(Context *rtwdev, u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		backup_bb_reg_val[i] =
			rtw89_phy_read32_mask(rtwdev, rtw8852b_backup_bb_regs[i],
					      MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]backup bb reg : %x, value =%x\n",
			    rtw8852b_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_backup_rf_reg(Context *rtwdev, u32 backup_rf_reg_val[],
			       u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		backup_rf_reg_val[i] =
			rtw89_read_rf(rtwdev, rf_path,
				      rtw8852b_backup_rf_regs[i], RFREG_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]backup rf S%d reg : %x, value =%x\n", rf_path,
			    rtw8852b_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _rfk_restore_bb_reg(Context *rtwdev,
				const u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, rtw8852b_backup_bb_regs[i],
				       MASKDWORD, backup_bb_reg_val[i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]restore bb reg : %x, value =%x\n",
			    rtw8852b_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_restore_rf_reg(Context *rtwdev,
				const u32 backup_rf_reg_val[], u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		rtw89_write_rf(rtwdev, rf_path, rtw8852b_backup_rf_regs[i],
			       RFREG_MASK, backup_rf_reg_val[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]restore rf S%d reg: %x, value =%x\n", rf_path,
			    rtw8852b_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _rfk_rf_direct_cntrl(Context *rtwdev,
				 u8 path, bool is_bybb)
{
	if (is_bybb)
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
}

static void _rfk_drf_direct_cntrl(Context *rtwdev,
				  u8 path, bool is_bybb)
{
	if (is_bybb)
		rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x0);
}

static bool _iqk_check_cal(Context *rtwdev, u8 path)
{
	bool fail = true;
	u32 val;
	int ret;

	ret = R16_RFK_POLL(rtw89_phy_read32_mask, val, val == 0x55,
				       1, 8200, false, rtwdev, 0xbff8, MASKBYTE0);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]NCTL1 IQK timeout!!!\n");

	delay(rtwdev,200);

	if (!ret)
		fail = rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, MASKBYTE0, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, ret=%d\n", path, ret);
	val = rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8008 = 0x%x\n", path, val);

	return fail;
}

static u8 _kpath(Context *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u8 val;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]dbcc_en: %x,PHY%d\n",
		    rtwdev->dbcc_en, phy_idx);

	if (!rtwdev->dbcc_en) {
		val = RF_AB;
	} else {
		if (phy_idx == RTW89_PHY_0)
			val = RF_A;
		else
			val = RF_B;
	}
	return val;
}

static void _set_rx_dck(Context *rtwdev, enum rtw89_phy_idx phy,
			u8 path)
{
	rtw89_write_rf(rtwdev, path, RR_DCK1, RR_DCK1_CLR, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x1);
	delay(rtwdev,1000*(1));
}

static void _rx_dck(Context *rtwdev, enum rtw89_phy_idx phy)
{
	u8 path, dck_tune;
	u32 rf_reg5;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ****** RXDCK Start (Ver: 0x%x, CV : 0x%x) ******\n",
		    RTW8852B_RXDCK_VER, rtwdev->hal.cv);

	for (path = 0; path < RF_PATH_NUM_8852B; path++) {
		rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);
		dck_tune = rtw89_read_rf(rtwdev, path, RR_DCK, RR_DCK_FINE);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev,
					       R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x1);

		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
		rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_FINE, 0x0);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
		_set_rx_dck(rtwdev, phy, path);
		rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_FINE, dck_tune);
		rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev,
					       R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x0);
	}
}

static void _rck(Context *rtwdev, u8 path)
{
	u32 rf_reg5;
	u32 rck_val;
	u32 val;
	int ret;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] ====== S%d RCK ======\n", path);

	rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] RF0x00 = 0x%05x\n",
		    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK));

	/* RCK trigger */
	rtw89_write_rf(rtwdev, path, RR_RCKC, RFREG_MASK, 0x00240);

	ret = R16_RFK_POLL(rtw89_read_rf, val, val, 2, 30,
				       false, rtwdev, path, RR_RCKS, bit(3));

	rck_val = rtw89_read_rf(rtwdev, path, RR_RCKC, RR_RCKC_CA);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] rck_val = 0x%x, ret = %d\n",
		    rck_val, ret);

	rtw89_write_rf(rtwdev, path, RR_RCKC, RFREG_MASK, rck_val);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] RF 0x1b = 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_RCKC, RFREG_MASK));
}

static void _afe_init(Context *rtwdev)
{
	rtw89_write32(rtwdev, R_AX_PHYREG_SET, 0xf);

	rtw89_rfk_parser(rtwdev, &rtw8852b_afe_init_defs_tbl);
}

static void _drck(Context *rtwdev)
{
	u32 rck_d;
	u32 val;
	int ret;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]Ddie RCK start!!!\n");
	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_KICK, 0x1);

	ret = R16_RFK_POLL(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, R_DRCK_RS, B_DRCK_RS_DONE);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DRCK timeout\n");

	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_KICK, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_FH, B_DRCK_LAT, 0x1);
	delay(rtwdev,1);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_FH, B_DRCK_LAT, 0x0);
	rck_d = rtw89_phy_read32_mask(rtwdev, R_DRCK_RS, B_DRCK_RS_LPS);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_SEL, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_CV, rck_d);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0xc0cc = 0x%x\n",
		    rtw89_phy_read32_mask(rtwdev, R_DRCK_V1, MASKDWORD));
}

static void _addck_backup(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x0);
	dack->addck_d[0][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_A0);
	dack->addck_d[0][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_A1);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1, 0x0);
	dack->addck_d[1][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR1, B_ADDCKR1_A0);
	dack->addck_d[1][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR1, B_ADDCKR1_A1);
}

static void _addck_reload(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	/* S0 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK0D_VAL, dack->addck_d[0][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_VAL, dack->addck_d[0][1] >> 6);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK0D_VAL2, dack->addck_d[0][1] & 0x3f);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_MAN, 0x3);

	/* S1 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1D, B_ADDCK1D_VAL, dack->addck_d[1][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK0_VAL, dack->addck_d[1][1] >> 6);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1D, B_ADDCK1D_VAL2, dack->addck_d[1][1] & 0x3f);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_MAN, 0x3);
}

static void _dack_backup_s0(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);

	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_DCOF0, B_DCOF0_V, i);
		dack->msbk_d[0][0][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK_S0P2, B_DACK_S0M0);
		rtw89_phy_write32_mask(rtwdev, R_DCOF8, B_DCOF8_V, i);
		dack->msbk_d[0][1][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK_S0P3, B_DACK_S0M1);
	}

	dack->biask_d[0][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS00, B_DACK_BIAS00);
	dack->biask_d[0][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS01, B_DACK_BIAS01);

	dack->dadck_d[0][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK00, B_DACK_DADCK00);
	dack->dadck_d[0][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK01, B_DACK_DADCK01);
}

static void _dack_backup_s1(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);

	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_DACK10, B_DACK10, i);
		dack->msbk_d[1][0][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK10S, B_DACK10S);
		rtw89_phy_write32_mask(rtwdev, R_DACK11, B_DACK11, i);
		dack->msbk_d[1][1][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK11S, B_DACK11S);
	}

	dack->biask_d[1][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS10, B_DACK_BIAS10);
	dack->biask_d[1][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS11, B_DACK_BIAS11);

	dack->dadck_d[1][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK10, B_DACK_DADCK10);
	dack->dadck_d[1][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK11, B_DACK_DADCK11);
}

static void _check_addc(Context *rtwdev, u8 path)
{
	s32 dc_re = 0, dc_im = 0;
	u32 tmp;
	u32 i;

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_check_addc_defs_a_tbl,
				 &rtw8852b_check_addc_defs_b_tbl);

	for (i = 0; i < ADDC_T_AVG; i++) {
		tmp = rtw89_phy_read32_mask(rtwdev, R_DBG32_D, MASKDWORD);
		dc_re += sign_extend32(fieldGet(0xfff000, tmp), 11);
		dc_im += sign_extend32(fieldGet(0xfff, tmp), 11);
	}

	dc_re /= ADDC_T_AVG;
	dc_im /= ADDC_T_AVG;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S%d,dc_re = 0x%x,dc_im =0x%x\n", path, dc_re, dc_im);
}

static void _addck(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 val;
	int ret;

	/* S0 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_MAN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1, 0x30, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xf);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1, bit(1), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0x3);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]before S0 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_TRG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_TRG, 0x0);
	delay(rtwdev,1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x1);

	ret = R16_RFK_POLL(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, R_ADDCKR0, bit(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADDCK timeout\n");
		dack->addck_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1, bit(1), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xc);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);

	/* S1 */
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xf);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1, bit(1), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0x3);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]before S1 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_B);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_TRG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_TRG, 0x0);
	delay(rtwdev,1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1, 0x1);

	ret = R16_RFK_POLL(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, R_ADDCKR1, bit(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADDCK timeout\n");
		dack->addck_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_B);

	rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1, bit(1), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xc);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);
}

static void _check_dadc(Context *rtwdev, u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_check_dadc_en_defs_a_tbl,
				 &rtw8852b_check_dadc_en_defs_b_tbl);

	_check_addc(rtwdev, path);

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_check_dadc_dis_defs_a_tbl,
				 &rtw8852b_check_dadc_dis_defs_b_tbl);
}

static bool _dack_s0_check_done(Context *rtwdev, bool part1)
{
	if (part1) {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S0P0, B_DACK_S0P0_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P1, B_DACK_S0P1_OK) == 0)
			return false;
	} else {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S0P2, B_DACK_S0P2_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P3, B_DACK_S0P3_OK) == 0)
			return false;
	}

	return true;
}

static void _dack_s0(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s0_1_defs_tbl);

	ret = R16_RFK_POLL(_dack_s0_check_done, done, done, 1, 10000,
				       false, rtwdev, true);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK timeout\n");
		dack->msbk_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s0_2_defs_tbl);

	ret = R16_RFK_POLL(_dack_s0_check_done, done, done, 1, 10000,
				       false, rtwdev, false);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DADCK timeout\n");
		dack->dadck_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s0_3_defs_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 DADCK\n");

	_dack_backup_s0(rtwdev);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);
}

static bool _dack_s1_check_done(Context *rtwdev, bool part1)
{
	if (part1) {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S1P0, B_DACK_S1P0_OK) == 0 &&
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S1P1, B_DACK_S1P1_OK) == 0)
			return false;
	} else {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK10S, B_DACK_S1P2_OK) == 0 &&
		    rtw89_phy_read32_mask(rtwdev, R_DACK11S, B_DACK_S1P3_OK) == 0)
			return false;
	}

	return true;
}

static void _dack_s1(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s1_1_defs_tbl);

	ret = R16_RFK_POLL(_dack_s1_check_done, done, done, 1, 10000,
				       false, rtwdev, true);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK timeout\n");
		dack->msbk_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s1_2_defs_tbl);

	ret = R16_RFK_POLL(_dack_s1_check_done, done, done, 1, 10000,
				       false, rtwdev, false);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 DADCK timeout\n");
		dack->dadck_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s1_3_defs_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 DADCK\n");

	_check_dadc(rtwdev, RF_PATH_B);
	_dack_backup_s1(rtwdev);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);
}

static void _dack(Context *rtwdev)
{
	_dack_s0(rtwdev);
	_dack_s1(rtwdev);
}

static void _dack_dump(Context *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;
	u8 t;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S0 ADC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->addck_d[0][0], dack->addck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S1 ADC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->addck_d[1][0], dack->addck_d[1][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S0 DAC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->dadck_d[0][0], dack->dadck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S1 DAC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->dadck_d[1][0], dack->dadck_d[1][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S0 biask ic = 0x%x, qc = 0x%x\n",
		    dack->biask_d[0][0], dack->biask_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S1 biask ic = 0x%x, qc = 0x%x\n",
		    dack->biask_d[1][0], dack->biask_d[1][1]);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK ic:\n");
	for (i = 0; i < 0x10; i++) {
		t = dack->msbk_d[0][0][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK qc:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[0][1][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK ic:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[1][0][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK qc:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[1][1][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}
}

static void _dac_cal(Context *rtwdev, bool force)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 rf0_0, rf1_0;

	dack->dack_done = false;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK 0x1\n");
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK start!!!\n");

	rf0_0 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK);
	rf1_0 = rtw89_read_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK);
	_afe_init(rtwdev);
	_drck(rtwdev);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x337e1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, 0x337e1);
	_addck(rtwdev);
	_addck_backup(rtwdev);
	_addck_reload(rtwdev);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT, RFREG_MASK, 0x0);
	_dack(rtwdev);
	_dack_dump(rtwdev);
	dack->dack_done = true;

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, rf0_0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, rf1_0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x1);
	dack->dack_cnt++;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK finish!!!\n");
}

static void _iqk_rxk_setting(Context *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL2G, 0x1);
		tmp = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
		rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK, tmp);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x1);
		tmp = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
		rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK, tmp);
		break;
	default:
		break;
	}
}

static bool _iqk_one_shot(Context *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path, u8 ktype)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 iqk_cmd;
	bool fail;

	switch (ktype) {
	case ID_FLOK_COARSE:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x108 | (1 << (4 + path));
		break;
	case ID_FLOK_FINE:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x208 | (1 << (4 + path));
		break;
	case ID_FLOK_VBUFFER:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_TXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0x8 + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_RXAGC:
		iqk_cmd = 0x508 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_RXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0xb + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_NBTXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x011);
		iqk_cmd = 0x408 | (1 << (4 + path));
		break;
	case ID_NBRXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		iqk_cmd = 0x608 | (1 << (4 + path));
		break;
	default:
		return false;
	}

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, iqk_cmd + 1);
	delay(rtwdev,1);
	fail = _iqk_check_cal(rtwdev, path);
	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);

	return fail;
}

static bool _rxk_group_sel(Context *rtwdev, enum rtw89_phy_idx phy_idx,
			   u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool fail;
	u8 gp;

	for (gp = 0; gp < RTW8852B_RXK_GROUP_NR; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
				       _g_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2G,
				       _g_idxattc2[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C1G,
				       _g_idxattc1[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
				       _a_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_HATT,
				       _a_idxattc2[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_CC2,
				       _a_idxattc1[gp]);
			break;
		default:
			break;
		}

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SET, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_GP_V1, gp);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);
		rtw89_phy_write32_mask(rtwdev, R_IQKINF,
				       bit(16 + gp + path * 4), fail);
		kfail |= fail;
	}
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x0);

	if (kfail) {
		iqk_info->nb_rxcfir[path] = 0x40000002;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_RXCFIR, 0x0);
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->nb_rxcfir[path] = 0x40000000;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_RXCFIR, 0x5);
		iqk_info->is_wb_rxiqk[path] = true;
	}

	return kfail;
}

static bool _iqk_nbrxk(Context *rtwdev, enum rtw89_phy_idx phy_idx,
		       u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	const u8 gp = 0x3;
	bool kfail = false;
	bool fail;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
			       _g_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2G,
			       _g_idxattc2[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C1G,
			       _g_idxattc1[gp]);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
			       _a_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_HATT,
			       _a_idxattc2[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_CC2,
			       _a_idxattc1[gp]);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SET, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP_V1, gp);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
	delay(rtwdev,1);

	fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, bit(16 + gp + path * 4), fail);
	kfail |= fail;
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x0);

	if (!kfail)
		iqk_info->nb_rxcfir[path] =
			 rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD) | 0x2;
	else
		iqk_info->nb_rxcfir[path] = 0x40000002;

	return kfail;
}

static void _iqk_rxclk_setting(Context *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	if (iqk_info->iqk_bw[path] == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
		delay(rtwdev,1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x0f);
		delay(rtwdev,1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x03);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa001);
		delay(rtwdev,1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa041);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_VAL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_VAL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_VAL, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
		delay(rtwdev,1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x0f);
		delay(rtwdev,1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x03);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa001);
		delay(rtwdev,1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa041);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_VAL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_VAL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_VAL, 0x0);
	}
}

static bool _txk_group_sel(Context *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool fail;
	u8 gp;

	for (gp = 0x0; gp < RTW8852B_RXK_GROUP_NR; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _g_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _g_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _g_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
					       MASKDWORD, _g_itqt[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _a_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _a_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _a_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
					       MASKDWORD, _a_itqt[gp]);
			break;
		default:
			break;
		}

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SET, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_G2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_TXK);
		rtw89_phy_write32_mask(rtwdev, R_IQKINF,
				       bit(8 + gp + path * 4), fail);
		kfail |= fail;
	}

	if (kfail) {
		iqk_info->nb_txcfir[path] = 0x40000002;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_TXCFIR, 0x0);
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		iqk_info->nb_txcfir[path] = 0x40000000;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_TXCFIR, 0x5);
		iqk_info->is_wb_txiqk[path] = true;
	}

	return kfail;
}

static bool _iqk_nbtxk(Context *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail;
	u8 gp = 0x2;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
			       _g_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
			       _g_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
			       _g_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _g_itqt[gp]);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
			       _a_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
			       _a_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
			       _a_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _a_itqt[gp]);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SET, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G2, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP, gp);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	kfail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);

	if (!kfail)
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8),
					      MASKDWORD) | 0x2;
	else
		iqk_info->nb_txcfir[path] = 0x40000002;

	return kfail;
}

static void _lok_res_table(Context *rtwdev, u8 path, u8 ibias)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, ibias = %x\n", path, ibias);

	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x2);
	if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, 0x0);
	else
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, ibias);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_TXVBUF, RR_TXVBUF_DACEN, 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x7c = %x\n", path,
		    rtw89_read_rf(rtwdev, path, RR_TXVBUF, RFREG_MASK));
}

static bool _lok_finetune_check(Context *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool is_fail1, is_fail2;
	u32 vbuff_i;
	u32 vbuff_q;
	u32 core_i;
	u32 core_q;
	u32 tmp;
	u8 ch;

	tmp = rtw89_read_rf(rtwdev, path, RR_TXMO, RFREG_MASK);
	core_i = fieldGet(RR_TXMO_COI, tmp);
	core_q = fieldGet(RR_TXMO_COQ, tmp);
	ch = (iqk_info->iqk_times / 2) % RTW89_IQK_CHS_NR;

	if (core_i < 0x2 || core_i > 0x1d || core_q < 0x2 || core_q > 0x1d)
		is_fail1 = true;
	else
		is_fail1 = false;

	iqk_info->lok_idac[ch][path] = tmp;

	tmp = rtw89_read_rf(rtwdev, path, RR_LOKVB, RFREG_MASK);
	vbuff_i = fieldGet(RR_LOKVB_COI, tmp);
	vbuff_q = fieldGet(RR_LOKVB_COQ, tmp);

	if (vbuff_i < 0x2 || vbuff_i > 0x3d || vbuff_q < 0x2 || vbuff_q > 0x3d)
		is_fail2 = true;
	else
		is_fail2 = false;

	iqk_info->lok_vbuf[ch][path] = tmp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, lok_idac[%x][%x] = 0x%x\n", path, ch, path,
		    iqk_info->lok_idac[ch][path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, lok_vbuf[%x][%x] = 0x%x\n", path, ch, path,
		    iqk_info->lok_vbuf[ch][path]);

	return is_fail1 | is_fail2;
}

static bool _iqk_lok(Context *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool tmp, vbuffer_fail;

	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x6);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x4);
		break;
	default:
		break;
	}

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x9);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_COARSE);
	iqk_info->lok_cor_fail[0][path] = tmp;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x24);
	vbuffer_fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x9);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_FINE);
	iqk_info->lok_fin_fail[0][path] = tmp;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x24);
	vbuffer_fail |= _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);

	return _lok_finetune_check(rtwdev, path) | vbuffer_fail |
	       iqk_info->lok_cor_fail[0][path] | iqk_info->lok_fin_fail[0][path];
}

static void _iqk_txk_setting(Context *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_XALNA2, RR_XALNA2_SW2, 0x00);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT2, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT1, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG2, RR_TXG2_ATT0, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M1, 0x00);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_IQK, 0x403e);
		delay(rtwdev,1);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_XGLNA2, RR_XGLNA2_SW, 0x00);
		rtw89_write_rf(rtwdev, path, RR_BIASA, RR_BIASA_A, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M1, 0x80);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_IQK, 0x403e);
		delay(rtwdev,1);
		break;
	default:
		break;
	}
}

static void _iqk_txclk_setting(Context *rtwdev, u8 path)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
	delay(rtwdev,1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x1f);
	delay(rtwdev,1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0001);
	delay(rtwdev,1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0041);
}

static void _iqk_info_iqk(Context *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp;
	bool flag;

	flag = iqk_info->lok_cor_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_FCOR << (path * 4), flag);
	flag = iqk_info->lok_fin_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_FFIN << (path * 4), flag);
	flag = iqk_info->iqk_tx_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_FTX << (path * 4), flag);
	flag = iqk_info->iqk_rx_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_F_RX << (path * 4), flag);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQK_RES + (path << 8), MASKDWORD);
	iqk_info->bp_iqkenable[path] = tmp;
	tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
	iqk_info->bp_txkresult[path] = tmp;
	tmp = rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD);
	iqk_info->bp_rxkresult[path] = tmp;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_KCNT, iqk_info->iqk_times);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQKINF, B_IQKINF_FAIL << (path * 4));
	if (tmp)
		iqk_info->iqk_fail_cnt++;
	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_FCNT << (path * 4),
			       iqk_info->iqk_fail_cnt);
}

static void _iqk_by_path(Context *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool lok_is_fail = false;
	const int attemptLimit = 3;
	u8 ibias = 0x1;
	u8 i;

	_iqk_txclk_setting(rtwdev, path);

	/* LOK */
	for (i = 0; i < attemptLimit; i++) {
		_lok_res_table(rtwdev, path, ibias++);
		_iqk_txk_setting(rtwdev, path);
		lok_is_fail = _iqk_lok(rtwdev, phy_idx, path);
		if (!lok_is_fail)
			break;
	}

	rtwdev->iqk.lok_fail[path] = lok_is_fail;
	if (lok_is_fail)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] LOK (%d) fail\n", path);

	/* TXK */
	if (iqk_info->is_nbiqk)
		iqk_info->iqk_tx_fail[0][path] = _iqk_nbtxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_tx_fail[0][path] = _txk_group_sel(rtwdev, phy_idx, path);

	/* RX */
	_iqk_rxclk_setting(rtwdev, path);
	_iqk_rxk_setting(rtwdev, path);
	if (iqk_info->is_nbiqk)
		iqk_info->iqk_rx_fail[0][path] = _iqk_nbrxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_rx_fail[0][path] = _rxk_group_sel(rtwdev, phy_idx, path);

	_iqk_info_iqk(rtwdev, phy_idx, path);
}

static void _iqk_get_ch_info(Context *rtwdev, enum rtw89_phy_idx phy, u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 reg_rf18;
	u32 reg_35c;
	u8 idx;
	u8 get_empty_table = false;

	for (idx = 0; idx < RTW89_IQK_CHS_NR; idx++) {
		if (iqk_info->iqk_mcc_ch[idx][path] == 0) {
			get_empty_table = true;
			break;
		}
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] (1)idx = %x\n", idx);

	if (!get_empty_table) {
		idx = iqk_info->iqk_table_idx[path] + 1;
		if (idx > 1)
			idx = 0;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] (2)idx = %x\n", idx);

	reg_rf18 = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	reg_35c = rtw89_phy_read32_mask(rtwdev, R_CIRST, B_CIRST_SYN);

	iqk_info->iqk_band[path] = chan->band_type;
	iqk_info->iqk_bw[path] = chan->band_width;
	iqk_info->iqk_ch[path] = chan->channel;
	iqk_info->iqk_mcc_ch[idx][path] = chan->channel;
	iqk_info->iqk_table_idx[path] = idx;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x18= 0x%x, idx = %x\n",
		    path, reg_rf18, idx);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x18= 0x%x\n",
		    path, reg_rf18);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]times = 0x%x, ch =%x\n",
		    iqk_info->iqk_times, idx);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]iqk_mcc_ch[%x][%x] = 0x%x\n",
		    idx, path, iqk_info->iqk_mcc_ch[idx][path]);

	if (reg_35c == 0x01)
		iqk_info->syn1to2 = 0x1;
	else
		iqk_info->syn1to2 = 0x0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, iqk_info->syn1to2= 0x%x\n", path,
		    iqk_info->syn1to2);

	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_VER, RTW8852B_IQK_VER);
	/* 2GHz/5GHz/6GHz = 0/1/2 */
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_BAND << (path * 16),
			       iqk_info->iqk_band[path]);
	/* 20/40/80 = 0/1/2 */
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_BW << (path * 16),
			       iqk_info->iqk_bw[path]);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_CH << (path * 16),
			       iqk_info->iqk_ch[path]);
}

static void _iqk_start_iqk(Context *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	_iqk_by_path(rtwdev, phy_idx, path);
}

static void _iqk_restore(Context *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail;

	rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD,
			       iqk_info->nb_txcfir[path]);
	rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD,
			       iqk_info->nb_rxcfir[path]);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD,
			       0x00000e19 + (path << 4));
	fail = _iqk_check_cal(rtwdev, path);
	rtwdev->restoreFailed[path] = fail;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "%s result =%x\n", __func__, fail);

	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS, B_IQK_RES_K, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_IQRSN, B_IQRSN_K1, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_IQRSN, B_IQRSN_K2, 0x0);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0x3);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
	rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x1);
}

static void _iqk_afebb_restore(Context *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	const struct rtw89_reg3_def *def;
	int size;
	u8 kpath;
	int i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "===> %s\n", __func__);

	kpath = _kpath(rtwdev, phy_idx);

	switch (kpath) {
	case RF_A:
	case RF_B:
		return;
	default:
		size = arraySize(rtw8852b_restore_nondbcc_path01);
		def = rtw8852b_restore_nondbcc_path01;
		break;
	}

	for (i = 0; i < size; i++, def++)
		rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
}

static void _iqk_preset(Context *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx;

	idx = iqk_info->iqk_table_idx[path];
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] (3)idx = %x\n", idx);

	rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8), B_COEF_SEL_IQC, idx);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3, idx);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x81ff010a);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK](1)S%x, 0x8%x54 = 0x%x\n", path, 1 << path,
		    rtw89_phy_read32_mask(rtwdev, R_CFIR_LUT + (path << 8), MASKDWORD));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK](1)S%x, 0x8%x04 = 0x%x\n", path, 1 << path,
		    rtw89_phy_read32_mask(rtwdev, R_COEF_SEL + (path << 8), MASKDWORD));
}

static void _iqk_macbb_setting(Context *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	const struct rtw89_reg3_def *def;
	int size;
	u8 kpath;
	int i;

	kpath = _kpath(rtwdev, phy_idx);

	switch (kpath) {
	case RF_A:
	case RF_B:
		return;
	default:
		size = arraySize(rtw8852b_set_nondbcc_path01);
		def = rtw8852b_set_nondbcc_path01;
		break;
	}

	for (i = 0; i < size; i++, def++)
		rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
}

static void _iqk_init(Context *rtwdev)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx, path;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF, MASKDWORD, 0x0);
	if (iqk_info->is_iqk_init)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	iqk_info->is_iqk_init = true;
	iqk_info->is_nbiqk = false;
	iqk_info->iqk_fft_en = false;
	iqk_info->iqk_sram_en = false;
	iqk_info->iqk_cfir_en = false;
	iqk_info->iqk_xym_en = false;
	iqk_info->iqk_times = 0x0;

	for (idx = 0; idx < RTW89_IQK_CHS_NR; idx++) {
		iqk_info->iqk_channel[idx] = 0x0;
		for (path = 0; path < RTW8852B_IQK_SS; path++) {
			iqk_info->lok_cor_fail[idx][path] = false;
			iqk_info->lok_fin_fail[idx][path] = false;
			iqk_info->iqk_tx_fail[idx][path] = false;
			iqk_info->iqk_rx_fail[idx][path] = false;
			iqk_info->iqk_mcc_ch[idx][path] = 0x0;
			iqk_info->iqk_table_idx[path] = 0x0;
		}
	}
}

static void _wait_rx_mode(Context *rtwdev, u8 kpath)
{
	u32 rf_mode;
	u8 path;
	int ret;

	for (path = 0; path < RF_PATH_MAX; path++) {
		if (!(kpath & bit(path)))
			continue;

		ret = R16_RFK_POLL(rtw89_read_rf, rf_mode,
					       rf_mode != 2, 2, 5000, false,
					       rtwdev, path, RR_MOD, RR_MOD_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK] Wait S%d to Rx mode!! (ret = %d)\n", path, ret);
	}
}

static void _doiqk(Context *rtwdev, bool force,
		   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 backup_bb_val[BACKUP_BB_REGS_NR];
	u32 backup_rf_val[RTW8852B_IQK_SS][BACKUP_RF_REGS_NR];
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]==========IQK start!!!!!==========\n");
	iqk_info->iqk_times++;
	iqk_info->version = RTW8852B_IQK_VER;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]Test Ver 0x%x\n", iqk_info->version);
	_iqk_get_ch_info(rtwdev, phy_idx, path);

	_rfk_backup_bb_reg(rtwdev, &backup_bb_val[0]);
	_rfk_backup_rf_reg(rtwdev, &backup_rf_val[path][0], path);
	_iqk_macbb_setting(rtwdev, phy_idx, path);
	_iqk_preset(rtwdev, path);
	_iqk_start_iqk(rtwdev, phy_idx, path);
	_iqk_restore(rtwdev, path);
	_iqk_afebb_restore(rtwdev, phy_idx, path);
	_rfk_restore_bb_reg(rtwdev, &backup_bb_val[0]);
	_rfk_restore_rf_reg(rtwdev, &backup_rf_val[path][0], path);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_STOP);
}

static void _iqk(Context *rtwdev, enum rtw89_phy_idx phy_idx, bool force)
{
	u8 kpath = _kpath(rtwdev, phy_idx);

	switch (kpath) {
	case RF_A:
		_doiqk(rtwdev, force, phy_idx, RF_PATH_A);
		break;
	case RF_B:
		_doiqk(rtwdev, force, phy_idx, RF_PATH_B);
		break;
	case RF_AB:
		_doiqk(rtwdev, force, phy_idx, RF_PATH_A);
		_doiqk(rtwdev, force, phy_idx, RF_PATH_B);
		break;
	default:
		break;
	}
}

static void _dpk_bkup_kip(Context *rtwdev, const u32 reg[],
			  u32 reg_bkup[][RTW8852B_DPK_KIP_REG_NUM], u8 path)
{
	u8 i;

	for (i = 0; i < RTW8852B_DPK_KIP_REG_NUM; i++) {
		reg_bkup[path][i] =
			rtw89_phy_read32_mask(rtwdev, reg[i] + (path << 8), MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Backup 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static void _dpk_reload_kip(Context *rtwdev, const u32 reg[],
			    const u32 reg_bkup[][RTW8852B_DPK_KIP_REG_NUM], u8 path)
{
	u8 i;

	for (i = 0; i < RTW8852B_DPK_KIP_REG_NUM; i++) {
		rtw89_phy_write32_mask(rtwdev, reg[i] + (path << 8), MASKDWORD,
				       reg_bkup[path][i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Reload 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static u8 _dpk_order_convert(Context *rtwdev)
{
	u8 order;
	u8 val;

	order = rtw89_phy_read32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP);
	val = 0x3 >> order;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] convert MDPD order to 0x%x\n", val);

	return val;
}

static void _dpk_onoff(Context *rtwdev, u8 path, bool off)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 val, kidx = dpk->cur_idx[path];

	val = dpk->is_dpk_enable && !off && dpk->bp[path][kidx].path_ok;

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       MASKBYTE3, _dpk_order_convert(rtwdev) << 1 | val);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] DPK %s !!!\n", path,
		    kidx, dpk->is_dpk_enable && !off ? "enable" : "disable");
}

static void _dpk_one_shot(Context *rtwdev, enum rtw89_phy_idx phy,
			  u8 path, enum rtw8852b_dpk_id id)
{
	u16 dpk_cmd;
	u32 val;
	int ret;

	dpk_cmd = (id << 8) | (0x19 + (path << 4));
	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, dpk_cmd);

	ret = R16_RFK_POLL(rtw89_phy_read32_mask, val, val == 0x55,
				       1, 20000, false,
				       rtwdev, 0xbff8, MASKBYTE0);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] one-shot over 20ms!!!!\n");

	delay(rtwdev,1);

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00030000);

	ret = R16_RFK_POLL(rtw89_phy_read32_mask, val, val == 0x8000,
				       1, 2000, false,
				       rtwdev, 0x80fc, MASKLWORD);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] one-shot over 20ms!!!!\n");

	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, MASKBYTE0, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] one-shot for %s = 0x%x\n",
		    id == 0x06 ? "LBK_RXIQK" :
		    id == 0x10 ? "SYNC" :
		    id == 0x11 ? "MDPK_IDL" :
		    id == 0x12 ? "MDPK_MPA" :
		    id == 0x13 ? "GAIN_LOSS" :
		    id == 0x14 ? "PWR_CAL" :
		    id == 0x15 ? "DPK_RXAGC" :
		    id == 0x16 ? "KIP_PRESET" :
		    id == 0x17 ? "KIP_RESTORE" : "DPK_TXAGC",
		    dpk_cmd);
}

static void _dpk_rx_dck(Context *rtwdev, enum rtw89_phy_idx phy,
			u8 path)
{
	rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_EN_TIA_IDA, 0x3);
	_set_rx_dck(rtwdev, phy, path);
}

static void _dpk_information(Context *rtwdev, enum rtw89_phy_idx phy,
			     u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	u8 kidx = dpk->cur_idx[path];

	dpk->bp[path][kidx].band = chan->band_type;
	dpk->bp[path][kidx].ch = chan->channel;
	dpk->bp[path][kidx].bw = chan->band_width;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d[%d] (PHY%d): TSSI %s/ DBCC %s/ %s/ CH%d/ %s\n",
		    path, dpk->cur_idx[path], phy,
		    rtwdev->is_tssi_mode[path] ? "on" : "off",
		    rtwdev->dbcc_en ? "on" : "off",
		    dpk->bp[path][kidx].band == 0 ? "2G" :
		    dpk->bp[path][kidx].band == 1 ? "5G" : "6G",
		    dpk->bp[path][kidx].ch,
		    dpk->bp[path][kidx].bw == 0 ? "20M" :
		    dpk->bp[path][kidx].bw == 1 ? "40M" : "80M");
}

static void _dpk_bb_afe_setting(Context *rtwdev,
				enum rtw89_phy_idx phy,
				u8 path, u8 kpath)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dpk_afe_defs_tbl);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1, B_P0_CFCH_EX, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BW_SEL_V1, B_PATH1_BW_SEL_EX, 0x1);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Set BB/AFE for PHY%d (kpath=%d)\n", phy, kpath);
}

static void _dpk_bb_afe_restore(Context *rtwdev,
				enum rtw89_phy_idx phy,
				u8 path, u8 kpath)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dpk_afe_restore_defs_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Restore BB/AFE for PHY%d (kpath=%d)\n", phy, kpath);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1, B_P0_CFCH_EX, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BW_SEL_V1,  B_PATH1_BW_SEL_EX, 0x0);
	}
}

static void _dpk_tssi_pause(Context *rtwdev,
			    u8 path, bool is_pause)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK + (path << 13),
			       B_P0_TSSI_TRK_EN, is_pause);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d TSSI %s\n", path,
		    is_pause ? "pause" : "resume");
}

static void _dpk_kip_restore(Context *rtwdev,
			     u8 path)
{
	rtw89_rfk_parser(rtwdev, &rtw8852b_dpk_kip_defs_tbl);

	if (rtwdev->hal.cv > CHIP_CAV)
		rtw89_phy_write32_mask(rtwdev, R_DPD_COM + (path << 8), B_DPD_COM_OF, 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d restore KIP\n", path);
}

static void _dpk_lbk_rxiqk(Context *rtwdev, enum rtw89_phy_idx phy,
			   u8 path)
{
	u8 cur_rxbb;
	u32 tmp;

	cur_rxbb = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASKRXBB);

	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, B_MDPK_RX_DCK_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8), B_IQK_RES_RXCFIR, 0x0);

	tmp = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK, tmp);
	rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASKMODE, 0xd);
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_PLLEN, 0x1);

	if (cur_rxbb >= 0x11)
		rtw89_write_rf(rtwdev, path, RR_TXIQK, RR_TXIQK_ATT1, 0x13);
	else if (cur_rxbb <= 0xa)
		rtw89_write_rf(rtwdev, path, RR_TXIQK, RR_TXIQK_ATT1, 0x00);
	else
		rtw89_write_rf(rtwdev, path, RR_TXIQK, RR_TXIQK_ATT1, 0x05);

	rtw89_write_rf(rtwdev, path, RR_XGLNA2, RR_XGLNA2_SW, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80014);
	delay(rtwdev,70);

	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x025);

	_dpk_one_shot(rtwdev, phy, path, LBK_RXIQK);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d LBK RXIQC = 0x%x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD));

	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_PLLEN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, B_MDPK_RX_DCK_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_KPATH_CFG, B_KPATH_CFG_ED, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_DI, 0x1);
	rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASKMODE, 0x5);
}

static void _dpk_get_thermal(Context *rtwdev, u8 kidx, u8 path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	rtw89_write_rf(rtwdev, path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, path, RR_TM, RR_TM_TRI, 0x1);

	delay(rtwdev,200);

	dpk->bp[path][kidx].ther_dpk = rtw89_read_rf(rtwdev, path, RR_TM, RR_TM_VAL);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] thermal@DPK = 0x%x\n",
		    dpk->bp[path][kidx].ther_dpk);
}

static void _dpk_rf_setting(Context *rtwdev, u8 gain,
			    u8 path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].band == RTW89_BAND_2G) {
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK, 0x50220);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_FATT, 0xf2);
		rtw89_write_rf(rtwdev, path, RR_LUTDBG, RR_LUTDBG_TIA, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TIA, RR_TIA_N6, 0x1);
	} else {
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK, 0x50220);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RAA2_SWATT, 0x5);
		rtw89_write_rf(rtwdev, path, RR_LUTDBG, RR_LUTDBG_TIA, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TIA, RR_TIA_N6, 0x1);
		rtw89_write_rf(rtwdev, path, RR_RXA_LNA, RFREG_MASK, 0x920FC);
		rtw89_write_rf(rtwdev, path, RR_XALNA2, RFREG_MASK, 0x002C0);
		rtw89_write_rf(rtwdev, path, RR_IQGEN, RFREG_MASK, 0x38800);
	}

	rtw89_write_rf(rtwdev, path, RR_RCKD, RR_RCKD_BW, 0x1);
	rtw89_write_rf(rtwdev, path, RR_BTC, RR_BTC_TXBB, dpk->bp[path][kidx].bw + 1);
	rtw89_write_rf(rtwdev, path, RR_BTC, RR_BTC_RXBB, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ARF 0x0/0x11/0x1a = 0x%x/ 0x%x/ 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_TXIG, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_BTC, RFREG_MASK));
}

static void _dpk_bypass_rxcfir(Context *rtwdev,
			       u8 path, bool is_bypass)
{
	if (is_bypass) {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       B_RXIQC_BYPASS2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       B_RXIQC_BYPASS, 0x1);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Bypass RXIQC (0x8%d3c = 0x%x)\n", 1 + path,
			    rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8),
						  MASKDWORD));
	} else {
		rtw89_phy_write32_clr(rtwdev, R_RXIQC + (path << 8), B_RXIQC_BYPASS2);
		rtw89_phy_write32_clr(rtwdev, R_RXIQC + (path << 8), B_RXIQC_BYPASS);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] restore 0x8%d3c = 0x%x\n", 1 + path,
			    rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8),
						  MASKDWORD));
	}
}

static void _dpk_tpg_sel(Context *rtwdev, u8 path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80)
		rtw89_phy_write32_clr(rtwdev, R_TPG_MOD, B_TPG_MOD_F);
	else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40)
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x2);
	else
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] TPG_Select for %s\n",
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80 ? "80M" :
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40 ? "40M" : "20M");
}

static void _dpk_table_select(Context *rtwdev,
			      u8 path, u8 kidx, u8 gain)
{
	u8 val;

	val = 0x80 + kidx * 0x20 + gain * 0x10;
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0 + (path << 8), MASKBYTE3, val);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] table select for Kidx[%d], Gain[%d] (0x%x)\n", kidx,
		    gain, val);
}

static bool _dpk_sync_check(Context *rtwdev, u8 path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u16 dc_i, dc_q;
	u8 corr_val, corr_idx;

	rtw89_phy_write32_clr(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL);

	corr_idx = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_CORI);
	corr_val = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_CORV);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d Corr_idx / Corr_val = %d / %d\n",
		    path, corr_idx, corr_val);

	dpk->corr_idx[path][kidx] = corr_idx;
	dpk->corr_val[path][kidx] = corr_val;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x9);

	dc_i = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);
	dc_q = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCQ);

	dc_i = absValue(sign_extend32(dc_i, 11));
	dc_q = absValue(sign_extend32(dc_q, 11));

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d DC I/Q, = %d / %d\n",
		    path, dc_i, dc_q);

	dpk->dc_i[path][kidx] = dc_i;
	dpk->dc_q[path][kidx] = dc_q;

	if (dc_i > DPK_SYNC_TH_DC_I || dc_q > DPK_SYNC_TH_DC_Q ||
	    corr_val < DPK_SYNC_TH_CORR)
		return true;
	else
		return false;
}

static bool _dpk_sync(Context *rtwdev, enum rtw89_phy_idx phy,
		      u8 path, u8 kidx)
{
	_dpk_one_shot(rtwdev, phy, path, SYNC);

	return _dpk_sync_check(rtwdev, path, kidx);
}

static u16 _dpk_dgain_read(Context *rtwdev)
{
	u16 dgain;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x0);

	dgain = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] DGain = 0x%x\n", dgain);

	return dgain;
}

static s8 _dpk_dgain_mapping(Context *rtwdev, u16 dgain)
{
	static const u16 bnd[15] = {
		0xbf1, 0xaa5, 0x97d, 0x875, 0x789, 0x6b7, 0x5fc, 0x556,
		0x4c1, 0x43d, 0x3c7, 0x35e, 0x2ac, 0x262, 0x220
	};
	s8 offset;

	if (dgain >= bnd[0])
		offset = 0x6;
	else if (bnd[0] > dgain && dgain >= bnd[1])
		offset = 0x6;
	else if (bnd[1] > dgain && dgain >= bnd[2])
		offset = 0x5;
	else if (bnd[2] > dgain && dgain >= bnd[3])
		offset = 0x4;
	else if (bnd[3] > dgain && dgain >= bnd[4])
		offset = 0x3;
	else if (bnd[4] > dgain && dgain >= bnd[5])
		offset = 0x2;
	else if (bnd[5] > dgain && dgain >= bnd[6])
		offset = 0x1;
	else if (bnd[6] > dgain && dgain >= bnd[7])
		offset = 0x0;
	else if (bnd[7] > dgain && dgain >= bnd[8])
		offset = 0xff;
	else if (bnd[8] > dgain && dgain >= bnd[9])
		offset = 0xfe;
	else if (bnd[9] > dgain && dgain >= bnd[10])
		offset = 0xfd;
	else if (bnd[10] > dgain && dgain >= bnd[11])
		offset = 0xfc;
	else if (bnd[11] > dgain && dgain >= bnd[12])
		offset = 0xfb;
	else if (bnd[12] > dgain && dgain >= bnd[13])
		offset = 0xfa;
	else if (bnd[13] > dgain && dgain >= bnd[14])
		offset = 0xf9;
	else if (bnd[14] > dgain)
		offset = 0xf8;
	else
		offset = 0x0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] DGain offset = %d\n", offset);

	return offset;
}

static u8 _dpk_gainloss_read(Context *rtwdev)
{
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x6);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG2, B_DPK_CFG2_ST, 0x1);

	return rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_GL);
}

static void _dpk_gainloss(Context *rtwdev, enum rtw89_phy_idx phy,
			  u8 path, u8 kidx)
{
	_dpk_table_select(rtwdev, path, kidx, 1);
	_dpk_one_shot(rtwdev, phy, path, GAIN_LOSS);
}

static void _dpk_kip_preset(Context *rtwdev, enum rtw89_phy_idx phy,
			    u8 path, u8 kidx)
{
	_dpk_tpg_sel(rtwdev, path, kidx);
	_dpk_one_shot(rtwdev, phy, path, KIP_PRESET);
}

static void _dpk_kip_pwr_clk_on(Context *rtwdev,
				u8 path)
{
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x807f030a);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8), MASKDWORD, 0xce000a08);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] KIP Power/CLK on\n");
}

static void _dpk_kip_set_txagc(Context *rtwdev, enum rtw89_phy_idx phy,
			       u8 path, u8 txagc)
{
	rtw89_write_rf(rtwdev, path, RR_TXAGC, RFREG_MASK, txagc);
	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
	_dpk_one_shot(rtwdev, phy, path, DPK_TXAGC);
	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] set TXAGC = 0x%x\n", txagc);
}

static void _dpk_kip_set_rxagc(Context *rtwdev, enum rtw89_phy_idx phy,
			       u8 path)
{
	u32 tmp;

	tmp = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
	rtw89_phy_write32_mask(rtwdev, R_KIP_MOD, B_KIP_MOD, tmp);
	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
	_dpk_one_shot(rtwdev, phy, path, DPK_RXAGC);
	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL_V1, 0x8);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] set RXBB = 0x%x (RF0x0[9:5] = 0x%x)\n",
		    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_RXBB_V1),
		    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASKRXBB));
}

static u8 _dpk_set_offset(Context *rtwdev, enum rtw89_phy_idx phy,
			  u8 path, s8 gain_offset)
{
	u8 txagc;

	txagc = rtw89_read_rf(rtwdev, path, RR_TXAGC, RFREG_MASK);

	if (txagc - gain_offset < s32(DPK_TXAGC_LOWER))
		txagc = DPK_TXAGC_LOWER;
	else if (txagc - gain_offset > s32(DPK_TXAGC_UPPER))
		txagc = DPK_TXAGC_UPPER;
	else
		txagc = txagc - gain_offset;

	_dpk_kip_set_txagc(rtwdev, phy, path, txagc);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] tmp_txagc (GL=%d) = 0x%x\n",
		    gain_offset, txagc);
	return txagc;
}

static bool _dpk_pas_read(Context *rtwdev, bool is_check)
{
	u32 val1_i = 0, val1_q = 0, val2_i = 0, val2_q = 0;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKBYTE2, 0x06);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG2, B_DPK_CFG2_ST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE2, 0x08);

	if (is_check) {
		rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, 0x00);
		val1_i = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKHWORD);
		val1_i = absValue(sign_extend32(val1_i, 11));
		val1_q = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKLWORD);
		val1_q = absValue(sign_extend32(val1_q, 11));

		rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, 0x1f);
		val2_i = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKHWORD);
		val2_i = absValue(sign_extend32(val2_i, 11));
		val2_q = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKLWORD);
		val2_q = absValue(sign_extend32(val2_q, 11));

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] PAS_delta = 0x%x\n",
			    safeDivide(val1_i * val1_i + val1_q * val1_q,
				    val2_i * val2_i + val2_q * val2_q));
	} else {
		for (i = 0; i < 32; i++) {
			rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, i);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[DPK] PAS_Read[%02d]= 0x%08x\n", i,
				    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD));
		}
	}

	if (val1_i * val1_i + val1_q * val1_q >=
	    (val2_i * val2_i + val2_q * val2_q) * 8 / 5)
		return true;

	return false;
}

static u8 _dpk_agc(Context *rtwdev, enum rtw89_phy_idx phy,
		   u8 path, u8 kidx, u8 init_txagc,
		   bool loss_only)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 step = DPK_AGC_STEP_SYNC_DGAIN;
	u8 tmp_txagc, tmp_rxbb = 0, tmp_gl_idx = 0;
	u8 goout = 0, agc_cnt = 0, limited_rxbb = 0;
	u16 dgain = 0;
	s8 offset;
	int limit = 200;

	tmp_txagc = init_txagc;

	do {
		switch (step) {
		case DPK_AGC_STEP_SYNC_DGAIN:
			if (_dpk_sync(rtwdev, phy, path, kidx)) {
				tmp_txagc = 0xff;
				goout = 1;
				break;
			}

			dgain = _dpk_dgain_read(rtwdev);

			if (loss_only == 1 || limited_rxbb == 1)
				step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			else
				step = DPK_AGC_STEP_GAIN_ADJ;
			break;

		case DPK_AGC_STEP_GAIN_ADJ:
			tmp_rxbb = rtw89_read_rf(rtwdev, path, RR_MOD,
						 RFREG_MASKRXBB);
			offset = _dpk_dgain_mapping(rtwdev, dgain);

			if (tmp_rxbb + offset > 0x1f) {
				tmp_rxbb = 0x1f;
				limited_rxbb = 1;
			} else if (tmp_rxbb + offset < 0) {
				tmp_rxbb = 0;
				limited_rxbb = 1;
			} else {
				tmp_rxbb = tmp_rxbb + offset;
			}

			rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASKRXBB,
				       tmp_rxbb);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[DPK] Adjust RXBB (%d) = 0x%x\n", offset, tmp_rxbb);
			if (offset || agc_cnt == 0) {
				if (chan->band_width < RTW89_CHANNEL_WIDTH_80)
					_dpk_bypass_rxcfir(rtwdev, path, true);
				else
					_dpk_lbk_rxiqk(rtwdev, phy, path);
			}
			if (dgain > 1922 || dgain < 342)
				step = DPK_AGC_STEP_SYNC_DGAIN;
			else
				step = DPK_AGC_STEP_GAIN_LOSS_IDX;

			agc_cnt++;
			break;

		case DPK_AGC_STEP_GAIN_LOSS_IDX:
			_dpk_gainloss(rtwdev, phy, path, kidx);
			tmp_gl_idx = _dpk_gainloss_read(rtwdev);

			if ((tmp_gl_idx == 0 && _dpk_pas_read(rtwdev, true)) ||
			    tmp_gl_idx >= 7)
				step = DPK_AGC_STEP_GL_GT_CRITERION;
			else if (tmp_gl_idx == 0)
				step = DPK_AGC_STEP_GL_LT_CRITERION;
			else
				step = DPK_AGC_STEP_SET_TX_GAIN;
			break;

		case DPK_AGC_STEP_GL_GT_CRITERION:
			if (tmp_txagc == 0x2e) {
				goout = 1;
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[DPK] Txagc@lower bound!!\n");
			} else {
				tmp_txagc = _dpk_set_offset(rtwdev, phy, path, 0x3);
			}
			step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			agc_cnt++;
			break;

		case DPK_AGC_STEP_GL_LT_CRITERION:
			if (tmp_txagc == 0x3f) {
				goout = 1;
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[DPK] Txagc@upper bound!!\n");
			} else {
				tmp_txagc = _dpk_set_offset(rtwdev, phy, path, 0xfe);
			}
			step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			agc_cnt++;
			break;
		case DPK_AGC_STEP_SET_TX_GAIN:
			tmp_txagc = _dpk_set_offset(rtwdev, phy, path, tmp_gl_idx);
			goout = 1;
			agc_cnt++;
			break;

		default:
			goout = 1;
			break;
		}
	} while (check(rtwdev) && !goout && agc_cnt < 6 && limit-- > 0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Txagc / RXBB for DPK = 0x%x / 0x%x\n", tmp_txagc,
		    tmp_rxbb);

	return check(rtwdev) && goout ? tmp_txagc : 0xff;
}

static void _dpk_set_mdpd_para(Context *rtwdev, u8 order)
{
	switch (order) {
	case 0:
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, order);
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_PN, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_MAN, 0x1);
		break;
	case 1:
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, order);
		rtw89_phy_write32_clr(rtwdev, R_LDL_NORM, B_LDL_NORM_PN);
		rtw89_phy_write32_clr(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_MAN);
		break;
	case 2:
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, order);
		rtw89_phy_write32_clr(rtwdev, R_LDL_NORM, B_LDL_NORM_PN);
		rtw89_phy_write32_clr(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_MAN);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Wrong MDPD order!!(0x%x)\n", order);
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Set MDPD order to 0x%x for IDL\n", order);
}

static void _dpk_idl_mpa(Context *rtwdev, enum rtw89_phy_idx phy,
			 u8 path, u8 kidx, u8 gain)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].bw < RTW89_CHANNEL_WIDTH_80 &&
	    dpk->bp[path][kidx].band == RTW89_BAND_5G)
		_dpk_set_mdpd_para(rtwdev, 0x2);
	else
		_dpk_set_mdpd_para(rtwdev, 0x0);

	_dpk_one_shot(rtwdev, phy, path, MDPK_IDL);
}

static void _dpk_fill_result(Context *rtwdev, enum rtw89_phy_idx phy,
			     u8 path, u8 kidx, u8 gain, u8 txagc)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	const u16 pwsf = 0x78;
	u8 gs = dpk->dpk_gs[phy];

	rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8),
			       B_COEF_SEL_MDPD, kidx);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Fill txagc/ pwsf/ gs = 0x%x/ 0x%x/ 0x%x\n", txagc,
		    pwsf, gs);

	dpk->bp[path][kidx].txagc_dpk = txagc;
	rtw89_phy_write32_mask(rtwdev, R_TXAGC_RFK + (path << 8),
			       0x3F << ((gain << 3) + (kidx << 4)), txagc);

	dpk->bp[path][kidx].pwsf = pwsf;
	rtw89_phy_write32_mask(rtwdev, R_DPD_BND + (path << 8) + (kidx << 2),
			       0x1FF << (gain << 4), pwsf);

	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x0);

	dpk->bp[path][kidx].gs = gs;
	if (dpk->dpk_gs[phy] == 0x7f)
		rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
				       MASKDWORD, 0x007f7f7f);
	else
		rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
				       MASKDWORD, 0x005b5b5b);

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_ORDER_V1, _dpk_order_convert(rtwdev));
	rtw89_phy_write32_mask(rtwdev, R_DPD_V1 + (path << 8), MASKDWORD, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_SEL, 0x0);
}

static bool _dpk_reload_check(Context *rtwdev, enum rtw89_phy_idx phy,
			      u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	bool is_reload = false;
	u8 idx, cur_band, cur_ch;

	cur_band = chan->band_type;
	cur_ch = chan->channel;

	for (idx = 0; idx < RTW89_DPK_BKUP_NUM; idx++) {
		if (cur_band != dpk->bp[path][idx].band ||
		    cur_ch != dpk->bp[path][idx].ch)
			continue;

		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8),
				       B_COEF_SEL_MDPD, idx);
		dpk->cur_idx[path] = idx;
		is_reload = true;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] reload S%d[%d] success\n", path, idx);
	}

	return is_reload;
}

static bool _dpk_main(Context *rtwdev, enum rtw89_phy_idx phy,
		      u8 path, u8 gain)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 txagc = 0x38, kidx = dpk->cur_idx[path];
	bool is_fail = false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ========= S%d[%d] DPK Start =========\n", path, kidx);

	_rfk_rf_direct_cntrl(rtwdev, path, false);
	_rfk_drf_direct_cntrl(rtwdev, path, false);

	_dpk_kip_pwr_clk_on(rtwdev, path);
	_dpk_kip_set_txagc(rtwdev, phy, path, txagc);
	_dpk_rf_setting(rtwdev, gain, path, kidx);
	_dpk_rx_dck(rtwdev, phy, path);

	_dpk_kip_preset(rtwdev, phy, path, kidx);
	_dpk_kip_set_rxagc(rtwdev, phy, path);
	_dpk_table_select(rtwdev, path, kidx, gain);

	txagc = _dpk_agc(rtwdev, phy, path, kidx, txagc, false);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Adjust txagc = 0x%x\n", txagc);

	if (txagc == 0xff) {
		is_fail = true;
	} else {
		_dpk_get_thermal(rtwdev, kidx, path);

		_dpk_idl_mpa(rtwdev, phy, path, kidx, gain);

		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);

		_dpk_fill_result(rtwdev, phy, path, kidx, gain, txagc);
	}

	if (!is_fail)
		dpk->bp[path][kidx].path_ok = true;
	else
		dpk->bp[path][kidx].path_ok = false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] DPK %s\n", path, kidx,
		    is_fail ? "Check" : "Success");

	return is_fail;
}

static void _dpk_cal_select(Context *rtwdev, bool force,
			    enum rtw89_phy_idx phy, u8 kpath)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	static const u32 kip_reg[] = {0x813c, 0x8124, 0x8120};
	u32 kip_bkup[RTW8852B_DPK_RF_PATH][RTW8852B_DPK_KIP_REG_NUM] = {};
	u32 backup_rf_val[RTW8852B_DPK_RF_PATH][BACKUP_RF_REGS_NR];
	u32 backup_bb_val[BACKUP_BB_REGS_NR];
	bool is_fail = true, reloaded[RTW8852B_DPK_RF_PATH] = {};
	u8 path;

	if (dpk->is_dpk_reload_en) {
		for (path = 0; path < RTW8852B_DPK_RF_PATH; path++) {
			reloaded[path] = _dpk_reload_check(rtwdev, phy, path);
			if (!reloaded[path] && dpk->bp[path][0].ch)
				dpk->cur_idx[path] = !dpk->cur_idx[path];
			else
				_dpk_onoff(rtwdev, path, false);
		}
	} else {
		for (path = 0; path < RTW8852B_DPK_RF_PATH; path++)
			dpk->cur_idx[path] = 0;
	}

	_rfk_backup_bb_reg(rtwdev, &backup_bb_val[0]);

	for (path = 0; path < RTW8852B_DPK_RF_PATH; path++) {
		_dpk_bkup_kip(rtwdev, kip_reg, kip_bkup, path);
		_rfk_backup_rf_reg(rtwdev, &backup_rf_val[path][0], path);
		_dpk_information(rtwdev, phy, path);
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, true);
	}

	_dpk_bb_afe_setting(rtwdev, phy, path, kpath);

	for (path = 0; path < RTW8852B_DPK_RF_PATH; path++) {
		is_fail = _dpk_main(rtwdev, phy, path, 1);
		_dpk_onoff(rtwdev, path, is_fail);
	}

	_dpk_bb_afe_restore(rtwdev, phy, path, kpath);
	_rfk_restore_bb_reg(rtwdev, &backup_bb_val[0]);

	for (path = 0; path < RTW8852B_DPK_RF_PATH; path++) {
		_dpk_kip_restore(rtwdev, path);
		_dpk_reload_kip(rtwdev, kip_reg, kip_bkup, path);
		_rfk_restore_rf_reg(rtwdev, &backup_rf_val[path][0], path);
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, false);
	}
}

static bool _dpk_bypass_check(Context *rtwdev, enum rtw89_phy_idx phy)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_fem_info *fem = &rtwdev->fem;

	if (fem->epa_2g && chan->band_type == RTW89_BAND_2G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Skip DPK due to 2G_ext_PA exist!!\n");
		return true;
	} else if (fem->epa_5g && chan->band_type == RTW89_BAND_5G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Skip DPK due to 5G_ext_PA exist!!\n");
		return true;
	} else if (fem->epa_6g && chan->band_type == RTW89_BAND_6G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Skip DPK due to 6G_ext_PA exist!!\n");
		return true;
	}

	return false;
}

static void _dpk_force_bypass(Context *rtwdev, enum rtw89_phy_idx phy)
{
	u8 path, kpath;

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < RTW8852B_DPK_RF_PATH; path++) {
		if (kpath & bit(path))
			_dpk_onoff(rtwdev, path, true);
	}
}

static void _dpk(Context *rtwdev, enum rtw89_phy_idx phy, bool force)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ****** DPK Start (Ver: 0x%x, Cv: %d, RF_para: %d) ******\n",
		    RTW8852B_DPK_VER, rtwdev->hal.cv,
		    RTW8852B_RF_REL_VERSION);

	if (_dpk_bypass_check(rtwdev, phy))
		_dpk_force_bypass(rtwdev, phy);
	else
		_dpk_cal_select(rtwdev, force, phy, RF_AB);
}

static void _dpk_track(Context *rtwdev)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	s8 txagc_bb, txagc_bb_tp, txagc_ofst;
	s16 ini_diff;
	s16 delta_ther[2] = {};
	u8 trk_idx, txagc_rf;
	u8 path, kidx;
	u16 pwsf[2];
	u8 cur_ther;
	u32 tmp;

	for (path = 0; path < RF_PATH_NUM_8852B; path++) {
		kidx = dpk->cur_idx[path];
		ini_diff = 0;

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] ================[S%d[%d] (CH %d)]================\n",
			    path, kidx, dpk->bp[path][kidx].ch);

		cur_ther = ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
		if (!cur_ther) continue;

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] thermal now = %d\n", cur_ther);

		if (dpk->bp[path][kidx].ch && cur_ther)
			delta_ther[path] = dpk->bp[path][kidx].ther_dpk - cur_ther;

		if (dpk->bp[path][kidx].band == RTW89_BAND_2G)
			delta_ther[path] = delta_ther[path] * 3 / 2;
		else
			delta_ther[path] = delta_ther[path] * 5 / 2;

		txagc_rf = rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13),
						 0x0000003f);

		if (rtwdev->is_tssi_mode[path]) {
			trk_idx = rtw89_read_rf(rtwdev, path, RR_TXA, RR_TXA_TRK);

			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] txagc_RF / track_idx = 0x%x / %d\n",
				    txagc_rf, trk_idx);

			txagc_bb =
				rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13),
						      MASKBYTE2);
			txagc_bb_tp =
				rtw89_phy_read32_mask(rtwdev, R_TXAGC_TP + (path << 13),
						      B_TXAGC_TP);

			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] txagc_bb_tp / txagc_bb = 0x%x / 0x%x\n",
				    txagc_bb_tp, txagc_bb);

			txagc_ofst =
				rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13),
						      MASKBYTE3);

			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] txagc_offset / delta_ther = %d / %d\n",
				    txagc_ofst, delta_ther[path]);
			tmp = rtw89_phy_read32_mask(rtwdev, R_DPD_COM + (path << 8),
						    B_DPD_COM_OF);
			if (tmp == 0x1) {
				txagc_ofst = 0;
				rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
					    "[DPK_TRK] HW txagc offset mode\n");
			}

			if (txagc_rf && cur_ther)
				ini_diff = txagc_ofst + (delta_ther[path]);

			tmp = rtw89_phy_read32_mask(rtwdev,
						    R_P0_TXDPD + (path << 13),
						    B_P0_TXDPD);
			if (tmp == 0x0) {
				pwsf[0] = dpk->bp[path][kidx].pwsf +
					  txagc_bb_tp - txagc_bb + ini_diff;
				pwsf[1] = dpk->bp[path][kidx].pwsf +
					  txagc_bb_tp - txagc_bb + ini_diff;
			} else {
				pwsf[0] = dpk->bp[path][kidx].pwsf + ini_diff;
				pwsf[1] = dpk->bp[path][kidx].pwsf + ini_diff;
			}

		} else {
			pwsf[0] = (dpk->bp[path][kidx].pwsf + delta_ther[path]) & 0x1ff;
			pwsf[1] = (dpk->bp[path][kidx].pwsf + delta_ther[path]) & 0x1ff;
		}

		tmp = rtw89_phy_read32_mask(rtwdev, R_DPK_TRK, B_DPK_TRK_DIS);
		if (!tmp && txagc_rf) {
			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] New pwsf[0] / pwsf[1] = 0x%x / 0x%x\n",
				    pwsf[0], pwsf[1]);

			rtw89_phy_write32_mask(rtwdev,
					       R_DPD_BND + (path << 8) + (kidx << 2),
					       B_DPD_BND_0, pwsf[0]);
			rtw89_phy_write32_mask(rtwdev,
					       R_DPD_BND + (path << 8) + (kidx << 2),
					       B_DPD_BND_1, pwsf[1]);
		}
	}
}

static void _set_dpd_backoff(Context *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 tx_scale, ofdm_bkof, path, kpath;

	kpath = _kpath(rtwdev, phy);

	ofdm_bkof = rtw89_phy_read32_mask(rtwdev, R_DPD_BF + (phy << 13), B_DPD_BF_OFDM);
	tx_scale = rtw89_phy_read32_mask(rtwdev, R_DPD_BF + (phy << 13), B_DPD_BF_SCA);

	if (ofdm_bkof + tx_scale >= 44) {
		/* move dpd backoff to bb, and set dpd backoff to 0 */
		dpk->dpk_gs[phy] = 0x7f;
		for (path = 0; path < RF_PATH_NUM_8852B; path++) {
			if (!(kpath & bit(path)))
				continue;

			rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8),
					       B_DPD_CFG, 0x7f7f7f);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[RFK] Set S%d DPD backoff to 0dB\n", path);
		}
	} else {
		dpk->dpk_gs[phy] = 0x5b;
	}
}

static void _tssi_rf_setting(Context *rtwdev, enum rtw89_phy_idx phy,
			     u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

	if (band == RTW89_BAND_2G)
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXG, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXA, 0x1);
}

static void _tssi_set_sys(Context *rtwdev, enum rtw89_phy_idx phy,
			  u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

	rtw89_rfk_parser(rtwdev, &rtw8852b_tssi_sys_defs_tbl);

	if (path == RF_PATH_A)
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_sys_a_defs_2g_tbl,
					 &rtw8852b_tssi_sys_a_defs_5g_tbl);
	else
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_sys_b_defs_2g_tbl,
					 &rtw8852b_tssi_sys_b_defs_5g_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb(Context *rtwdev,
				    enum rtw89_phy_idx phy,
				    u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_init_txpwr_defs_a_tbl,
				 &rtw8852b_tssi_init_txpwr_defs_b_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb_he_tb(Context *rtwdev,
					  enum rtw89_phy_idx phy,
					  u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_init_txpwr_he_tb_defs_a_tbl,
				 &rtw8852b_tssi_init_txpwr_he_tb_defs_b_tbl);
}

static void _tssi_set_dck(Context *rtwdev, enum rtw89_phy_idx phy,
			  u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_dck_defs_a_tbl,
				 &rtw8852b_tssi_dck_defs_b_tbl);
}

static void _tssi_set_tmeter_tbl(Context *rtwdev, enum rtw89_phy_idx phy,
				 u8 path)
{

	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u8 subband = chan->subband_type;
	const s8 *thm_up_a = NULL;
	const s8 *thm_down_a = NULL;
	const s8 *thm_up_b = NULL;
	const s8 *thm_down_b = NULL;
	u8 thermal = 0xff;
	s8 thm_ofst[64] = {0};
	u32 tmp = 0;
	u8 i, j;

	switch (subband) {
	default:
	case RTW89_CH_2G:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_2ga_p;
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_2ga_n;
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_2gb_p;
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_2gb_n;
		break;
	case RTW89_CH_5G_BAND_1:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_p[0];
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_n[0];
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_p[0];
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_n[0];
		break;
	case RTW89_CH_5G_BAND_3:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_p[1];
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_n[1];
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_p[1];
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_n[1];
		break;
	case RTW89_CH_5G_BAND_4:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_p[2];
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_n[2];
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_p[2];
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_n[2];
		break;
	}

	if (path == RF_PATH_A) {
		thermal = tssi_info->thermal[RF_PATH_A];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] ch=%d thermal_pathA=0x%x\n", ch, thermal);

		rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER_TRK, 0x1);

		if (thermal == 0xff) {
			rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER, 32);
			rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_VAL, 32);

			for (i = 0; i < 64; i += 4) {
				rtw89_phy_write32(rtwdev, R_P0_TSSI_BASE + i, 0x0);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    R_P0_TSSI_BASE + i, 0x0);
			}

		} else {
			rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER, thermal);
			rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_VAL,
					       thermal);

			i = 0;
			for (j = 0; j < 32; j++)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      -thm_down_a[i++] :
					      -thm_down_a[DELTA_SWINGIDX_SIZE - 1];

			i = 1;
			for (j = 63; j >= 32; j--)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      thm_up_a[i++] :
					      thm_up_a[DELTA_SWINGIDX_SIZE - 1];

			for (i = 0; i < 64; i += 4) {
				tmp = thermalWord(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_P0_TSSI_BASE + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x5c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, R_P0_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, R_P0_RFCTM_RDY, 0x0);

	} else {
		thermal = tssi_info->thermal[RF_PATH_B];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] ch=%d thermal_pathB=0x%x\n", ch, thermal);

		rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER_TRK, 0x1);

		if (thermal == 0xff) {
			rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER, 32);
			rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, B_P1_RFCTM_VAL, 32);

			for (i = 0; i < 64; i += 4) {
				rtw89_phy_write32(rtwdev, R_TSSI_THOF + i, 0x0);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x7c00 + i, 0x0);
			}

		} else {
			rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER, thermal);
			rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, B_P1_RFCTM_VAL,
					       thermal);

			i = 0;
			for (j = 0; j < 32; j++)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      -thm_down_b[i++] :
					      -thm_down_b[DELTA_SWINGIDX_SIZE - 1];

			i = 1;
			for (j = 63; j >= 32; j--)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      thm_up_b[i++] :
					      thm_up_b[DELTA_SWINGIDX_SIZE - 1];

			for (i = 0; i < 64; i += 4) {
				tmp = thermalWord(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_TSSI_THOF + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x7c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x0);
	}

}

static void _tssi_set_dac_gain_tbl(Context *rtwdev, enum rtw89_phy_idx phy,
				   u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_dac_gain_defs_a_tbl,
				 &rtw8852b_tssi_dac_gain_defs_b_tbl);
}

static void _tssi_slope_cal_org(Context *rtwdev, enum rtw89_phy_idx phy,
				u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

	if (path == RF_PATH_A)
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_slope_a_defs_2g_tbl,
					 &rtw8852b_tssi_slope_a_defs_5g_tbl);
	else
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_slope_b_defs_2g_tbl,
					 &rtw8852b_tssi_slope_b_defs_5g_tbl);
}

static void _tssi_alignment_default(Context *rtwdev, enum rtw89_phy_idx phy,
				    u8 path, bool all)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;
	const struct rtw89_rfk_tbl *tbl = NULL;
	u8 ch = chan->channel;

	if (path == RF_PATH_A) {
		if (band == RTW89_BAND_2G) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_2g_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_2g_part_defs_tbl;
		} else if (ch >= 36 && ch <= 64) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_5g1_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_5g1_part_defs_tbl;
		} else if (ch >= 100 && ch <= 144) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_5g2_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_5g2_part_defs_tbl;
		} else if (ch >= 149 && ch <= 177) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_5g3_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_5g3_part_defs_tbl;
		}
	} else {
		if (ch >= 1 && ch <= 14) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_2g_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_2g_part_defs_tbl;
		} else if (ch >= 36 && ch <= 64) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_5g1_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_5g1_part_defs_tbl;
		} else if (ch >= 100 && ch <= 144) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_5g2_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_5g2_part_defs_tbl;
		} else if (ch >= 149 && ch <= 177) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_5g3_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_5g3_part_defs_tbl;
		}
	}

	if (tbl)
		rtw89_rfk_parser(rtwdev, tbl);
}

static void _tssi_set_tssi_slope(Context *rtwdev, enum rtw89_phy_idx phy,
				 u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_slope_defs_a_tbl,
				 &rtw8852b_tssi_slope_defs_b_tbl);
}

static void _tssi_set_tssi_track(Context *rtwdev, enum rtw89_phy_idx phy,
				 u8 path)
{
	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSIC, B_P0_TSSIC_BYPASS, 0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSIC, B_P1_TSSIC_BYPASS, 0x0);
}

static void _tssi_set_txagc_offset_mv_avg(Context *rtwdev,
					  enum rtw89_phy_idx phy,
					  u8 path)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "======>%s   path=%d\n", __func__,
		    path);

	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_MIX, 0x010);
	else
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_RFCTM_DEL, 0x010);
}

static void _tssi_enable(Context *rtwdev, enum rtw89_phy_idx phy)
{
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		_tssi_set_tssi_track(rtwdev, phy, i);
		_tssi_set_txagc_offset_mv_avg(rtwdev, phy, i);

		if (i == RF_PATH_A) {
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG,
					       B_P0_TSSI_MV_CLR, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG,
					       B_P0_TSSI_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG,
					       B_P0_TSSI_EN, 0x1);
			rtw89_write_rf(rtwdev, i, RR_TXGA_V1,
				       RR_TXGA_V1_TRK_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_RFC, 0x3);

			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_OFT, 0xc0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_OFT_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_OFT_EN, 0x1);

			rtwdev->is_tssi_mode[RF_PATH_A] = true;
		} else {
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG,
					       B_P1_TSSI_MV_CLR, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG,
					       B_P1_TSSI_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG,
					       B_P1_TSSI_EN, 0x1);
			rtw89_write_rf(rtwdev, i, RR_TXGA_V1,
				       RR_TXGA_V1_TRK_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_RFC, 0x3);

			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_OFT, 0xc0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_OFT_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_OFT_EN, 0x1);

			rtwdev->is_tssi_mode[RF_PATH_B] = true;
		}
	}
}

static void _tssi_disable(Context *rtwdev, enum rtw89_phy_idx phy)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_RFC, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_CLR, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG, B_P1_TSSI_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_RFC, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_TSSI_MV_CLR, 0x1);

	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;
}

static u32 _tssi_get_cck_group(Context *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 2:
		return 0;
	case 3 ... 5:
		return 1;
	case 6 ... 8:
		return 2;
	case 9 ... 11:
		return 3;
	case 12 ... 13:
		return 4;
	case 14:
		return 5;
	}

	return 0;
}

static u32 _tssi_get_ofdm_group(Context *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 2:
		return 0;
	case 3 ... 5:
		return 1;
	case 6 ... 8:
		return 2;
	case 9 ... 11:
		return 3;
	case 12 ... 14:
		return 4;
	case 36 ... 40:
		return 5;
	case 41 ... 43:
		return tssiExtraGroup(5);
	case 44 ... 48:
		return 6;
	case 49 ... 51:
		return tssiExtraGroup(6);
	case 52 ... 56:
		return 7;
	case 57 ... 59:
		return tssiExtraGroup(7);
	case 60 ... 64:
		return 8;
	case 100 ... 104:
		return 9;
	case 105 ... 107:
		return tssiExtraGroup(9);
	case 108 ... 112:
		return 10;
	case 113 ... 115:
		return tssiExtraGroup(10);
	case 116 ... 120:
		return 11;
	case 121 ... 123:
		return tssiExtraGroup(11);
	case 124 ... 128:
		return 12;
	case 129 ... 131:
		return tssiExtraGroup(12);
	case 132 ... 136:
		return 13;
	case 137 ... 139:
		return tssiExtraGroup(13);
	case 140 ... 144:
		return 14;
	case 149 ... 153:
		return 15;
	case 154 ... 156:
		return tssiExtraGroup(15);
	case 157 ... 161:
		return 16;
	case 162 ... 164:
		return tssiExtraGroup(16);
	case 165 ... 169:
		return 17;
	case 170 ... 172:
		return tssiExtraGroup(17);
	case 173 ... 177:
		return 18;
	}

	return 0;
}

static u32 _tssi_get_trim_group(Context *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 8:
		return 0;
	case 9 ... 14:
		return 1;
	case 36 ... 48:
		return 2;
	case 52 ... 64:
		return 3;
	case 100 ... 112:
		return 4;
	case 116 ... 128:
		return 5;
	case 132 ... 144:
		return 6;
	case 149 ... 177:
		return 7;
	}

	return 0;
}

static s8 _tssi_get_ofdm_de(Context *rtwdev, enum rtw89_phy_idx phy,
			    u8 path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u32 gidx, gidx_1st, gidx_2nd;
	s8 de_1st;
	s8 de_2nd;
	s8 val;

	gidx = _tssi_get_ofdm_group(rtwdev, ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs group_idx=0x%x\n", path, gidx);

	if (isTssiExtraGroup(gidx)) {
		gidx_1st = tssiExtraIndex1(gidx);
		gidx_2nd = tssiExtraIndex2(gidx);
		de_1st = tssi_info->tssi_mcs[path][gidx_1st];
		de_2nd = tssi_info->tssi_mcs[path][gidx_2nd];
		val = (de_1st + de_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d 1st=%d 2nd=%d\n",
			    path, val, de_1st, de_2nd);
	} else {
		val = tssi_info->tssi_mcs[path][gidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d\n", path, val);
	}

	return val;
}

static s8 _tssi_get_ofdm_trim_de(Context *rtwdev, enum rtw89_phy_idx phy,
				 u8 path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u32 tgidx, tgidx_1st, tgidx_2nd;
	s8 tde_1st;
	s8 tde_2nd;
	s8 val;

	tgidx = _tssi_get_trim_group(rtwdev, ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs trim_group_idx=0x%x\n",
		    path, tgidx);

	if (isTssiExtraGroup(tgidx)) {
		tgidx_1st = tssiExtraIndex1(tgidx);
		tgidx_2nd = tssiExtraIndex2(tgidx);
		tde_1st = tssi_info->tssi_trim[path][tgidx_1st];
		tde_2nd = tssi_info->tssi_trim[path][tgidx_2nd];
		val = (tde_1st + tde_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d 1st=%d 2nd=%d\n",
			    path, val, tde_1st, tde_2nd);
	} else {
		val = tssi_info->tssi_trim[path][tgidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d\n",
			    path, val);
	}

	return val;
}

static void _tssi_set_efuse_to_de(Context *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u8 gidx;
	s8 ofdm_de;
	s8 trim_de;
	s32 val;
	u32 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRIM]: phy=%d ch=%d\n",
		    phy, ch);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8852B; i++) {
		gidx = _tssi_get_cck_group(rtwdev, ch);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i);
		val = tssi_info->tssi_cck[i][gidx] + trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d cck[%d]=0x%x trim=0x%x\n",
			    i, gidx, tssi_info->tssi_cck[i][gidx], trim_de);

		rtw89_phy_write32_mask(rtwdev, _tssi_de_cck_long[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_cck_short[i], _TSSI_DE_MASK, val);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] Set TSSI CCK DE 0x%x[21:12]=0x%x\n",
			    _tssi_de_cck_long[i],
			    rtw89_phy_read32_mask(rtwdev, _tssi_de_cck_long[i],
						  _TSSI_DE_MASK));

		ofdm_de = _tssi_get_ofdm_de(rtwdev, phy, i);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i);
		val = ofdm_de + trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs=0x%x trim=0x%x\n",
			    i, ofdm_de, trim_de);

		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_20m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_40m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_80m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_80m_80m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_5m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_10m[i], _TSSI_DE_MASK, val);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] Set TSSI MCS DE 0x%x[21:12]=0x%x\n",
			    _tssi_de_mcs_20m[i],
			    rtw89_phy_read32_mask(rtwdev, _tssi_de_mcs_20m[i],
						  _TSSI_DE_MASK));
	}
}

static void _tssi_alimentk_dump_result(Context *rtwdev, u8 path)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K]\n0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n"
		    "0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n",
		    R_TSSI_PA_K1 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K1 + (path << 13), MASKDWORD),
		    R_TSSI_PA_K2 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K2 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM1 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM3 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD),
		    R_TSSI_PA_K5 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K5 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM2 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM4 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD),
		    R_TSSI_PA_K8 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K8 + (path << 13), MASKDWORD));
}

static void _tssi_hw_tx(Context *rtwdev, enum rtw89_phy_idx phy,
			u8 path, u16 cnt, u16 period, s16 pwr_dbm,
			u8 enable)
{
	enum rtw89_rf_path_bit rx_path;

	if (path == RF_PATH_A)
		rx_path = RF_A;
	else if (path == RF_PATH_B)
		rx_path = RF_B;
	else if (path == RF_PATH_AB)
		rx_path = RF_AB;
	else
		rx_path = RF_ABCD; /* don't change path, but still set others */

	if (enable) {
		if (!check(rtwdev) || !rtwdev->io.armCalibrationTx()) { fail(rtwdev,Error::io); return; }
		rtw8852b_bb_set_plcp_tx(rtwdev);
		rtw8852b_bb_cfg_tx_path(rtwdev, path);
		rtw8852b_bb_ctrl_rx_path(rtwdev, rx_path);
		rtw8852b_bb_set_power(rtwdev, pwr_dbm, phy);
	}

	if (enable) rtw8852b_bb_set_pmac_pkt_tx(rtwdev, enable, cnt, period, 20, phy);
	else {
		if (!rtwdev->io.stopCalibrationTx()) fail(rtwdev,Error::io);
		if (check(rtwdev)) rtw8852b_bb_set_pmac_pkt_tx(rtwdev, false, cnt, period, 20, phy);
	}
}

static void _tssi_backup_bb_registers(Context *rtwdev,
				      enum rtw89_phy_idx phy, const u32 reg[],
				      u32 reg_backup[], u32 reg_num)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		reg_backup[i] = rtw89_phy_read32_mask(rtwdev, reg[i], MASKDWORD);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI] Backup BB 0x%x = 0x%x\n", reg[i],
			    reg_backup[i]);
	}
}

static void _tssi_reload_bb_registers(Context *rtwdev,
				      enum rtw89_phy_idx phy, const u32 reg[],
				      u32 reg_backup[], u32 reg_num)

{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		rtw89_phy_write32_mask(rtwdev, reg[i], MASKDWORD, reg_backup[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI] Reload BB 0x%x = 0x%x\n", reg[i],
			    reg_backup[i]);
	}
}

static u8 _tssi_ch_to_idx(Context *rtwdev, u8 channel)
{
	u8 channel_index;

	if (channel >= 1 && channel <= 14)
		channel_index = channel - 1;
	else if (channel >= 36 && channel <= 64)
		channel_index = (channel - 36) / 2 + 14;
	else if (channel >= 100 && channel <= 144)
		channel_index = ((channel - 100) / 2) + 15 + 14;
	else if (channel >= 149 && channel <= 177)
		channel_index = ((channel - 149) / 2) + 38 + 14;
	else
		channel_index = 0;

	return channel_index;
}

static bool _tssi_get_cw_report(Context *rtwdev, enum rtw89_phy_idx phy,
				u8 path, const s16 *power,
				u32 *tssi_cw_rpt)
{
	u32 tx_counter, tx_counter_tmp;
	const int retry = 100;
	u32 tmp;
	unsigned j; int k;

	for (j = 0; j < RTW8852B_TSSI_PATH_NR; j++) {
		rtw89_phy_write32_mask(rtwdev, _tssi_trigger[path], B_P0_TSSI_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, _tssi_trigger[path], B_P0_TSSI_EN, 0x1);

		tx_counter = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);

		tmp = rtw89_phy_read32_mask(rtwdev, _tssi_trigger[path], MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] 0x%x = 0x%08x   path=%d\n",
			    _tssi_trigger[path], tmp, path);

		if (j == 0)
			_tssi_hw_tx(rtwdev, phy, path, 100, 5000, power[j], true);
		else
			_tssi_hw_tx(rtwdev, phy, RF_PATH_ABCD, 100, 5000, power[j], true);

		tx_counter_tmp = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);
		tx_counter_tmp -= tx_counter;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] First HWTXcounter=%d path=%d\n",
			    tx_counter_tmp, path);

		for (k = 0; k < retry; k++) {
			tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_rpt_addr[path],
						    B_TSSI_CWRPT_RDY);
			if (tmp)
				break;

			delay(rtwdev,30);

			tx_counter_tmp =
				rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);
			tx_counter_tmp -= tx_counter;

			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[TSSI PA K] Flow k = %d HWTXcounter=%d path=%d\n",
				    k, tx_counter_tmp, path);
		}

		if (k >= retry) {
			fail(rtwdev,Error::timeout);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[TSSI PA K] TSSI finish bit k > %d mp:100ms normal:30us path=%d\n",
				    k, path);

			_tssi_hw_tx(rtwdev, phy, path, 100, 5000, power[j], false);
			return false;
		}

		tssi_cw_rpt[j] =
			rtw89_phy_read32_mask(rtwdev, _tssi_cw_rpt_addr[path], B_TSSI_CWRPT);

		_tssi_hw_tx(rtwdev, phy, path, 100, 5000, power[j], false);

		tx_counter_tmp = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);
		tx_counter_tmp -= tx_counter;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] Final HWTXcounter=%d path=%d\n",
			    tx_counter_tmp, path);
	}

	return true;
}

static void _tssi_alimentk(Context *rtwdev, enum rtw89_phy_idx phy,
			   u8 path)
{
	static const u32 bb_reg[8] = {0x5820, 0x7820, 0x4978, 0x58e4,
				      0x78e4, 0x49c0, 0x0d18, 0x0d80};
	static const s16 power_2g[4] = {48, 20, 4, 4};
	static const s16 power_5g[4] = {48, 20, 4, 4};
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	s32 tssi_alim_offset_1, tssi_alim_offset_2, tssi_alim_offset_3;
	u32 tssi_cw_rpt[RTW8852B_TSSI_PATH_NR] = {0};
	u8 channel = chan->channel;
	u8 ch_idx = _tssi_ch_to_idx(rtwdev, channel);
	struct rtw8852b_bb_tssi_bak tssi_bak;
	s32 aliment_diff, tssi_cw_default;
	u32 start_time, finish_time;
	u32 bb_reg_backup[8] = {0};
	const s16 *power;
	u8 band;
	bool ok;
	u32 tmp;
	u8 j;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======> %s   channel=%d   path=%d\n", __func__, channel,
		    path);

	if (tssi_info->check_backup_aligmk[path][ch_idx]) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][0]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][1]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][2]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][3]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "======> %s   Reload TSSI Alignment !!!\n", __func__);
		_tssi_alimentk_dump_result(rtwdev, path);
		return;
	}

	start_time = (rtwdev->io.nowUs()*1000);

	if (chan->band_type == RTW89_BAND_2G)
		power = power_2g;
	else
		power = power_5g;

	if (channel >= 1 && channel <= 14)
		band = TSSI_ALIMK_2G;
	else if (channel >= 36 && channel <= 64)
		band = TSSI_ALIMK_5GL;
	else if (channel >= 100 && channel <= 144)
		band = TSSI_ALIMK_5GM;
	else if (channel >= 149 && channel <= 177)
		band = TSSI_ALIMK_5GH;
	else
		band = TSSI_ALIMK_2G;

	rtw8852b_bb_backup_tssi(rtwdev, phy, &tssi_bak);
	_tssi_backup_bb_registers(rtwdev, phy, bb_reg, bb_reg_backup, arraySize(bb_reg_backup));

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_AVG, 0x8);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG, B_P1_TSSI_AVG, 0x8);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_AVG, 0x2);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_TSSI_MV_AVG, 0x2);

	ok = _tssi_get_cw_report(rtwdev, phy, path, power, tssi_cw_rpt);
	if (!ok)
		goto out;

	for (j = 0; j < RTW8852B_TSSI_PATH_NR; j++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] power[%d]=%d  tssi_cw_rpt[%d]=%d\n", j,
			    power[j], j, tssi_cw_rpt[j]);
	}

	tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_default_addr[path][1],
				    _tssi_cw_default_mask[1]);
	tssi_cw_default = sign_extend32(tmp, 8);
	tssi_alim_offset_1 = tssi_cw_rpt[0] - ((power[0] - power[1]) * 2) -
			     tssi_cw_rpt[1] + tssi_cw_default;
	aliment_diff = tssi_alim_offset_1 - tssi_cw_default;

	tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_default_addr[path][2],
				    _tssi_cw_default_mask[2]);
	tssi_cw_default = sign_extend32(tmp, 8);
	tssi_alim_offset_2 = tssi_cw_default + aliment_diff;

	tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_default_addr[path][3],
				    _tssi_cw_default_mask[3]);
	tssi_cw_default = sign_extend32(tmp, 8);
	tssi_alim_offset_3 = tssi_cw_default + aliment_diff;

	if (path == RF_PATH_A) {
		tmp = fieldPrep(B_P1_TSSI_ALIM11, tssi_alim_offset_1) |
		      fieldPrep(B_P1_TSSI_ALIM12, tssi_alim_offset_2) |
		      fieldPrep(B_P1_TSSI_ALIM13, tssi_alim_offset_3);

		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM1, tmp);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM2, B_P0_TSSI_ALIM2, tmp);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] tssi_alim_offset = 0x%x   0x%x   0x%x   0x%x\n",
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3, B_P0_TSSI_ALIM31),
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM11),
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM12),
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM13));
	} else {
		tmp = fieldPrep(B_P1_TSSI_ALIM11, tssi_alim_offset_1) |
		      fieldPrep(B_P1_TSSI_ALIM12, tssi_alim_offset_2) |
		      fieldPrep(B_P1_TSSI_ALIM13, tssi_alim_offset_3);

		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM1, tmp);
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_ALIM2, B_P1_TSSI_ALIM2, tmp);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] tssi_alim_offset = 0x%x   0x%x   0x%x   0x%x\n",
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM3, B_P1_TSSI_ALIM31),
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM11),
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM12),
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM13));
	}

	tssi_info->alignment_done[path][band] = true;
	tssi_info->alignment_value[path][band][0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD);
	tssi_info->alignment_value[path][band][1] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD);
	tssi_info->alignment_value[path][band][2] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD);
	tssi_info->alignment_value[path][band][3] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD);

	tssi_info->check_backup_aligmk[path][ch_idx] = true;
	tssi_info->alignment_backup_by_ch[path][ch_idx][0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD);
	tssi_info->alignment_backup_by_ch[path][ch_idx][1] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD);
	tssi_info->alignment_backup_by_ch[path][ch_idx][2] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD);
	tssi_info->alignment_backup_by_ch[path][ch_idx][3] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][0], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM1 + (path << 13),
		    tssi_info->alignment_value[path][band][0]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][1], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM3 + (path << 13),
		    tssi_info->alignment_value[path][band][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][2], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM2 + (path << 13),
		    tssi_info->alignment_value[path][band][2]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][3], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM4 + (path << 13),
		    tssi_info->alignment_value[path][band][3]);

out:
	_tssi_reload_bb_registers(rtwdev, phy, bb_reg, bb_reg_backup, arraySize(bb_reg_backup));
	rtw8852b_bb_restore_tssi(rtwdev, phy, &tssi_bak);
	rtw8852b_bb_tx_mode_switch(rtwdev, phy, 0);

	finish_time = (rtwdev->io.nowUs()*1000);
	tssi_info->tssi_alimk_time += finish_time - start_time;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] %s processing time = %d ms\n", __func__,
		    tssi_info->tssi_alimk_time);
}

static void rtw8852b_set_gain_offset(Context *rtwdev,
				     enum rtw89_subband subband,
				     enum rtw89_phy_idx phy_idx)
{
	static const u32 gain_err_addr[2] = {R_P0_AGC_RSVD, R_P1_AGC_RSVD};
	static const u32 rssi_ofst_addr[2] = {R_PATH0_G_TIA1_LNA6_OP1DB_V1,
					      R_PATH1_G_TIA1_LNA6_OP1DB_V1};
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_phy_efuse_gain *efuse_gain = &rtwdev->efuse_gain;
	enum rtw89_gain_offset gain_ofdm_band;
	s32 offset_a, offset_b;
	s32 offset_ofdm, offset_cck;
	s32 tmp;
	u8 path;

	if (!efuse_gain->comp_valid)
		goto next;

	for (path = RF_PATH_A; path < BB_PATH_NUM_8852B; path++) {
		tmp = efuse_gain->comp[path][subband];
		tmp = clampS32( tmp * (1 << 2), (-128), 127);
		rtw89_phy_write32_mask(rtwdev, gain_err_addr[path], MASKBYTE0, tmp);
	}

next:
	if (!efuse_gain->offset_valid)
		return;

	gain_ofdm_band = rtw89_subband_to_gain_offset_band_of_ofdm(subband);

	offset_a = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
	offset_b = -efuse_gain->offset[RF_PATH_B][gain_ofdm_band];

	tmp = -((offset_a * (1 << 2)) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clampS32( tmp, (-128), 127);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_A], B_PATH0_R_G_OFST_MASK, tmp);

	tmp = -((offset_b * (1 << 2)) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clampS32( tmp, (-128), 127);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_B], B_PATH0_R_G_OFST_MASK, tmp);

	if (hal->antenna_rx == RF_B) {
		offset_ofdm = -efuse_gain->offset[RF_PATH_B][gain_ofdm_band];
		offset_cck = -efuse_gain->offset[RF_PATH_B][0];
	} else {
		offset_ofdm = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
		offset_cck = -efuse_gain->offset[RF_PATH_A][0];
	}

	tmp = (offset_ofdm * (1 << 4)) + efuse_gain->offset_base[RTW89_PHY_0];
	tmp = clampS32( tmp, (-128), 127);
	rtw89_phy_write32_idx(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	tmp = (offset_ofdm * (1 << 4)) + efuse_gain->rssi_base[RTW89_PHY_0];
	tmp = clampS32( tmp, (-128), 127);
	rtw89_phy_write32_idx(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	if (subband == RTW89_CH_2G) {
		tmp = (offset_cck * (1 << 3)) + (efuse_gain->offset_base[RTW89_PHY_0] >> 1);
		tmp = clampS32( tmp, (-128) >> 1, 127 >> 1);
		rtw89_phy_write32_mask(rtwdev, R_RX_RPL_OFST,
				       B_RX_RPL_OFST_CCK_MASK, tmp);
	}
}

static void rtw8852b_bb_set_plcp_tx(Context *rtwdev)
{
	const struct rtw89_reg3_def *def = rtw8852b_pmac_ht20_mcs7_tbl;
	u8 i;

	for (i = 0; i < arraySize(rtw8852b_pmac_ht20_mcs7_tbl); i++, def++)
		rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
}

static void rtw8852b_stop_pmac_tx(Context *rtwdev,
				  struct rtw8852b_bb_pmac_info *tx_info,
				  enum rtw89_phy_idx idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC Stop Tx");
	if (tx_info->mode == CONT_TX)
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_CTX_EN, 0, idx);
	else if (tx_info->mode == PKTS_TX)
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_PTX_EN, 0, idx);
}

static void rtw8852b_start_pmac_tx(Context *rtwdev,
				   struct rtw8852b_bb_pmac_info *tx_info,
				   enum rtw89_phy_idx idx)
{
	enum rtw8852b_pmac_mode mode = static_cast<rtw8852b_pmac_mode>(tx_info->mode);
	u32 pkt_cnt = tx_info->tx_cnt;
	u16 period = tx_info->period;

	if (mode == CONT_TX && !tx_info->is_cck) {
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_CTX_EN, 1, idx);
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CTx Start");
	} else if (mode == PKTS_TX) {
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_PTX_EN, 1, idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD,
				      B_PMAC_TX_PRD_MSK, period, idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CNT, B_PMAC_TX_CNT_MSK,
				      pkt_cnt, idx);
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC PTx Start");
	}

	rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CTRL, B_PMAC_TXEN_DIS, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CTRL, B_PMAC_TXEN_DIS, 0, idx);
}

static void rtw8852b_bb_set_pmac_tx(Context *rtwdev,
			     struct rtw8852b_bb_pmac_info *tx_info,
			     enum rtw89_phy_idx idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);

	if (!tx_info->en_pmac_tx) {
		rtw8852b_stop_pmac_tx(rtwdev, tx_info, idx);
		rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0, idx);
		if (chan->band_type == RTW89_BAND_2G)
			rtw89_phy_write32_clr(rtwdev, R_RXCCA, B_RXCCA_DIS);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC Tx Enable");

	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_TXEN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_RXEN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RX_CFG1, B_PMAC_OPT1_MSK, 0x3f, idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 1, idx);
	rtw89_phy_write32_set(rtwdev, R_RXCCA, B_RXCCA_DIS);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, idx);

	rtw8852b_start_pmac_tx(rtwdev, tx_info, idx);
}

static void rtw8852b_bb_set_pmac_pkt_tx(Context *rtwdev, u8 enable,
				 u16 tx_cnt, u16 period, u16 tx_time,
				 enum rtw89_phy_idx idx)
{
	struct rtw8852b_bb_pmac_info tx_info = {};

	tx_info.en_pmac_tx = enable;
	tx_info.is_cck = 0;
	tx_info.mode = PKTS_TX;
	tx_info.tx_cnt = tx_cnt;
	tx_info.period = period;
	tx_info.tx_time = tx_time;

	rtw8852b_bb_set_pmac_tx(rtwdev, &tx_info, idx);
}

static void rtw8852b_bb_set_power(Context *rtwdev, s16 pwr_dbm,
			   enum rtw89_phy_idx idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CFG Tx PWR = %d", pwr_dbm);

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, pwr_dbm, idx);
}

static void rtw8852b_bb_cfg_tx_path(Context *rtwdev, u8 tx_path)
{
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 7, RTW89_PHY_0);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CFG Tx Path = %d", tx_path);

	if (tx_path == RF_PATH_A) {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 1);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0);
	} else if (tx_path == RF_PATH_B) {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 2);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0);
	} else if (tx_path == RF_PATH_AB) {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 3);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 4);
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "Error Tx Path");
	}
}

static void rtw8852b_bb_tx_mode_switch(Context *rtwdev,
				enum rtw89_phy_idx idx, u8 mode)
{
	if (mode != 0)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "Tx mode switch");

	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_TXEN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_RXEN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RX_CFG1, B_PMAC_OPT1_MSK, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RXMOD, B_PMAC_RXMOD_MSK, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_DPD_EN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 0, idx);
}

static void rtw8852b_bb_backup_tssi(Context *rtwdev, enum rtw89_phy_idx idx,
			     struct rtw8852b_bb_tssi_bak *bak)
{
	s32 tmp;

	bak->tx_path = rtw89_phy_read32_idx(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, idx);
	bak->rx_path = rtw89_phy_read32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, idx);
	bak->p0_rfmode = rtw89_phy_read32_idx(rtwdev, R_P0_RFMODE, MASKDWORD, idx);
	bak->p0_rfmode_ftm = rtw89_phy_read32_idx(rtwdev, R_P0_RFMODE_FTM_RX, MASKDWORD, idx);
	bak->p1_rfmode = rtw89_phy_read32_idx(rtwdev, R_P1_RFMODE, MASKDWORD, idx);
	bak->p1_rfmode_ftm = rtw89_phy_read32_idx(rtwdev, R_P1_RFMODE_FTM_RX, MASKDWORD, idx);
	tmp = rtw89_phy_read32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, idx);
	bak->tx_pwr = sign_extend32(tmp, 8);
}

static void rtw8852b_bb_restore_tssi(Context *rtwdev, enum rtw89_phy_idx idx,
			      const struct rtw8852b_bb_tssi_bak *bak)
{
	rtw89_phy_write32_idx(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, bak->tx_path, idx);
	if (bak->tx_path == RF_AB)
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0x4);
	else
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0x0);
	rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, bak->rx_path, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE, MASKDWORD, bak->p0_rfmode, idx);
	rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_FTM_RX, MASKDWORD, bak->p0_rfmode_ftm, idx);
	rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE, MASKDWORD, bak->p1_rfmode, idx);
	rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_FTM_RX, MASKDWORD, bak->p1_rfmode_ftm, idx);
	rtw89_phy_write32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, bak->tx_pwr, idx);
}

static void rtw8852b_ctrl_btg_bt_rx(Context *rtwdev, bool en,
				    enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x20);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x30);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x1a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0xc);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}
}

static void rtw8852b_bb_ctrl_rx_path(Context *rtwdev,
			      enum rtw89_rf_path_bit rx_path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u32 rst_mask0;
	u32 rst_mask1;

	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	} else if (rx_path == RF_B) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 2);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 2);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 2);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	} else if (rx_path == RF_AB) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 3);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 3);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 3);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 1);
	}

	rtw8852b_set_gain_offset(rtwdev, chan->subband_type, RTW89_PHY_0);

	if (chan->band_type == RTW89_BAND_2G &&
	    (rx_path == RF_B || rx_path == RF_AB))
		rtw8852b_ctrl_btg_bt_rx(rtwdev, true, RTW89_PHY_0);
	else
		rtw8852b_ctrl_btg_bt_rx(rtwdev, false, RTW89_PHY_0);

	rst_mask0 = B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI;
	rst_mask1 = B_P1_TXPW_RSTB_MANON | B_P1_TXPW_RSTB_TSSI;
	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 3);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 1);
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 3);
	}
}

static u8 rtw8852b_get_thermal(Context *rtwdev, u8 rf_path)
{
	if (rtwdev->is_tssi_mode[rf_path]) {
		u32 addr = 0x1c10 + (rf_path << 13);

		return rtw89_phy_read32_mask(rtwdev, addr, 0x3F000000);
	}

	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	delay(rtwdev,200);

	return rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL);
}

static enum rtw89_gain_offset rtw89_subband_to_gain_offset_band_of_ofdm(enum rtw89_subband subband)
{
	switch (subband) {
	default:
	case RTW89_CH_2G:
		return RTW89_GAIN_OFFSET_2G_OFDM;
	case RTW89_CH_5G_BAND_1:
		return RTW89_GAIN_OFFSET_5G_LOW;
	case RTW89_CH_5G_BAND_3:
		return RTW89_GAIN_OFFSET_5G_MID;
	case RTW89_CH_5G_BAND_4:
		return RTW89_GAIN_OFFSET_5G_HIGH;
	case RTW89_CH_6G_BAND_IDX0:
		return RTW89_GAIN_OFFSET_6G_L0;
	case RTW89_CH_6G_BAND_IDX1:
		return RTW89_GAIN_OFFSET_6G_L1;
	case RTW89_CH_6G_BAND_IDX2:
		return RTW89_GAIN_OFFSET_6G_M0;
	case RTW89_CH_6G_BAND_IDX3:
		return RTW89_GAIN_OFFSET_6G_M1;
	case RTW89_CH_6G_BAND_IDX4:
		return RTW89_GAIN_OFFSET_6G_H0;
	case RTW89_CH_6G_BAND_IDX5:
		return RTW89_GAIN_OFFSET_6G_H1;
	case RTW89_CH_6G_BAND_IDX6:
		return RTW89_GAIN_OFFSET_6G_UH0;
	case RTW89_CH_6G_BAND_IDX7:
		return RTW89_GAIN_OFFSET_6G_UH1;
	}
}
