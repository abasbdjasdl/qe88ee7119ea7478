// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 d1fced1b8a741dc9f92b47c69489c24385945f6e; BSD option. Do not edit.
static void rtw8852b_set_channel_mac(Context *rtwdev,
				     const struct rtw89_chan *chan,
				     u8 mac_idx)
{
	u32 rf_mod = rtw89_mac_reg_by_idx(rtwdev, R_AX_WMAC_RFMOD, mac_idx);
	u32 sub_carr = rtw89_mac_reg_by_idx(rtwdev, R_AX_TX_SUB_CARRIER_VALUE, mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(rtwdev, R_AX_TXRATE_CHK, mac_idx);
	u8 txsc20 = 0, txsc40 = 0;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_40);
		/* fall through */
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, bit(1));
		rtw89_write32(rtwdev, sub_carr, txsc20 | (txsc40 << 4));
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, bit(0));
		rtw89_write32(rtwdev, sub_carr, txsc20);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_write8_clr(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK);
		rtw89_write32(rtwdev, sub_carr, 0);
		break;
	default:
		break;
	}

	if (chan->channel > 14) {
		rtw89_write8_clr(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_set(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	} else {
		rtw89_write8_set(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_clr(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	}
}

static void rtw8852b_ctrl_sco_cck(Context *rtwdev, u8 primary_ch)
{
	u8 ch_element = primary_ch - 1;

	rtw89_phy_write32_mask(rtwdev, R_RXSCOBC, B_RXSCOBC_TH,
			       rtw8852b_sco_barker_threshold[ch_element]);
	rtw89_phy_write32_mask(rtwdev, R_RXSCOCCK, B_RXSCOCCK_TH,
			       rtw8852b_sco_cck_threshold[ch_element]);
}

static u8 rtw8852b_sco_mapping(u8 central_ch)
{
	if (central_ch == 1)
		return 109;
	else if (central_ch >= 2 && central_ch <= 6)
		return 108;
	else if (central_ch >= 7 && central_ch <= 10)
		return 107;
	else if (central_ch >= 11 && central_ch <= 14)
		return 106;
	else if (central_ch == 36 || central_ch == 38)
		return 51;
	else if (central_ch >= 40 && central_ch <= 58)
		return 50;
	else if (central_ch >= 60 && central_ch <= 64)
		return 49;
	else if (central_ch == 100 || central_ch == 102)
		return 48;
	else if (central_ch >= 104 && central_ch <= 126)
		return 47;
	else if (central_ch >= 128 && central_ch <= 151)
		return 46;
	else if (central_ch >= 153 && central_ch <= 177)
		return 45;
	else
		return 0;
}

static void rtw8852b_set_gain_error(Context *rtwdev,
				    u8 subband,
				    u8 path)
{
	const network::BasebandGain *gain = &rtwdev->gain;
	u8 gain_band = rtw89_subband_to_bb_gain_band(subband);
	s32 val;
	u32 reg;
	u32 mask;
	unsigned i;

	for (i = 0; i < LNA_GAIN_NUM; i++) {
		if (subband == RTW89_CH_2G)
			reg = bb_gain_lna[i].gain_g[path];
		else
			reg = bb_gain_lna[i].gain_a[path];

		mask = bb_gain_lna[i].gain_mask;
		val = gain->lna_gain[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);
	}

	for (i = 0; i < TIA_GAIN_NUM; i++) {
		if (subband == RTW89_CH_2G)
			reg = bb_gain_tia[i].gain_g[path];
		else
			reg = bb_gain_tia[i].gain_a[path];

		mask = bb_gain_tia[i].gain_mask;
		val = gain->tia_gain[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);
	}
}

static void rtw8852b_set_gain_offset(Context *rtwdev,
				     u8 subband,
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

static
void rtw8852b_set_rxsc_rpl_comp(Context *rtwdev, u8 subband)
{
	const network::BasebandGain *gain = &rtwdev->gain;
	u8 band = rtw89_subband_to_bb_gain_band(subband);
	u32 val;

	val = fieldPrep(B_P0_RPL1_20_MASK, (gain->rpl_ofst_20[band][RF_PATH_A] +
					     gain->rpl_ofst_20[band][RF_PATH_B]) / 2) |
	      fieldPrep(B_P0_RPL1_40_MASK, (gain->rpl_ofst_40[band][RF_PATH_A][0] +
					     gain->rpl_ofst_40[band][RF_PATH_B][0]) / 2) |
	      fieldPrep(B_P0_RPL1_41_MASK, (gain->rpl_ofst_40[band][RF_PATH_A][1] +
					     gain->rpl_ofst_40[band][RF_PATH_B][1]) / 2);
	val >>= B_P0_RPL1_SHIFT;
	rtw89_phy_write32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_MASK, val);
	rtw89_phy_write32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_MASK, val);

	val = fieldPrep(B_P0_RTL2_42_MASK, (gain->rpl_ofst_40[band][RF_PATH_A][2] +
					     gain->rpl_ofst_40[band][RF_PATH_B][2]) / 2) |
	      fieldPrep(B_P0_RTL2_80_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][0] +
					     gain->rpl_ofst_80[band][RF_PATH_B][0]) / 2) |
	      fieldPrep(B_P0_RTL2_81_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][1] +
					     gain->rpl_ofst_80[band][RF_PATH_B][1]) / 2) |
	      fieldPrep(B_P0_RTL2_8A_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][10] +
					     gain->rpl_ofst_80[band][RF_PATH_B][10]) / 2);
	rtw89_phy_write32(rtwdev, R_P0_RPL2, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL2, val);

	val = fieldPrep(B_P0_RTL3_82_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][2] +
					     gain->rpl_ofst_80[band][RF_PATH_B][2]) / 2) |
	      fieldPrep(B_P0_RTL3_83_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][3] +
					     gain->rpl_ofst_80[band][RF_PATH_B][3]) / 2) |
	      fieldPrep(B_P0_RTL3_84_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][4] +
					     gain->rpl_ofst_80[band][RF_PATH_B][4]) / 2) |
	      fieldPrep(B_P0_RTL3_89_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][9] +
					     gain->rpl_ofst_80[band][RF_PATH_B][9]) / 2);
	rtw89_phy_write32(rtwdev, R_P0_RPL3, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL3, val);
}

