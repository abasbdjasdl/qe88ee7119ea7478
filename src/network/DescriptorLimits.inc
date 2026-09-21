// Generated from pinned rtw89 TXWD masks.
    if (p.ch_dma > 15u) return false;
    if (p.hdr_llc_len > 31u) return false;
    if (p.hw_ssn_sel > 3u) return false;
    if (p.hw_seq_mode > 3u) return false;
    if (p.qsel > 63u) return false;
    if (p.pkt_size > 16383u) return false;
    if (p.mac_id > 127u) return false;
    if (p.seq > 4095u) return false;
    if (p.data_rate > 511u) return false;
    if (p.port > 7u) return false;
    if (p.data_retry_lowest_rate > 511u) return false;
    if (p.ampdu_density > 7u) return false;
    if (p.sec_type > 15u) return false;
