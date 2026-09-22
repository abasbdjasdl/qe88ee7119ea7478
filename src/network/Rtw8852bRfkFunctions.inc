// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 by tools/import_rfk_reference.py.
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