static void rtw8852b_ctrl_ch(Context *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 central_ch = chan->channel;
	u8 subband = chan->subband_type;
	u8 sco_comp;
	bool is_2g = central_ch <= 14;

	/* Path A */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 0, phy_idx);

	/* Path B */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
				      B_PATH1_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
				      B_PATH1_BAND_SEL_MSK_V1, 0, phy_idx);

	/* SCO compensate FC setting */
	sco_comp = rtw8852b_sco_mapping(central_ch);
	rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_INV, sco_comp, phy_idx);

	if (chan->band_type == RTW89_BAND_6G)
		return;

	/* CCK parameters */
	if (central_ch == 14) {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3b13ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x1c42de);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfdb0ad);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xf60f6e);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xfd8f92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0x2d011);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0x1c02c);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xfff00a);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3d23ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x29b354);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfc1c8);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xfdb053);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xf86f9a);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0xfaef92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0xfe5fcc);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xffdff5);
	}

	rtw8852b_set_gain_error(rtwdev, subband, RF_PATH_A);
	rtw8852b_set_gain_error(rtwdev, subband, RF_PATH_B);
	rtw8852b_set_gain_offset(rtwdev, subband, phy_idx);
	rtw8852b_set_rxsc_rpl_comp(rtwdev, subband);
}

static void rtw8852b_bw_setting(Context *rtwdev, u8 bw, u8 path)
{
	static const u32 adc_sel[2] = {0xC0EC, 0xC1EC};
	static const u32 wbadc_sel[2] = {0xC0E4, 0xC1E4};

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x1);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x2);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x1);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
	}
}

static void rtw8852b_ctrl_bw(Context *rtwdev, u8 pri_ch, u8 bw,
			     enum rtw89_phy_idx phy_idx)
{
	u32 rx_path_0;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		/*CCK primary channel */
		if (pri_ch == RTW89_SC_20_UPPER)
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 1);
		else
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 0);

		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);

		/*Set RF mode at A */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0xaaa, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0xaaa, phy_idx);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to switch bw (bw:%d, pri ch:%d)\n", bw,
			   pri_ch);
	}

	rtw8852b_bw_setting(rtwdev, bw, RF_PATH_A);
	rtw8852b_bw_setting(rtwdev, bw, RF_PATH_B);

	rx_path_0 = rtw89_phy_read32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0,
					 phy_idx);
	if (rx_path_0 == 0x1)
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x111, phy_idx);
	else if (rx_path_0 == 0x2)
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x111, phy_idx);
}

static void rtw8852b_ctrl_cck_en(Context *rtwdev, bool cck_en)
{
	if (cck_en) {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 1);
	}
}

