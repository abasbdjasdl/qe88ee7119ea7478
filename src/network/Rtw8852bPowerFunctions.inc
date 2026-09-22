// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 d1fced1b8a741dc9f92b47c69489c24385945f6e; BSD option. Do not edit.
static void rtw89_phy_fill_txpwr_limit_20m_ax(Context *rtwdev,
					      struct rtw89_txpwr_limit_ax *lmt,
					      u8 band, u8 ntx, u8 ch)
{
	fillPair(rtwdev,lmt->cck_20m, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_CCK, ch);
	fillPair(rtwdev,lmt->cck_40m, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_CCK, ch);
	fillPair(rtwdev,lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, ch);
	fillPair(rtwdev,lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch);
}

static void rtw89_phy_fill_txpwr_limit_40m_ax(Context *rtwdev,
					      struct rtw89_txpwr_limit_ax *lmt,
					      u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	fillPair(rtwdev,lmt->cck_20m, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_CCK, ch - 2);
	fillPair(rtwdev,lmt->cck_40m, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_CCK, ch);
	fillPair(rtwdev,lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, pri_ch);
	fillPair(rtwdev,lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 2);
	fillPair(rtwdev,lmt->mcs_20m[1], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 2);
	fillPair(rtwdev,lmt->mcs_40m[0], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch);
}

static void rtw89_phy_fill_txpwr_limit_80m_ax(Context *rtwdev,
					      struct rtw89_txpwr_limit_ax *lmt,
					      u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	s8 val_0p5_n[RTW89_BF_NUM];
	s8 val_0p5_p[RTW89_BF_NUM];
	u8 i;

	fillPair(rtwdev,lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, pri_ch);
	fillPair(rtwdev,lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 6);
	fillPair(rtwdev,lmt->mcs_20m[1], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 2);
	fillPair(rtwdev,lmt->mcs_20m[2], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 2);
	fillPair(rtwdev,lmt->mcs_20m[3], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 6);
	fillPair(rtwdev,lmt->mcs_40m[0], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 4);
	fillPair(rtwdev,lmt->mcs_40m[1], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 4);
	fillPair(rtwdev,lmt->mcs_80m[0], band,
				    RTW89_CHANNEL_WIDTH_80,
				    ntx, RTW89_RS_MCS, ch);

	fillPair(rtwdev,val_0p5_n, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 4);
	fillPair(rtwdev,val_0p5_p, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 4);

	for (i = 0; i < RTW89_BF_NUM; i++)
		lmt->mcs_40m_0p5[i] = minimum( val_0p5_n[i], val_0p5_p[i]);
}

static void
rtw89_phy_fill_txpwr_limit_ru_20m_ax(Context *rtwdev,
				     struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch);
	lmt_ru->ru52[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch);
	lmt_ru->ru106[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch);
}

static void
rtw89_phy_fill_txpwr_limit_ru_40m_ax(Context *rtwdev,
				     struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch - 2);
	lmt_ru->ru26[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch + 2);
	lmt_ru->ru52[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch - 2);
	lmt_ru->ru52[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch + 2);
	lmt_ru->ru106[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch - 2);
	lmt_ru->ru106[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch + 2);
}

static void
rtw89_phy_fill_txpwr_limit_ru_80m_ax(Context *rtwdev,
				     struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch - 6);
	lmt_ru->ru26[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch - 2);
	lmt_ru->ru26[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch + 2);
	lmt_ru->ru26[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch + 6);
	lmt_ru->ru52[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch - 6);
	lmt_ru->ru52[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch - 2);
	lmt_ru->ru52[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch + 2);
	lmt_ru->ru52[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch + 6);
	lmt_ru->ru106[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch - 6);
	lmt_ru->ru106[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch - 2);
	lmt_ru->ru106[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch + 2);
	lmt_ru->ru106[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch + 6);
}
