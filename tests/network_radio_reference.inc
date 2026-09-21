// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation
// Generated from pinned rtw89 by tools/import_radio_reference.py.
static int rtw89_phy_sel_headline(struct rtw89_dev *rtwdev,
				  const struct rtw89_phy_table *table,
				  u32 *headline_size, u32 *headline_idx,
				  u8 rfe, u8 cv)
{
	const struct rtw89_reg2_def *reg;
	u32 headline;
	u32 compare, target;
	u8 rfe_para, cv_para;
	u8 cv_max = 0;
	bool case_matched = false;
	u32 i;

	for (i = 0; i < table->n_regs; i++) {
		reg = &table->regs[i];
		headline = get_phy_headline(reg->addr);
		if (headline != PHY_HEADLINE_VALID)
			break;
	}
	*headline_size = i;
	if (*headline_size == 0)
		return 0;

	/* case 1: RFE match, CV match */
	compare = get_phy_compare(rfe, cv);
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		target = get_phy_target(reg->addr);
		if (target == compare) {
			*headline_idx = i;
			return 0;
		}
	}

	/* case 2: RFE match, CV don't care */
	compare = get_phy_compare(rfe, PHY_COND_DONT_CARE);
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		target = get_phy_target(reg->addr);
		if (target == compare) {
			*headline_idx = i;
			return 0;
		}
	}

	/* case 3: RFE match, CV max in table */
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		rfe_para = get_phy_cond_rfe(reg->addr);
		cv_para = get_phy_cond_cv(reg->addr);
		if (rfe_para == rfe) {
			if (cv_para >= cv_max) {
				cv_max = cv_para;
				*headline_idx = i;
				case_matched = true;
			}
		}
	}

	if (case_matched)
		return 0;

	/* case 4: RFE don't care, CV max in table */
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		rfe_para = get_phy_cond_rfe(reg->addr);
		cv_para = get_phy_cond_cv(reg->addr);
		if (rfe_para == PHY_COND_DONT_CARE) {
			if (cv_para >= cv_max) {
				cv_max = cv_para;
				*headline_idx = i;
				case_matched = true;
			}
		}
	}

	if (case_matched)
		return 0;

	return -EINVAL;
}
static void rtw89_phy_init_reg(struct rtw89_dev *rtwdev,
			       const struct rtw89_phy_table *table,
			       void (*config)(struct rtw89_dev *rtwdev,
					      const struct rtw89_reg2_def *reg,
					      enum rtw89_rf_path rf_path,
					      void *data),
			       void *extra_data)
{
	const struct rtw89_reg2_def *reg;
	enum rtw89_rf_path rf_path = table->rf_path;
	u8 rfe = rtwdev->efuse.rfe_type;
	u8 cv = rtwdev->hal.cv;
	u32 i;
	u32 headline_size = 0, headline_idx = 0;
	u32 target = 0, cfg_target;
	u8 cond;
	bool is_matched = true;
	bool target_found = false;
	int ret;

	ret = rtw89_phy_sel_headline(rtwdev, table, &headline_size,
				     &headline_idx, rfe, cv);
	if (ret) {
		rtw89_err(rtwdev, "invalid PHY package: %d/%d\n", rfe, cv);
		return;
	}

	cfg_target = get_phy_target(table->regs[headline_idx].addr);
	for (i = headline_size; i < table->n_regs; i++) {
		reg = &table->regs[i];
		cond = get_phy_cond(reg->addr);
		switch (cond) {
		case PHY_COND_BRANCH_IF:
		case PHY_COND_BRANCH_ELIF:
			target = get_phy_target(reg->addr);
			break;
		case PHY_COND_BRANCH_ELSE:
			is_matched = false;
			if (!target_found) {
				rtw89_warn(rtwdev, "failed to load CR %x/%x\n",
					   reg->addr, reg->data);
				return;
			}
			break;
		case PHY_COND_BRANCH_END:
			is_matched = true;
			target_found = false;
			break;
		case PHY_COND_CHECK:
			if (target_found) {
				is_matched = false;
				break;
			}

			if (target == cfg_target) {
				is_matched = true;
				target_found = true;
			} else {
				is_matched = false;
				target_found = false;
			}
			break;
		default:
			if (is_matched)
				config(rtwdev, reg, rf_path, extra_data);
			break;
		}
	}
}
union rtw89_phy_bb_gain_arg {
	u32 addr;
	struct {
		union {
			u8 type;
			struct {
				u8 rxsc_start:4;
				u8 bw:4;
			};
		};
		u8 path;
		u8 gain_band;
		u8 cfg_type;
	};
} __attribute__((packed));
enum rtw89_phy_bb_rxsc_start_idx {
	RTW89_BB_RXSC_START_IDX_FULL = 0,
	RTW89_BB_RXSC_START_IDX_20 = 1,
	RTW89_BB_RXSC_START_IDX_20_1 = 5,
	RTW89_BB_RXSC_START_IDX_40 = 9,
	RTW89_BB_RXSC_START_IDX_80 = 13,
};
static void
rtw89_phy_cfg_bb_gain_error(struct rtw89_dev *rtwdev,
			    union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 type = arg.type;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_gain[gband][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_gain[gband][path][i] = data & 0xff;
		break;
	case 2:
		for (i = 0; i < 2; i++, data >>= 8)
			gain->tia_gain[gband][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain error {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}
static void
rtw89_phy_cfg_bb_rpl_ofst(struct rtw89_dev *rtwdev,
			  union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 rxsc_start = arg.rxsc_start;
	u8 bw = arg.bw;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	u8 rxsc;
	s8 ofst;
	int i;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		gain->rpl_ofst_20[gband][path] = (s8)data;
		break;
	case RTW89_CHANNEL_WIDTH_40:
		if (rxsc_start == RTW89_BB_RXSC_START_IDX_FULL) {
			gain->rpl_ofst_40[gband][path][0] = (s8)data;
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20) {
			for (i = 0; i < 2; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_40[gband][path][rxsc] = ofst;
			}
		}
		break;
	case RTW89_CHANNEL_WIDTH_80:
		if (rxsc_start == RTW89_BB_RXSC_START_IDX_FULL) {
			gain->rpl_ofst_80[gband][path][0] = (s8)data;
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_80[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_40) {
			for (i = 0; i < 2; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_40 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_80[gband][path][rxsc] = ofst;
			}
		}
		break;
	case RTW89_CHANNEL_WIDTH_160:
		if (rxsc_start == RTW89_BB_RXSC_START_IDX_FULL) {
			gain->rpl_ofst_160[gband][path][0] = (s8)data;
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20_1) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20_1 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_40) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_40 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_80) {
			for (i = 0; i < 2; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_80 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		}
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb rpl ofst {0x%x:0x%x} with unknown bw: %d\n",
			   arg.addr, data, bw);
		break;
	}
}
static void
rtw89_phy_cfg_bb_gain_bypass(struct rtw89_dev *rtwdev,
			     union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 type = arg.type;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_gain_bypass[gband][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_gain_bypass[gband][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain bypass {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}
static void
rtw89_phy_cfg_bb_gain_op1db(struct rtw89_dev *rtwdev,
			    union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 type = arg.type;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_op1db[gband][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_op1db[gband][path][i] = data & 0xff;
		break;
	case 2:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->tia_lna_op1db[gband][path][i] = data & 0xff;
		break;
	case 3:
		for (i = 4; i < 8; i++, data >>= 8)
			gain->tia_lna_op1db[gband][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain op1db {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}
static void rtw89_phy_config_bb_gain_ax(struct rtw89_dev *rtwdev,
					const struct rtw89_reg2_def *reg,
					enum rtw89_rf_path rf_path,
					void *extra_data)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	union rtw89_phy_bb_gain_arg arg = { .addr = reg->addr };
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	if (arg.gain_band >= RTW89_BB_GAIN_BAND_NR)
		return;

	if (arg.path >= chip->rf_path_num)
		return;

	if (arg.addr >= 0xf9 && arg.addr <= 0xfe) {
		rtw89_warn(rtwdev, "bb gain table with flow ctrl\n");
		return;
	}

	switch (arg.cfg_type) {
	case 0:
		rtw89_phy_cfg_bb_gain_error(rtwdev, arg, reg->data);
		break;
	case 1:
		rtw89_phy_cfg_bb_rpl_ofst(rtwdev, arg, reg->data);
		break;
	case 2:
		rtw89_phy_cfg_bb_gain_bypass(rtwdev, arg, reg->data);
		break;
	case 3:
		rtw89_phy_cfg_bb_gain_op1db(rtwdev, arg, reg->data);
		break;
	case 4:
		/* This cfg_type is only used by rfe_type >= 50 with eFEM */
		if (efuse->rfe_type < 50)
			break;
		fallthrough;
	default:
		rtw89_warn(rtwdev,
			   "bb gain {0x%x:0x%x} with unknown cfg type: %d\n",
			   arg.addr, reg->data, arg.cfg_type);
		break;
	}
}