static void rtw8852b_5m_mask(Context *rtwdev, const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 pri_ch = chan->pri_ch_idx;
	bool mask_5m_low;
	bool mask_5m_en;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_40:
		/* Prich=1: Mask 5M High, Prich=2: Mask 5M Low */
		mask_5m_en = true;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWER;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		/* Prich=3: Mask 5M High, Prich=4: Mask 5M Low, Else: Disable */
		mask_5m_en = pri_ch == RTW89_SC_20_UPMOST ||
			     pri_ch == RTW89_SC_20_LOWEST;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWEST;
		break;
	default:
		mask_5m_en = false;
		break;
	}

	if (!mask_5m_en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x0);
		rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
				      B_ASSIGN_SBD_OPT_EN_V1, 0x0, phy_idx);
		return;
	}

	if (mask_5m_low) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB0, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB0, 0x0);
	}
	rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
			      B_ASSIGN_SBD_OPT_EN_V1, 0x1, phy_idx);
}

static void rtw8852b_bb_reset_all(Context *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	delay(rtwdev,1);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
}

static void rtw8852b_bb_reset_en(Context *rtwdev, enum rtw89_band band,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
		if (band == RTW89_BAND_2G)
			rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x1);
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		delay(rtwdev,1);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	}
}

static void rtw8852b_bb_set_pop(Context *rtwdev)
{
	if (rtwdev->monitor)
		rtw89_phy_write32_clr(rtwdev, R_PKT_CTRL, B_PKT_POP_EN);
}

static void rtw8852b_set_channel_bb(Context *rtwdev, const struct rtw89_chan *chan,
				    enum rtw89_phy_idx phy_idx)
{
	bool cck_en = chan->channel <= 14;
	u8 pri_ch_idx = chan->pri_ch_idx;
	u8 band = chan->band_type, chan_idx;

	if (cck_en)
		rtw8852b_ctrl_sco_cck(rtwdev,  chan->primary_channel);

	rtw8852b_ctrl_ch(rtwdev, chan, phy_idx);
	rtw8852b_ctrl_bw(rtwdev, pri_ch_idx, chan->band_width, phy_idx);
	rtw8852b_ctrl_cck_en(rtwdev, cck_en);
	if (chan->band_type == RTW89_BAND_5G) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}
	chan_idx = rtw89_encode_chan_idx(rtwdev, chan->primary_channel, band);
	rtw89_phy_write32_mask(rtwdev, R_MAC_PIN_SEL, B_CH_IDX_SEG0, chan_idx);
	rtw8852b_5m_mask(rtwdev, chan, phy_idx);
	rtw8852b_bb_set_pop(rtwdev);
	rtw8852b_bb_reset_all(rtwdev, phy_idx);
}

static void rtw8852b_set_channel(Context *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtw8852b_set_channel_mac(rtwdev, chan, mac_idx);
	rtw8852b_set_channel_bb(rtwdev, chan, phy_idx);
	rtw8852b_set_channel_rf(rtwdev, chan, phy_idx);
}

static void rtw8852b_tssi_cont_en(Context *rtwdev, bool en,
				  u8 path)
{
	static const u32 tssi_trk[2] = {R_P0_TSSI_TRK, R_P1_TSSI_TRK};
	static const u32 ctrl_bbrst[2] = {R_P0_TXPW_RSTB, R_P1_TXPW_RSTB};

	if (en) {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], B_P0_TXPW_RSTB_MANON, 0x0);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], B_P0_TSSI_TRK_EN, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], B_P0_TXPW_RSTB_MANON, 0x1);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], B_P0_TSSI_TRK_EN, 0x1);
	}
}

static void rtw8852b_tssi_cont_en_phyidx(Context *rtwdev, bool en,
					 u8 phy_idx)
{
	if (!rtwdev->dbcc_en) {
		rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		if (phy_idx == RTW89_PHY_0)
			rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}

static void rtw8852b_adc_en(Context *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0xf);
}

static void rtw8852b_set_channel_help(Context *rtwdev, bool enter,
				      struct rtw89_channel_help_params *p,
				      const struct rtw89_chan *chan,
				      enum rtw89_mac_idx mac_idx,
				      enum rtw89_phy_idx phy_idx)
{
	if (enter) {
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw8852b_tssi_cont_en_phyidx(rtwdev, false, RTW89_PHY_0);
		rtw8852b_adc_en(rtwdev, false);
		delay(rtwdev,40);
		rtw8852b_bb_reset_en(rtwdev, chan->band_type, phy_idx, false);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw8852b_adc_en(rtwdev, true);
		rtw8852b_tssi_cont_en_phyidx(rtwdev, true, RTW89_PHY_0);
		rtw8852b_bb_reset_en(rtwdev, chan->band_type, phy_idx, true);
	}
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

static void _bw_setting(Context *rtwdev, u8 path,
			enum rtw89_bandwidth bw, bool dav)
{
	u32 rf_reg18;
	u32 reg18_addr = dav ? RR_CFGCH : RR_CFGCH_V1;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===> %s\n", __func__);

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK);
	if (rf_reg18 == INV_RF_DATA) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]Invalid RF_0x18 for Path-%d\n", path);
		return;
	}
	rf_reg18 &= ~RR_CFGCH_BW;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
		rf_reg18 |= fieldPrep(RR_CFGCH_BW, CFGCH_BW_20M);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rf_reg18 |= fieldPrep(RR_CFGCH_BW, CFGCH_BW_40M);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rf_reg18 |= fieldPrep(RR_CFGCH_BW, CFGCH_BW_80M);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]Fail to set CH\n");
	}

	rf_reg18 &= ~(RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN |
		      RR_CFGCH_BW2) & RFREG_MASK;
	rf_reg18 |= RR_CFGCH_BW2;
	rtw89_write_rf(rtwdev, path, reg18_addr, RFREG_MASK, rf_reg18);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK] set %x at path%d, %x =0x%x\n",
		    bw, path, reg18_addr,
		    rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK));
}

static void _ctrl_bw(Context *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	_bw_setting(rtwdev, RF_PATH_A, bw, true);
	_bw_setting(rtwdev, RF_PATH_B, bw, true);
	_bw_setting(rtwdev, RF_PATH_A, bw, false);
	_bw_setting(rtwdev, RF_PATH_B, bw, false);
}

static bool _set_s0_arfc18(Context *rtwdev, u32 val)
{
	u32 bak;
	u32 tmp;
	int ret;

	bak = rtw89_read_rf(rtwdev, RF_PATH_A, RR_LDO, RFREG_MASK);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LDO, RR_LDO_SEL, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK, val);

	ret = R16_CHANNEL_POLL(rtw89_read_rf, tmp, tmp == 0, 1, 1000,
				       false, rtwdev, RF_PATH_A, RR_LPF, RR_LPF_BUSY);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]LCK timeout\n");

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LDO, RFREG_MASK, bak);

	return !!ret;
}

static void _lck_check(Context *rtwdev)
{
	u32 tmp;

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]SYN MMD reset\n");

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_EN, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_SYN, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_SYN, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_EN, 0x0);
	}

	delay(rtwdev,10);

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]re-set RF 0x18\n");

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
		_set_s0_arfc18(rtwdev, tmp);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);
	}

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]SYN off/on\n");

		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_POW, RFREG_MASK);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RFREG_MASK, tmp);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_SX, RFREG_MASK);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_SX, RFREG_MASK, tmp);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_SYNLUT, RR_SYNLUT_MOD, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_SYNLUT, RR_SYNLUT_MOD, 0x0);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
		_set_s0_arfc18(rtwdev, tmp);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]0xb2=%x, 0xc5=%x\n",
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_VCO, RFREG_MASK),
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RFREG_MASK));
	}
	if (check(rtwdev) && rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) != 1)
		fail(rtwdev,Error::pllUnlocked);
}

static void _set_ch(Context *rtwdev, u32 val)
{
	bool timeout;

	timeout = _set_s0_arfc18(rtwdev, val);
	if (!timeout)
		_lck_check(rtwdev);
}

static void _ch_setting(Context *rtwdev, u8 path,
			u8 central_ch, bool dav)
{
	u32 reg18_addr = dav ? RR_CFGCH : RR_CFGCH_V1;
	bool is_2g_ch = central_ch <= 14;
	u32 rf_reg18;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===> %s\n", __func__);

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK);
	rf_reg18 &= ~(RR_CFGCH_BAND1 | RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH |
		      RR_CFGCH_BCN | RR_CFGCH_BAND0 | RR_CFGCH_CH);
	rf_reg18 |= fieldPrep(RR_CFGCH_CH, central_ch);

	if (!is_2g_ch)
		rf_reg18 |= fieldPrep(RR_CFGCH_BAND1, CFGCH_BAND1_5G) |
			    fieldPrep(RR_CFGCH_BAND0, CFGCH_BAND0_5G);

	rf_reg18 &= ~(RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN |
		      RR_CFGCH_BW2) & RFREG_MASK;
	rf_reg18 |= RR_CFGCH_BW2;

	if (path == RF_PATH_A && dav)
		_set_ch(rtwdev, rf_reg18);
	else
		rtw89_write_rf(rtwdev, path, reg18_addr, RFREG_MASK, rf_reg18);

	rtw89_write_rf(rtwdev, path, RR_LCKST, RR_LCKST_BIN, 0);
	rtw89_write_rf(rtwdev, path, RR_LCKST, RR_LCKST_BIN, 1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RFK]CH: %d for Path-%d, reg0x%x = 0x%x\n",
		    central_ch, path, reg18_addr,
		    rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK));
}

static void _ctrl_ch(Context *rtwdev, u8 central_ch)
{
	_ch_setting(rtwdev, RF_PATH_A, central_ch, true);
	_ch_setting(rtwdev, RF_PATH_B, central_ch, true);
	_ch_setting(rtwdev, RF_PATH_A, central_ch, false);
	_ch_setting(rtwdev, RF_PATH_B, central_ch, false);
}

static void _set_rxbb_bw(Context *rtwdev, enum rtw89_bandwidth bw,
			 u8 path)
{
	rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x1);
	rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M2, 0x12);

	if (bw == RTW89_CHANNEL_WIDTH_20)
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0x1b);
	else if (bw == RTW89_CHANNEL_WIDTH_40)
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0x13);
	else if (bw == RTW89_CHANNEL_WIDTH_80)
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0xb);
	else
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0x3);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK] set S%d RXBB BW 0x3F = 0x%x\n", path,
		    rtw89_read_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB));

	rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x0);
}

static void _rxbb_bw(Context *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	u8 kpath, path;

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < RF_PATH_NUM_8852B; path++) {
		if (!(kpath & bit(path)))
			continue;

		_set_rxbb_bw(rtwdev, bw, path);
	}
}

static void rtw8852b_ctrl_bw_ch(Context *rtwdev,
				enum rtw89_phy_idx phy, u8 central_ch,
				enum rtw89_band band, enum rtw89_bandwidth bw)
{
	_ctrl_ch(rtwdev, central_ch);
	_ctrl_bw(rtwdev, phy, bw);
	_rxbb_bw(rtwdev, phy, bw);
}

static void rtw8852b_set_channel_rf(Context *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8852b_ctrl_bw_ch(rtwdev, phy_idx, chan->channel, chan->band_type,
			    chan->band_width);
}

static u8 rtw89_phy_get_txsc(Context *rtwdev,
		      const struct rtw89_chan *chan,
		      enum rtw89_bandwidth dbw)
{
	enum rtw89_bandwidth cbw = chan->band_width;
	u8 pri_ch = chan->primary_channel;
	u8 central_ch = chan->channel;
	u8 txsc_idx = 0;
	u8 tmp = 0;

	if (cbw == dbw || cbw == RTW89_CHANNEL_WIDTH_20)
		return txsc_idx;

	switch (cbw) {
	case RTW89_CHANNEL_WIDTH_40:
		txsc_idx = pri_ch > central_ch ? 1 : 2;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		if (dbw == RTW89_CHANNEL_WIDTH_20) {
			if (pri_ch > central_ch)
				txsc_idx = (pri_ch - central_ch) >> 1;
			else
				txsc_idx = ((central_ch - pri_ch) >> 1) + 1;
		} else {
			txsc_idx = pri_ch > central_ch ? 9 : 10;
		}
		break;
	case RTW89_CHANNEL_WIDTH_160:
		if (pri_ch > central_ch)
			tmp = (pri_ch - central_ch) >> 1;
		else
			tmp = ((central_ch - pri_ch) >> 1) + 1;

		if (dbw == RTW89_CHANNEL_WIDTH_20) {
			txsc_idx = tmp;
		} else if (dbw == RTW89_CHANNEL_WIDTH_40) {
			if (tmp == 1 || tmp == 3)
				txsc_idx = 9;
			else if (tmp == 5 || tmp == 7)
				txsc_idx = 11;
			else if (tmp == 2 || tmp == 4)
				txsc_idx = 10;
			else if (tmp == 6 || tmp == 8)
				txsc_idx = 12;
			else
				return 0xff;
		} else {
			txsc_idx = pri_ch > central_ch ? 13 : 14;
		}
		break;
	case RTW89_CHANNEL_WIDTH_80_80:
		if (dbw == RTW89_CHANNEL_WIDTH_20) {
			if (pri_ch > central_ch)
				txsc_idx = (10 - (pri_ch - central_ch)) >> 1;
			else
				txsc_idx = ((central_ch - pri_ch) >> 1) + 5;
		} else if (dbw == RTW89_CHANNEL_WIDTH_40) {
			txsc_idx = pri_ch > central_ch ? 10 : 12;
		} else {
			txsc_idx = 14;
		}
		break;
	default:
		break;
	}

	return txsc_idx;
}

static u8 rtw89_encode_chan_idx(Context *rtwdev, u8 central_ch, u8 band)
{
	u8 chan_idx;
	u8 last, first;
	u8 idx;

	switch (band) {
	case RTW89_BAND_2G:
		chan_idx = fieldPrep(RTW89_CH_BASE_IDX_MASK, RTW89_CH_BASE_IDX_2G) |
			   fieldPrep(RTW89_CH_OFFSET_MASK, central_ch);
		return chan_idx;
	case RTW89_BAND_5G:
		first = RTW89_CH_BASE_IDX_5G_FIRST;
		last = RTW89_CH_BASE_IDX_5G_LAST;
		break;
	case RTW89_BAND_6G:
		first = RTW89_CH_BASE_IDX_6G_FIRST;
		last = RTW89_CH_BASE_IDX_6G_LAST;
		break;
	default:
		rtw89_warn(rtwdev, "Unsupported band %d\n", band);
		return 0;
	}

	for (idx = last; idx >= first; idx--)
		if (central_ch >= rtw89_ch_base_table[idx])
			break;

	if (idx < first) {
		rtw89_warn(rtwdev, "Unknown band %d channel %d\n", band, central_ch);
		return 0;
	}

	chan_idx = fieldPrep(RTW89_CH_BASE_IDX_MASK, idx) |
		   fieldPrep(RTW89_CH_OFFSET_MASK,
			      (central_ch - rtw89_ch_base_table[idx]) >> 1);
	return chan_idx;
}

static enum rtw89_phy_bb_gain_band rtw89_subband_to_bb_gain_band(u8 subband)
{
	switch (subband) {
	default:
	case RTW89_CH_2G:
		return RTW89_BB_GAIN_BAND_2G;
	case RTW89_CH_5G_BAND_1:
		return RTW89_BB_GAIN_BAND_5G_L;
	case RTW89_CH_5G_BAND_3:
		return RTW89_BB_GAIN_BAND_5G_M;
	case RTW89_CH_5G_BAND_4:
		return RTW89_BB_GAIN_BAND_5G_H;
	case RTW89_CH_6G_BAND_IDX0:
	case RTW89_CH_6G_BAND_IDX1:
		return RTW89_BB_GAIN_BAND_6G_L;
	case RTW89_CH_6G_BAND_IDX2:
	case RTW89_CH_6G_BAND_IDX3:
		return RTW89_BB_GAIN_BAND_6G_M;
	case RTW89_CH_6G_BAND_IDX4:
	case RTW89_CH_6G_BAND_IDX5:
		return RTW89_BB_GAIN_BAND_6G_H;
	case RTW89_CH_6G_BAND_IDX6:
	case RTW89_CH_6G_BAND_IDX7:
		return RTW89_BB_GAIN_BAND_6G_UH;
	}
}

static enum rtw89_gain_offset rtw89_subband_to_gain_offset_band_of_ofdm(u8 subband)
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

static
int rtw89_mac_cfg_ppdu_status_ax(Context *rtwdev, u8 mac_idx, bool enable)
{
	u32 reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PPDU_STAT, mac_idx);
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (!enable) {
		rtw89_write32_clr(rtwdev, reg, B_AX_PPDU_STAT_RPT_EN);
		return 0;
	}

	rtw89_write32(rtwdev, reg, B_AX_PPDU_STAT_RPT_EN |
				   B_AX_APP_MAC_INFO_RPT |
				   B_AX_APP_RX_CNT_RPT | B_AX_APP_PLCP_HDR_RPT |
				   B_AX_PPDU_STAT_RPT_CRC32);
	rtw89_write32_mask(rtwdev, R_AX_HW_RPT_FWD, B_AX_FWD_PPDU_STAT_MASK,
			   RTW89_PRPT_DEST_HOST);

	return 0;
}
