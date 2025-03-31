
int r8153_u1u2en(struct usbh_rtl8152 *tp, bool enable)
{
    uint8_t u1u2[8];

    if (enable)
        memset(u1u2, 0xff, sizeof(u1u2));
    else
        memset(u1u2, 0x00, sizeof(u1u2));

    return usb_ocp_write(tp, USB_TOLERANCE, BYTE_EN_SIX_BYTES, sizeof(u1u2),
                         u1u2);
}

static int r8153b_u1u2en(struct usbh_rtl8152 *tp, bool enable)
{
    if (enable)
        return ocp_word_set_bits(tp, MCU_TYPE_USB, USB_LPM_CONFIG,
                                 LPM_U1U2_EN);
    else
        return ocp_word_clr_bits(tp, MCU_TYPE_USB, USB_LPM_CONFIG,
                                 LPM_U1U2_EN);
}

static int r8153_u2p3en(struct usbh_rtl8152 *tp, bool enable)
{
    if (enable)
        return ocp_word_set_bits(tp, MCU_TYPE_USB, USB_U2P3_CTRL,
                                 U2P3_ENABLE);
    else
        return ocp_word_clr_bits(tp, MCU_TYPE_USB, USB_U2P3_CTRL,
                                 U2P3_ENABLE);
}

static int r8153b_green_en(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    if (enable) {
        /* 10M abiq&ldvbias */
        ret = sram_write(tp, 0x8045, 0);
        if (ret < 0)
            goto out;

        /* 100M short abiq&ldvbias */
        ret = sram_write(tp, 0x804d, 0x1222);
        if (ret < 0)
            goto out;

        /* 1000M short abiq&ldvbias */
        ret = sram_write(tp, 0x805d, 0x0022);
    } else {
        /* 10M abiq&ldvbias */
        ret = sram_write(tp, 0x8045, 0x2444);
        if (ret < 0)
            goto out;

        /* 100M short abiq&ldvbias */
        ret = sram_write(tp, 0x804d, 0x2444);
        if (ret < 0)
            goto out;

        /* 1000M short abiq&ldvbias */
        ret = sram_write(tp, 0x805d, 0x2444);
    }

    if (ret < 0)
        goto out;

    ret = rtl_green_en(tp, true);

out:
    return ret;
}

static int r8153_phy_status(struct usbh_rtl8152 *tp, uint16_t desired)
{
    int i, ret;
    uint16_t data;

    for (i = 0; i < 500; i++) {
        ret = ocp_reg_read(tp, OCP_PHY_STATUS, &data);
        if (ret < 0)
            return ret;

        data &= PHY_STAT_MASK;
        if (desired) {
            if (data == desired)
                break;
        } else if (data == PHY_STAT_LAN_ON || data == PHY_STAT_PWRDN ||
                   data == PHY_STAT_EXT_INIT) {
            break;
        }

        usb_osal_msleep(20);
        if (test_bit(RTL8152_UNPLUG, &tp->flags))
            break;
    }

    return data;
}

static int r8153b_ups_flags(struct usbh_rtl8152 *tp)
{
    uint32_t ups_flags = 0;

    if (tp->ups_info.green)
        ups_flags |= UPS_FLAGS_EN_GREEN;

    if (tp->ups_info.aldps)
        ups_flags |= UPS_FLAGS_EN_ALDPS;

    if (tp->ups_info.eee)
        ups_flags |= UPS_FLAGS_EN_EEE;

    if (tp->ups_info.flow_control)
        ups_flags |= UPS_FLAGS_EN_FLOW_CTR;

    if (tp->ups_info.eee_ckdiv)
        ups_flags |= UPS_FLAGS_EN_EEE_CKDIV;

    if (tp->ups_info.eee_cmod_lv)
        ups_flags |= UPS_FLAGS_EEE_CMOD_LV_EN;

    if (tp->ups_info.r_tune)
        ups_flags |= UPS_FLAGS_R_TUNE;

    if (tp->ups_info._10m_ckdiv)
        ups_flags |= UPS_FLAGS_EN_10M_CKDIV;

    if (tp->ups_info.eee_plloff_100)
        ups_flags |= UPS_FLAGS_EEE_PLLOFF_100;

    if (tp->ups_info.eee_plloff_giga)
        ups_flags |= UPS_FLAGS_EEE_PLLOFF_GIGA;

    if (tp->ups_info._250m_ckdiv)
        ups_flags |= UPS_FLAGS_250M_CKDIV;

    if (tp->ups_info.ctap_short_off)
        ups_flags |= UPS_FLAGS_CTAP_SHORT_DIS;

    switch (tp->ups_info.speed_duplex) {
        case NWAY_10M_HALF:
            ups_flags |= ups_flags_speed(1);
            break;
        case NWAY_10M_FULL:
            ups_flags |= ups_flags_speed(2);
            break;
        case NWAY_100M_HALF:
            ups_flags |= ups_flags_speed(3);
            break;
        case NWAY_100M_FULL:
            ups_flags |= ups_flags_speed(4);
            break;
        case NWAY_1000M_FULL:
            ups_flags |= ups_flags_speed(5);
            break;
        case FORCE_10M_HALF:
            ups_flags |= ups_flags_speed(6);
            break;
        case FORCE_10M_FULL:
            ups_flags |= ups_flags_speed(7);
            break;
        case FORCE_100M_HALF:
            ups_flags |= ups_flags_speed(8);
            break;
        case FORCE_100M_FULL:
            ups_flags |= ups_flags_speed(9);
            break;
        default:
            break;
    }

    return ocp_write_dword(tp, MCU_TYPE_USB, USB_UPS_FLAGS, ups_flags);
}

static int r8153_queue_wake(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    if (enable)
        ret = ocp_byte_set_bits(tp, MCU_TYPE_PLA, PLA_INDICATE_FALG,
                                UPCOMING_RUNTIME_D3);
    else
        ret = ocp_byte_clr_bits(tp, MCU_TYPE_PLA, PLA_INDICATE_FALG,
                                UPCOMING_RUNTIME_D3);
    if (ret < 0)
        goto out;

    ret = ocp_byte_clr_bits(tp, MCU_TYPE_PLA, PLA_SUSPEND_FLAG,
                            LINK_CHG_EVENT);
    if (ret < 0)
        goto out;

    ret = ocp_word_clr_bits(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS,
                            LINK_CHANGE_FLAG);

out:
    return ret;
}

static int r8153_lanwake_clr_en(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    if (enable) {
        /* Enable the feature that the MCU could clear the lanwake */
        ret = ocp_byte_set_bits(tp, MCU_TYPE_PLA, PLA_CONFIG6,
                                LANWAKE_CLR_EN);
        if (ret < 0)
            goto out;

        /* Clear lanwake */
        ret = ocp_byte_clr_bits(tp, MCU_TYPE_PLA, PLA_LWAKE_CTRL_REG,
                                LANWAKE_PIN);
    } else {
        /* Disable the feature that the MCU could clear the lanwake */
        ret = ocp_byte_clr_bits(tp, MCU_TYPE_PLA, PLA_CONFIG6,
                                LANWAKE_CLR_EN);
    }

out:
    return ret;
}

static int r8153_mac_clk_speed_down(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    /* MAC clock speed down */
    if (enable)
        ret = ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL2,
                                MAC_CLK_SPDWN_EN);
    else
        ret = ocp_word_clr_bits(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL2,
                                MAC_CLK_SPDWN_EN);

    return ret;
}

static int r8153_aldps_en(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    if (enable) {
        ret = ocp_reg_set_bits(tp, OCP_POWER_CFG, EN_ALDPS);
    } else {
        int i;

        ret = ocp_reg_clr_bits(tp, OCP_POWER_CFG, EN_ALDPS);
        if (ret < 0)
            goto out;
        for (i = 0; i < 20; i++) {
            uint32_t ocp_data;

            usb_osal_msleep(2);
            ret = ocp_read_word(tp, MCU_TYPE_PLA, 0xe000,
                                &ocp_data);
            if (ret < 0 || (ocp_data & 0x0100))
                break;
        }
    }

    tp->ups_info.aldps = enable;

out:
    return ret;
}

static int r8153b_mcu_spdown_en(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    if (enable)
        ret = ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3,
                                PLA_MCU_SPDWN_EN);
    else
        ret = ocp_word_clr_bits(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3,
                                PLA_MCU_SPDWN_EN);

    return ret;
}

static int r8153_eee_en(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;

    if (enable) {
        ret = ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_EEE_CR,
                                EEE_RX_EN | EEE_TX_EN);
        if (ret < 0)
            goto out;
        ret = ocp_reg_set_bits(tp, OCP_EEE_CFG, EEE10_EN);
        ret = ocp_reg_write(tp, OCP_EEE_ADV, tp->eee_adv);
    } else {
        ret = ocp_word_clr_bits(tp, MCU_TYPE_PLA, PLA_EEE_CR,
                                EEE_RX_EN | EEE_TX_EN);
        if (ret < 0)
            goto out;
        ret = ocp_reg_clr_bits(tp, OCP_EEE_CFG, EEE10_EN);
        ret = ocp_reg_write(tp, OCP_EEE_ADV, 0);
    }

    tp->ups_info.eee = enable;

out:
    return ret;
}

static void r8153b_firmware(struct usbh_rtl8152 *tp)
{
    uint8_t ocp_data;

    if (tp->version == RTL_VER_09) {
        static uint8_t usb_patch2_b[] = {
            0x10, 0xe0, 0x5b, 0xe0,
            0x7c, 0xe0, 0x9c, 0xe0,
            0xb0, 0xe0, 0xc9, 0xe0,
            0xea, 0xe0, 0x46, 0xe1,
            0x62, 0xe1, 0x65, 0xe1,
            0x7d, 0xe1, 0x8f, 0xe1,
            0x97, 0xe1, 0xf5, 0xe1,
            0x11, 0xe2, 0x22, 0xe2,
            0x43, 0xc4, 0x80, 0x63,
            0xb2, 0x49, 0x05, 0xf0,
            0x41, 0xc4, 0x02, 0xc3,
            0x00, 0xbb, 0x88, 0x3d,
            0x64, 0xc4, 0x3b, 0xc3,
            0x84, 0x9b, 0x00, 0x1b,
            0x86, 0x8b, 0x86, 0x73,
            0xbf, 0x49, 0xfe, 0xf1,
            0x80, 0x73, 0x35, 0xc2,
            0x40, 0x9b, 0x34, 0xc3,
            0x80, 0x9b, 0x83, 0x1b,
            0x86, 0x8b, 0x86, 0x73,
            0xbf, 0x49, 0xfe, 0xf1,
            0x2e, 0xc3, 0x84, 0x9b,
            0x00, 0x1b, 0x86, 0x8b,
            0x86, 0x73, 0xbf, 0x49,
            0xfe, 0xf1, 0x80, 0x73,
            0xba, 0x48, 0xbb, 0x48,
            0x80, 0x9b, 0x83, 0x1b,
            0x86, 0x8b, 0x86, 0x73,
            0xbf, 0x49, 0xfe, 0xf1,
            0x20, 0xc3, 0x84, 0x9b,
            0x1f, 0xc3, 0x80, 0x9b,
            0x83, 0x1b, 0x86, 0x8b,
            0x86, 0x73, 0xbf, 0x49,
            0xfe, 0xf1, 0x11, 0xc3,
            0x84, 0x9b, 0x40, 0x73,
            0x80, 0x9b, 0x83, 0x1b,
            0x86, 0x8b, 0x86, 0x73,
            0xbf, 0x49, 0xfe, 0xf1,
            0x0d, 0xc4, 0x80, 0x73,
            0xbb, 0x48, 0x80, 0x9b,
            0x02, 0xc3, 0x00, 0xbb,
            0x06, 0x3e, 0xee, 0xcf,
            0x6c, 0xe8, 0xe0, 0xcb,
            0x2e, 0xc3, 0x00, 0xa0,
            0x08, 0xb4, 0x4a, 0xd8,
            0x00, 0xb4, 0x00, 0x92,
            0x1c, 0xc6, 0xc0, 0x61,
            0x04, 0x11, 0x15, 0xf1,
            0x19, 0xc6, 0xc0, 0x61,
            0x9c, 0x20, 0x9c, 0x24,
            0x09, 0x11, 0x0f, 0xf1,
            0x14, 0xc6, 0x01, 0x19,
            0xc0, 0x89, 0x13, 0xc1,
            0x13, 0xc6, 0x24, 0x9e,
            0x00, 0x1e, 0x26, 0x8e,
            0x26, 0x76, 0xef, 0x49,
            0xfe, 0xf1, 0x22, 0x76,
            0x08, 0xc1, 0x22, 0x9e,
            0x07, 0xc6, 0x02, 0xc1,
            0x00, 0xb9, 0x8c, 0x08,
            0x18, 0xb4, 0x4a, 0xb4,
            0x90, 0xcc, 0x80, 0xd4,
            0x08, 0xdc, 0x10, 0xe8,
            0x1f, 0xc0, 0x00, 0x75,
            0xd1, 0x49, 0x15, 0xf0,
            0x19, 0xc7, 0x17, 0xc2,
            0xec, 0x9a, 0x00, 0x19,
            0xee, 0x89, 0xee, 0x71,
            0x9f, 0x49, 0xfe, 0xf1,
            0xea, 0x71, 0x9f, 0x49,
            0x0a, 0xf0, 0x11, 0xc2,
            0xec, 0x9a, 0x00, 0x19,
            0xe8, 0x99, 0x81, 0x19,
            0xee, 0x89, 0xee, 0x71,
            0x9f, 0x49, 0xfe, 0xf1,
            0x06, 0xc3, 0x02, 0xc2,
            0x00, 0xba, 0xf0, 0x1d,
            0x4c, 0xe8, 0x00, 0xdc,
            0x00, 0xd4, 0x34, 0xd3,
            0x24, 0xe4, 0x7b, 0xc0,
            0x00, 0x75, 0xd1, 0x49,
            0x0d, 0xf0, 0x74, 0xc0,
            0x74, 0xc5, 0x00, 0x1e,
            0x08, 0x9e, 0x72, 0xc6,
            0x0a, 0x9e, 0x0c, 0x9d,
            0x8f, 0x1c, 0x0e, 0x8c,
            0x0e, 0x74, 0xcf, 0x49,
            0xfe, 0xf1, 0x04, 0xc0,
            0x02, 0xc1, 0x00, 0xb9,
            0xc4, 0x16, 0x20, 0xd4,
            0x66, 0xc0, 0x00, 0x75,
            0xd1, 0x48, 0x00, 0x9d,
            0xe3, 0xc7, 0x5f, 0xc2,
            0xec, 0x9a, 0x00, 0x19,
            0xe8, 0x9a, 0x81, 0x19,
            0xee, 0x89, 0xee, 0x71,
            0x9f, 0x49, 0xfe, 0xf1,
            0x2c, 0xc1, 0xec, 0x99,
            0x81, 0x19, 0xee, 0x89,
            0xee, 0x71, 0x9f, 0x49,
            0xfe, 0xf1, 0x04, 0xc3,
            0x02, 0xc2, 0x00, 0xba,
            0x96, 0x1c, 0xc0, 0xd4,
            0xc0, 0x88, 0x1e, 0xc6,
            0xc0, 0x70, 0x8f, 0x49,
            0x0e, 0xf0, 0x8f, 0x48,
            0x1b, 0xc6, 0xca, 0x98,
            0x11, 0x18, 0xc8, 0x98,
            0x16, 0xc0, 0xcc, 0x98,
            0x8f, 0x18, 0xce, 0x88,
            0xce, 0x70, 0x8f, 0x49,
            0xfe, 0xf1, 0x0b, 0xe0,
            0x36, 0xc6, 0x00, 0x18,
            0xc8, 0x98, 0x0b, 0xc0,
            0xcc, 0x98, 0x81, 0x18,
            0xce, 0x88, 0xce, 0x70,
            0x8f, 0x49, 0xfe, 0xf1,
            0x02, 0xc0, 0x00, 0xb8,
            0xf2, 0x19, 0x40, 0xd3,
            0x20, 0xe4, 0x00, 0xdc,
            0x90, 0x49, 0x1f, 0xf0,
            0x29, 0xc0, 0x01, 0x66,
            0x05, 0x16, 0x3f, 0xf0,
            0x25, 0x16, 0x45, 0xf0,
            0x09, 0x16, 0x23, 0xf0,
            0x16, 0xe0, 0x1a, 0xc2,
            0x40, 0x76, 0xe1, 0x48,
            0x40, 0x9e, 0x17, 0xc2,
            0x00, 0x1e, 0x48, 0x9e,
            0xec, 0xc6, 0x4c, 0x9e,
            0x81, 0x1e, 0x4e, 0x8e,
            0x4e, 0x76, 0xef, 0x49,
            0xfe, 0xf1, 0x0b, 0xc6,
            0x4c, 0x9e, 0x81, 0x1e,
            0x4e, 0x8e, 0x4e, 0x76,
            0xef, 0x49, 0xfe, 0xf1,
            0x90, 0x49, 0x02, 0xc7,
            0x00, 0xbf, 0xe2, 0x27,
            0x24, 0xe4, 0x34, 0xd3,
            0x00, 0xdc, 0x00, 0xdc,
            0x24, 0xe4, 0x80, 0x02,
            0x34, 0xd3, 0xf8, 0xc7,
            0xf9, 0xc2, 0x40, 0x76,
            0xe1, 0x48, 0x40, 0x9e,
            0xf6, 0xc2, 0x00, 0x1e,
            0x48, 0x9e, 0xcb, 0xc6,
            0x4c, 0x9e, 0x81, 0x1e,
            0x4e, 0x8e, 0x4e, 0x76,
            0xef, 0x49, 0xfe, 0xf1,
            0xea, 0xc6, 0x4c, 0x9e,
            0x81, 0x1e, 0x4e, 0x8e,
            0x4e, 0x76, 0xef, 0x49,
            0xfe, 0xf1, 0xdf, 0xe7,
            0x40, 0xd4, 0x00, 0x00,
            0xfe, 0xc2, 0x4c, 0x73,
            0xbf, 0x49, 0xc4, 0xf0,
            0x06, 0x76, 0xfa, 0xc2,
            0x32, 0x40, 0xc0, 0xf0,
            0xde, 0xc6, 0xc0, 0x75,
            0xd1, 0x49, 0xd1, 0xf0,
            0xd7, 0xc0, 0xd7, 0xc6,
            0x0c, 0x9e, 0x00, 0x1e,
            0x08, 0x9e, 0xd4, 0xc6,
            0x0a, 0x9e, 0x8f, 0x1e,
            0x0e, 0x8e, 0x0e, 0x76,
            0xef, 0x49, 0xfe, 0xf1,
            0xc4, 0xe7, 0x1a, 0xc6,
            0xc0, 0x67, 0xf0, 0x49,
            0x13, 0xf0, 0xf0, 0x48,
            0xc0, 0x8f, 0xc2, 0x77,
            0x14, 0xc1, 0x14, 0xc6,
            0x24, 0x9e, 0x22, 0x9f,
            0x8c, 0x1e, 0x26, 0x8e,
            0x26, 0x76, 0xef, 0x49,
            0xfe, 0xf1, 0xfb, 0x49,
            0x05, 0xf0, 0x07, 0xc6,
            0xc0, 0x61, 0x10, 0x48,
            0xc0, 0x89, 0x02, 0xc6,
            0x00, 0xbe, 0x7e, 0x36,
            0x6c, 0xb4, 0x90, 0xcc,
            0x08, 0xdc, 0x10, 0xe8,
            0x1e, 0x89, 0x02, 0xc0,
            0x00, 0xb8, 0xfa, 0x12,
            0x18, 0xc0, 0x00, 0x65,
            0xd1, 0x49, 0x0d, 0xf0,
            0x11, 0xc0, 0x11, 0xc5,
            0x00, 0x1e, 0x08, 0x9e,
            0x0c, 0x9d, 0x0e, 0xc6,
            0x0a, 0x9e, 0x8f, 0x1c,
            0x0e, 0x8c, 0x0e, 0x74,
            0xcf, 0x49, 0xfe, 0xf1,
            0x04, 0xc0, 0x02, 0xc2,
            0x00, 0xba, 0xa0, 0x41,
            0x06, 0xd4, 0x00, 0xdc,
            0x24, 0xe4, 0x80, 0x02,
            0x34, 0xd3, 0x9e, 0x49,
            0x0a, 0xf0, 0x0f, 0xc2,
            0x40, 0x71, 0x9f, 0x49,
            0x02, 0xf1, 0x08, 0xe0,
            0x0b, 0xc2, 0x40, 0x61,
            0x91, 0x48, 0x40, 0x89,
            0x02, 0xc5, 0x00, 0xbd,
            0x82, 0x24, 0x02, 0xc5,
            0x00, 0xbd, 0xf8, 0x23,
            0xfe, 0xcf, 0x1e, 0xd4,
            0xfe, 0xc7, 0xe0, 0x75,
            0x5f, 0x48, 0xe0, 0x9d,
            0x04, 0xc7, 0x02, 0xc5,
            0x00, 0xbd, 0x82, 0x18,
            0x14, 0xd8, 0xc0, 0x88,
            0x5d, 0xc7, 0x56, 0xc6,
            0xe4, 0x9e, 0x0f, 0x1e,
            0xe6, 0x8e, 0xe6, 0x76,
            0xef, 0x49, 0xfe, 0xf1,
            0xe2, 0x75, 0xe0, 0x74,
            0xd8, 0x25, 0xd8, 0x22,
            0xd8, 0x26, 0x48, 0x23,
            0x68, 0x27, 0x48, 0x26,
            0x04, 0xb4, 0x05, 0xb4,
            0x06, 0xb4, 0x45, 0xc6,
            0xe2, 0x23, 0xfe, 0x39,
            0x00, 0x1c, 0x00, 0x1d,
            0x00, 0x13, 0x0c, 0xf0,
            0xb0, 0x49, 0x04, 0xf1,
            0x01, 0x05, 0xb1, 0x25,
            0xfa, 0xe7, 0xb8, 0x33,
            0x35, 0x43, 0x26, 0x31,
            0x01, 0x05, 0xb1, 0x25,
            0xf4, 0xe7, 0x06, 0xb0,
            0x05, 0xb0, 0xae, 0x41,
            0x25, 0x31, 0x30, 0xc5,
            0x6c, 0x41, 0x04, 0xb0,
            0x05, 0xb4, 0x30, 0xc7,
            0x29, 0xc6, 0x04, 0x06,
            0xe4, 0x9e, 0x0f, 0x1e,
            0xe6, 0x8e, 0xe6, 0x76,
            0xef, 0x49, 0xfe, 0xf1,
            0xe0, 0x76, 0xe8, 0x25,
            0xe8, 0x23, 0xf8, 0x27,
            0x1e, 0xc5, 0x6f, 0x41,
            0x33, 0x23, 0xb3, 0x31,
            0x74, 0x41, 0xf5, 0x31,
            0x19, 0xc6, 0x7e, 0x41,
            0x1a, 0xc6, 0xc4, 0x9f,
            0xf1, 0x21, 0xdf, 0x30,
            0x05, 0xb0, 0xc2, 0x9d,
            0x52, 0x22, 0xa3, 0x31,
            0x0e, 0xc7, 0xb7, 0x31,
            0x0e, 0xc7, 0x77, 0x41,
            0x0e, 0xc7, 0xe6, 0x9e,
            0x0b, 0xc3, 0xde, 0x30,
            0x60, 0x64, 0xe8, 0x8c,
            0x02, 0xc4, 0x00, 0xbc,
            0xe8, 0x19, 0x00, 0xc0,
            0x41, 0x00, 0xff, 0x00,
            0x7f, 0x00, 0x00, 0xe6,
            0x60, 0xd3, 0x08, 0xdc,
            0x1b, 0xc4, 0x80, 0x75,
            0x08, 0x15, 0x04, 0xf0,
            0x01, 0x05, 0x80, 0x9d,
            0x0f, 0xe0, 0x00, 0x1d,
            0x80, 0x9d, 0x25, 0xc4,
            0x80, 0x75, 0xd8, 0x22,
            0xdc, 0x26, 0x01, 0x15,
            0x04, 0xf1, 0x0d, 0xc4,
            0x11, 0x1d, 0x80, 0x8d,
            0x14, 0x1e, 0xe5, 0x8e,
            0x04, 0xe0, 0xe5, 0x66,
            0x62, 0x48, 0xe5, 0x8e,
            0x02, 0xc3, 0x00, 0xbb,
            0x8c, 0x06, 0x50, 0xd3,
            0x4c, 0xb4, 0x11, 0xc0,
            0x00, 0x71, 0x98, 0x20,
            0x9c, 0x24, 0x01, 0x11,
            0x06, 0xf1, 0x0a, 0xc6,
            0x01, 0x1d, 0xc6, 0x8d,
            0x19, 0x1d, 0xc1, 0x8d,
            0x04, 0xc0, 0x02, 0xc1,
            0x00, 0xb9, 0xa2, 0x12,
            0xc0, 0xd4, 0x04, 0xe4,
            0xb4, 0xbb, 0xec, 0xc6,
            0x00, 0x1d, 0xc0, 0x8d,
            0xfb, 0xc6, 0x14, 0x1d,
            0xc5, 0x8d, 0x04, 0xc6,
            0x02, 0xc5, 0x00, 0xbd,
            0xd2, 0x03, 0x40, 0xb4
        };
        static uint8_t pla_patch2_b[] = {
            0x10, 0xe0, 0x26, 0xe0,
            0x37, 0xe0, 0x6b, 0xe0,
            0x7e, 0xe0, 0xcb, 0xe0,
            0xcd, 0xe0, 0xcf, 0xe0,
            0xd1, 0xe0, 0xd3, 0xe0,
            0xd5, 0xe0, 0xd7, 0xe0,
            0xd9, 0xe0, 0xdb, 0xe0,
            0xdd, 0xe0, 0xdf, 0xe0,
            0x15, 0xc6, 0xc2, 0x64,
            0xd2, 0x49, 0x06, 0xf1,
            0xc4, 0x48, 0xc5, 0x48,
            0xc6, 0x48, 0xc7, 0x48,
            0x05, 0xe0, 0x44, 0x48,
            0x45, 0x48, 0x46, 0x48,
            0x47, 0x48, 0xc2, 0x8c,
            0xc0, 0x64, 0x46, 0x48,
            0xc0, 0x8c, 0x05, 0xc5,
            0x02, 0xc4, 0x00, 0xbc,
            0x18, 0x02, 0x06, 0xdc,
            0xb0, 0xc0, 0x10, 0xc5,
            0xa0, 0x77, 0xa0, 0x74,
            0x46, 0x48, 0x47, 0x48,
            0xa0, 0x9c, 0x0b, 0xc5,
            0xa0, 0x74, 0x44, 0x48,
            0x43, 0x48, 0xa0, 0x9c,
            0x05, 0xc5, 0xa0, 0x9f,
            0x02, 0xc5, 0x00, 0xbd,
            0x3c, 0x03, 0x1c, 0xe8,
            0x20, 0xe8, 0xd4, 0x49,
            0x04, 0xf1, 0xd5, 0x49,
            0x20, 0xf1, 0x28, 0xe0,
            0x2a, 0xc7, 0xe0, 0x75,
            0xda, 0x49, 0x14, 0xf0,
            0x27, 0xc7, 0xe0, 0x75,
            0xdc, 0x49, 0x10, 0xf1,
            0x24, 0xc7, 0xe0, 0x75,
            0x25, 0xc7, 0xe0, 0x74,
            0x2c, 0x40, 0x0a, 0xfa,
            0x1f, 0xc7, 0xe4, 0x75,
            0xd0, 0x49, 0x09, 0xf1,
            0x1c, 0xc5, 0xe6, 0x9d,
            0x11, 0x1d, 0xe4, 0x8d,
            0x04, 0xe0, 0x16, 0xc7,
            0x00, 0x1d, 0xe4, 0x8d,
            0xe0, 0x8e, 0x11, 0x1d,
            0xe0, 0x8d, 0x07, 0xe0,
            0x0c, 0xc7, 0xe0, 0x75,
            0xda, 0x48, 0xe0, 0x9d,
            0x0b, 0xc7, 0xe4, 0x8e,
            0x02, 0xc4, 0x00, 0xbc,
            0x28, 0x03, 0x02, 0xc4,
            0x00, 0xbc, 0x14, 0x03,
            0x12, 0xe8, 0x4e, 0xe8,
            0x1c, 0xe6, 0x20, 0xe4,
            0x80, 0x02, 0xa4, 0xc0,
            0x12, 0xc2, 0x40, 0x73,
            0xb0, 0x49, 0x08, 0xf0,
            0xb8, 0x49, 0x06, 0xf0,
            0xb8, 0x48, 0x40, 0x9b,
            0x0b, 0xc2, 0x40, 0x76,
            0x05, 0xe0, 0x02, 0x61,
            0x02, 0xc3, 0x00, 0xbb,
            0x0a, 0x0a, 0x02, 0xc3,
            0x00, 0xbb, 0x1a, 0x0a,
            0x98, 0xd3, 0x1e, 0xfc,
            0x1f, 0xe8, 0xfd, 0xc0,
            0x02, 0x62, 0xa0, 0x48,
            0x02, 0x8a, 0x00, 0x72,
            0xa0, 0x49, 0x11, 0xf0,
            0x13, 0xc1, 0x20, 0x62,
            0x2e, 0x21, 0x2f, 0x25,
            0x00, 0x71, 0x9f, 0x24,
            0x0a, 0x40, 0x09, 0xf0,
            0x00, 0x71, 0x18, 0x48,
            0xa0, 0x49, 0x03, 0xf1,
            0x9f, 0x48, 0x02, 0xe0,
            0x1f, 0x48, 0x00, 0x99,
            0x02, 0xc2, 0x00, 0xba,
            0xda, 0x0e, 0x08, 0xe9,
            0x08, 0xea, 0x34, 0xd3,
            0xe8, 0xd4, 0x00, 0xb4,
            0x01, 0xb4, 0x02, 0xb4,
            0xf9, 0xc1, 0x20, 0x62,
            0x2e, 0x21, 0x2f, 0x25,
            0xa0, 0x49, 0x23, 0xf0,
            0xf4, 0xc0, 0xf4, 0xc2,
            0x04, 0x9a, 0x00, 0x1a,
            0x06, 0x8a, 0x06, 0x72,
            0xaf, 0x49, 0xfe, 0xf1,
            0x00, 0x72, 0xa1, 0x49,
            0x18, 0xf0, 0xeb, 0xc2,
            0x04, 0x9a, 0x00, 0x1a,
            0x06, 0x8a, 0x06, 0x72,
            0xaf, 0x49, 0xfe, 0xf1,
            0x00, 0x72, 0xa1, 0x48,
            0x00, 0x9a, 0x81, 0x1a,
            0x06, 0x8a, 0x06, 0x72,
            0xaf, 0x49, 0xfe, 0xf1,
            0x00, 0x72, 0x21, 0x48,
            0x00, 0x9a, 0x81, 0x1a,
            0x06, 0x8a, 0x06, 0x72,
            0xaf, 0x49, 0xfe, 0xf1,
            0x02, 0xb0, 0x01, 0xb0,
            0x00, 0xb0, 0x80, 0xff,
            0x02, 0xc0, 0x00, 0xb8,
            0x3a, 0x4e, 0x02, 0xc0,
            0x00, 0xb8, 0x3a, 0x4e,
            0x02, 0xc0, 0x00, 0xb8,
            0x3a, 0x4e, 0x02, 0xc0,
            0x00, 0xb8, 0x00, 0x00,
            0x02, 0xc0, 0x00, 0xb8,
            0x00, 0x00, 0x02, 0xc0,
            0x00, 0xb8, 0x00, 0x00,
            0x02, 0xc0, 0x00, 0xb8,
            0x00, 0x00, 0x02, 0xc0,
            0x00, 0xb8, 0x00, 0x00,
            0x02, 0xc0, 0x00, 0xb8,
            0x00, 0x00, 0x02, 0xc0,
            0x00, 0xb8, 0x00, 0x00,
            0x02, 0xc0, 0x00, 0xb8,
            0x00, 0x00, 0x00, 0x00
        };
        uint8_t new_ver;

        rtl_fw_ver_erase(tp);

        new_ver = 7;
        if (rtl_check_fw_ver_ok(tp, USB_FW_USB_VER, new_ver)) {
            rtl_clear_bp(tp, MCU_TYPE_USB);

            /* enable fc timer and set timer to 1 second. */
            ocp_write_word(tp, MCU_TYPE_USB, USB_FC_TIMER,
                           CTRL_TIMER_EN | (1000 / 8));

            generic_ocp_write(tp, 0xe600, 0xff,
                              sizeof(usb_patch2_b), usb_patch2_b,
                              MCU_TYPE_USB);

            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_BA, 0xa000);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_0, 0x3d86);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_1, 0x088a);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_2, 0x1dee);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_3, 0x16c2);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_4, 0x1c94);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_5, 0x19f0);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_6, 0x27e0);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_7, 0x35a8);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_8, 0x12f8);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_9, 0x419e);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_10, 0x23f4);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_11, 0x186e);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_12, 0x19e6);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_13, 0x0674);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_14, 0x12a0);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP_15, 0x03d0);
            ocp_write_word(tp, MCU_TYPE_USB, USB_BP2_EN, 0xffff);
            ocp_write_byte(tp, MCU_TYPE_USB, USB_FW_USB_VER,
                           new_ver);
        }

        new_ver = 3;
        if (rtl_check_fw_ver_ok(tp, USB_FW_PLA_VER, new_ver)) {
            rtl_clear_bp(tp, MCU_TYPE_PLA);

            generic_ocp_write(tp, 0xf800, 0xff,
                              sizeof(pla_patch2_b), pla_patch2_b,
                              MCU_TYPE_PLA);

            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_BA, 0x8000);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_0, 0x0216);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_1, 0x0332);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_2, 0x030c);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_3, 0x0a08);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_4, 0x0ec0);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_5, 0x0000);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_6, 0x0000);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_7, 0x0000);
            ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_EN, 0x001e);
            ocp_write_byte(tp, MCU_TYPE_USB, USB_FW_PLA_VER,
                           new_ver);

            ocp_read_byte(tp, MCU_TYPE_USB, USB_MISC_1, &ocp_data);
            if (ocp_data & BND_MASK)
                ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_BP_EN,
                                  BIT(0));
        }

        ocp_word_set_bits(tp, MCU_TYPE_USB, USB_FW_CTRL,
                          FLOW_CTRL_PATCH_OPT);

        ocp_word_set_bits(tp, MCU_TYPE_USB, USB_FW_TASK, FC_PATCH_TASK);

        ocp_word_set_bits(tp, MCU_TYPE_USB, USB_FW_FIX_EN1,
                          FW_IP_RESET_EN);

        rtl_reset_ocp_base(tp);
    }
    rtl_reset_ocp_base(tp);
}

static void r8153b_hw_phy_cfg(struct usbh_rtl8152 *tp)
{
    uint32_t ocp_data;
    uint16_t data;
    int ret;
    USB_LOG_INFO("%s:L%d \r\n", __FUNCTION__, __LINE__);
    ocp_word_test_and_clr_bits(tp, MCU_TYPE_USB, USB_MISC_0, PCUT_STATUS);

    /* disable ALDPS before updating the PHY parameters */
    r8153_aldps_en(tp, false);

    /* disable EEE before updating the PHY parameters */
    rtl_eee_enable(tp, false);

    /* U1/U2/L1 idle timer. 500 us */
    ocp_write_word(tp, MCU_TYPE_USB, USB_U1U2_TIMER, 500);

    ret = r8153_phy_status(tp, 0);
    if (ret < 0)
        return;
    USB_LOG_INFO("%s:L%d ret : %d\r\n", __FUNCTION__, __LINE__, ret);
    switch (ret) {
        case PHY_STAT_PWRDN:
        case PHY_STAT_EXT_INIT:
            r8153b_firmware(tp);
            r8152_mdio_clr_bit(tp, MII_BMCR, BMCR_PDOWN);
            break;
        case PHY_STAT_LAN_ON:
        default:
            r8153b_firmware(tp);
            break;
    }

    r8153b_green_en(tp, test_bit(GREEN_ETHERNET, &tp->flags));

    sram_set_bits(tp, SRAM_GREEN_CFG, R_TUNE_EN);
    ocp_reg_set_bits(tp, OCP_NCTL_CFG, PGA_RETURN_EN);

    /* ADC Bias Calibration:
	 * read efuse offset 0x7d to get a 17-bit data. Remove the dummy/fake
	 * bit (bit3) to rebuild the real 16-bit data. Write the data to the
	 * ADC ioffset.
	 */
    ocp_data = r8152_efuse_read(tp, 0x7d);
    data = (uint16_t)(((ocp_data & 0x1fff0) >> 1) | (ocp_data & 0x7));
    if (data != 0xffff)
        ocp_reg_write(tp, OCP_ADC_IOFFSET, data);

    /* ups mode tx-link-pulse timing adjustment:
	 * rg_saw_cnt = OCP reg 0xC426 Bit[13:0]
	 * swr_cnt_1ms_ini = 16000000 / rg_saw_cnt
	 */
    ret = ocp_reg_read(tp, 0xc426, &data);
    if (ret < 0)
        return;

    ocp_data = data & 0x3fff;
    if (ocp_data) {
        uint32_t swr_cnt_1ms_ini;

        swr_cnt_1ms_ini = (16000000 / ocp_data) & SAW_CNT_1MS_MASK;
        ret = ocp_word_w0w1(tp, MCU_TYPE_USB, USB_UPS_CFG, SAW_CNT_1MS_MASK,
                            swr_cnt_1ms_ini);
        if (ret < 0)
            return;
    }

    ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_PHY_PWR, PFM_PWM_SWITCH);

#ifdef CONFIG_CTAP_SHORT_OFF
    ocp_reg_clr_bits(tp, OCP_EEE_CFG, CTAP_SHORT_EN);

    tp->ups_info.ctap_short_off = true;
#endif
    /* Advnace EEE */
    if (!rtl_phy_patch_request(tp, true, true)) {
        ocp_reg_set_bits(tp, OCP_POWER_CFG, EEE_CLKDIV_EN);
        tp->ups_info.eee_ckdiv = true;

        ocp_reg_set_bits(tp, OCP_DOWN_SPEED,
                         EN_EEE_CMODE | EN_EEE_1000 | EN_10M_CLKDIV);
        tp->ups_info.eee_cmod_lv = true;
        tp->ups_info._10m_ckdiv = true;
        tp->ups_info.eee_plloff_giga = true;

        ocp_reg_write(tp, OCP_SYSCLK_CFG, 0);
        ocp_reg_write(tp, OCP_SYSCLK_CFG, clk_div_expo(5));
        tp->ups_info._250m_ckdiv = true;

        rtl_phy_patch_request(tp, false, true);
    }

    if (tp->eee_en)
        rtl_eee_enable(tp, true);

    r8153_aldps_en(tp, true);
    r8152b_enable_fc(tp);
    //	r8153_u2p3en(tp, true);

    set_bit(PHY_RESET, &tp->flags);
}

static inline void r8153b_rx_agg_chg_indicate(struct usbh_rtl8152 *tp)
{
    ocp_write_byte(tp, MCU_TYPE_USB, USB_UPT_RXDMA_OWN,
                   OWN_UPDATE | OWN_CLEAR);
}

static int r8153_teredo_off(struct usbh_rtl8152 *tp)
{
    int ret = 0;
    uint32_t ocp_data;

    switch (tp->version) {
        case RTL_VER_01:
        case RTL_VER_02:
        case RTL_VER_03:
        case RTL_VER_04:
        case RTL_VER_05:
        case RTL_VER_06:
        case RTL_VER_07:
            ret = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG, &ocp_data);
            if (ret < 0)
                goto out;
            ocp_data &= ~(TEREDO_SEL | TEREDO_RS_EVENT_MASK |
                          OOB_TEREDO_EN);
            ret = ocp_write_word(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG, ocp_data);
            if (ret < 0)
                goto out;
            break;

        case RTL_VER_08:
        case RTL_VER_09:
        case RTL_TEST_01:
        case RTL_VER_10:
        case RTL_VER_11:
        case RTL_VER_12:
        case RTL_VER_13:
        case RTL_VER_14:
        case RTL_VER_15:
        default:
            /* The bit 0 ~ 7 are relative with teredo settings. They are
		 * W1C (write 1 to clear), so set all 1 to disable it.
		 */
            ret = ocp_write_byte(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG, 0xff);
            if (ret < 0)
                goto out;
            break;
    }

    ret = ocp_write_word(tp, MCU_TYPE_PLA, PLA_WDT6_CTRL, WDT6_SET_MODE);
    if (ret < 0)
        goto out;
    ret = ocp_write_word(tp, MCU_TYPE_PLA, PLA_REALWOW_TIMER, 0);
    if (ret < 0)
        goto out;
    ret = ocp_write_dword(tp, MCU_TYPE_PLA, PLA_TEREDO_TIMER, 0);
out:
    return ret;
}

static int r8153b_ups_en(struct usbh_rtl8152 *tp, bool enable)
{
    int ret;
    USB_LOG_INFO("%s:L%d \r\n", __FUNCTION__, __LINE__);
    if (enable) {
        ret = r8153b_ups_flags(tp);

        ret = ocp_byte_set_bits(tp, MCU_TYPE_USB, USB_POWER_CUT,
                                UPS_EN | USP_PREWAKE | PHASE2_EN);
        if (ret < 0)
            goto out;

        ret = ocp_byte_set_bits(tp, MCU_TYPE_USB, USB_MISC_2,
                                UPS_FORCE_PWR_DOWN);
    } else {
        uint32_t ocp_data;

        ret = ocp_byte_clr_bits(tp, MCU_TYPE_USB, USB_POWER_CUT,
                                UPS_EN | USP_PREWAKE);
        if (ret < 0)
            goto out;

        ret = ocp_byte_clr_bits(tp, MCU_TYPE_USB, USB_MISC_2,
                                UPS_FORCE_PWR_DOWN | UPS_NO_UPS);
        if (ret < 0)
            goto out;

        ret = ocp_read_word(tp, MCU_TYPE_USB, USB_MISC_0, &ocp_data);
        if (ret < 0)
            goto out;
        USB_LOG_INFO("%s:L%d ocp_data:%d\r\n", __FUNCTION__, __LINE__, ocp_data);
        if (ocp_data & PCUT_STATUS) {
            int i;

            for (i = 0; i < 500; i++) {
                ret = ocp_read_word(tp, MCU_TYPE_PLA,
                                    PLA_BOOT_CTRL, &ocp_data);
                if (ret < 0)
                    goto out;
                if (ocp_data & AUTOLOAD_DONE)
                    break;
                usb_osal_msleep(20);
            }

            tp->rtl_ops.hw_phy_cfg(tp);

            ret = rtl8152_set_speed(tp, AUTONEG_ENABLE, tp->supports_gmii ? SPEED_1000 : SPEED_100, DUPLEX_FULL);
        }
    }

out:
    return ret;
}

static int r8153b_init(struct usbh_rtl8152 *tp)
{
    uint32_t ocp_data;
    uint16_t data;
    int i = 0, ret = 0;
    USB_LOG_INFO("%s:L%d \r\n", __FUNCTION__, __LINE__);
    ret = r8153b_u1u2en(tp, false);
    if (ret < 0)
        goto out;

    for (i = 0; i < 500; i++) {
        ret = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BOOT_CTRL, &ocp_data);

        if (ret < 0)
            goto out;
        if (ocp_data & AUTOLOAD_DONE)
            break;

        usb_osal_msleep(20);
    }

    ret = r8153_phy_status(tp, 0);
    if (ret < 0)
        goto out;

    data = r8152_mdio_read(tp, MII_BMCR);
    if (data & BMCR_PDOWN) {
        data &= ~BMCR_PDOWN;
        r8152_mdio_write(tp, MII_BMCR, data);
    }

    ret = r8153_phy_status(tp, PHY_STAT_LAN_ON);
    if (ret < 0)
        goto out;

    ret = r8153_u2p3en(tp, false);
    if (ret < 0)
        goto out;

    /* MSC timer = 0xfff * 8ms = 32760 ms */
    ret = ocp_write_word(tp, MCU_TYPE_USB, USB_MSC_TIMER, 0x0fff);
    if (ret < 0)
        goto out;

    r8152_power_cut_en(tp, false);

    ret = r8153b_ups_en(tp, false);
    if (ret < 0)
        goto out;

    ret = r8153_queue_wake(tp, false);
    if (ret < 0)
        goto out;

    ret = rtl_runtime_suspend_enable(tp, false);
    if (ret < 0)
        goto out;

    if (rtl8152_get_speed(tp) & LINK_STATUS)
        ret = ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS,
                                CUR_LINK_OK | POLL_LINK_CHG);
    else
        ret = ocp_word_w0w1(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS,
                            CUR_LINK_OK, POLL_LINK_CHG);
    if (ret < 0)
        goto out;

    ret = r8153_lanwake_clr_en(tp, true);
    if (ret < 0)
        goto out;

    /* MAC clock speed down */
    ret = r8153_mac_clk_speed_down(tp, false);
    if (ret < 0)
        goto out;

    ret = r8153b_mcu_spdown_en(tp, false);
    if (ret < 0)
        goto out;

    if (tp->version == RTL_VER_09) {
        uint8_t ocp_data8;
        /* Disable Test IO for 32QFN */
        ret = ocp_read_byte(tp, MCU_TYPE_PLA, 0xdc00, &ocp_data8);
        if (ret < 0)
            goto out;
        if (ocp_data8 & BIT(5)) {
            USB_LOG_INFO("%s:L%d \r\n", __FUNCTION__, __LINE__);
            ret = test_io_en(tp, false);
            if (ret < 0)
                goto out;
        }
    }

    /* enable rx aggregation */
    ret = ocp_word_clr_bits(tp, MCU_TYPE_USB, USB_USB_CTRL,
                            RX_AGG_DISABLE | RX_ZERO_EN);
    if (ret < 0)
        goto out;

    ret = rtl_tally_reset(tp);
    if (ret < 0)
        goto out;

    return 0;
out:
    USB_LOG_ERR("%s:L%d, ret:%d \r\n", __FUNCTION__, __LINE__, ret);
    return (ret < 0) ? ret : 0;
}

static int rtl8153_change_mtu(struct usbh_rtl8152 *tp)
{
    int ret;

    ret = ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, 9 * 1024);
    if (ret < 0)
        goto out;

    ret = ocp_write_byte(tp, MCU_TYPE_PLA, PLA_MTPS, MTPS_JUMBO);

out:
    return ret;
}

static int r8153_first_init(struct usbh_rtl8152 *tp)
{
    int ret;

    ret = rxdy_gated_en(tp, true);
    if (ret < 0)
        goto out;
    ret = r8153_teredo_off(tp);
    if (ret < 0)
        goto out;

    ret = ocp_dword_clr_bits(tp, MCU_TYPE_PLA, PLA_RCR, RCR_ACPT_ALL);
    if (ret < 0)
        goto out;

    ret = rtl8152_nic_reset(tp);
    if (ret < 0)
        goto out;
    ret = rtl_reset_bmu(tp);
    if (ret < 0)
        goto out;

    ret = ocp_byte_clr_bits(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, NOW_IS_OOB);
    if (ret < 0)
        goto out;

    ret = ocp_word_clr_bits(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, MCU_BORW_EN);
    if (ret < 0)
        goto out;

    wait_oob_link_list_ready(tp);

    ret = ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, RE_INIT_LL);
    if (ret < 0)
        goto out;

    wait_oob_link_list_ready(tp);

    // ret = rtl_rx_vlan_en(tp, tp->netdev->features & NETIF_F_HW_VLAN_CTAG_RX);
    // if (ret < 0)
    // 	goto out;

    ret = rtl8153_change_mtu(tp);
    if (ret < 0)
        goto out;

    ret = ocp_word_set_bits(tp, MCU_TYPE_PLA, PLA_TCR0, TCR0_AUTO_FIFO);
    if (ret < 0)
        goto out;

    ret = rtl8152_nic_reset(tp);
    if (ret < 0)
        goto out;

    /* rx share fifo credit full threshold */
    ret = ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_NORMAL);
    if (ret < 0)
        goto out;
    ret = ocp_write_word(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1, RXFIFO_THR2_NORMAL);
    if (ret < 0)
        goto out;
    ret = ocp_write_word(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2, RXFIFO_THR3_NORMAL);
    if (ret < 0)
        goto out;
    /* TX share fifo free credit full threshold */
    ret = ocp_write_dword(tp, MCU_TYPE_PLA, PLA_TXFIFO_CTRL, TXFIFO_THR_NORMAL2);
    if (ret < 0)
        goto out;

    ret = r8153_lanwake_clr_en(tp, true);

out:
    return ret;
}

static int rtl8153b_up(struct usbh_rtl8152 *tp)
{
    int ret;
    USB_LOG_INFO("%s:L%d \r\n", __FUNCTION__, __LINE__);
    if (test_bit(RTL8152_UNPLUG, &tp->flags))
        return -1;
    USB_LOG_INFO("%s:L%d \r\n", __FUNCTION__, __LINE__);
    ret = r8153b_u1u2en(tp, false);
    if (ret < 0)
        goto out;
    ret = r8153_u2p3en(tp, false);
    if (ret < 0)
        goto out;
    ret = r8153_aldps_en(tp, false);
    if (ret < 0)
        goto out;

    ret = r8153_first_init(tp);
    if (ret < 0)
        goto out;
    ret = ocp_write_dword(tp, MCU_TYPE_USB, USB_RX_BUF_TH, RX_THR_B);
    if (ret < 0)
        goto out;

    ret = r8153b_mcu_spdown_en(tp, false);
    if (ret < 0)
        goto out;
    ret = r8153_aldps_en(tp, true);
    if (ret < 0)
        goto out;
    //	ret = r8153_u2p3en(tp, true);
    //	if (ret < 0)
    //		goto out;
    //	if (tp->udev->speed >= USB_SPEED_SUPER)
    //		ret = r8153b_u1u2en(tp, true);
    return ret;
out:
    USB_LOG_ERR("%s:L%d ret:%d\r\n", __FUNCTION__, __LINE__, ret);
    return ret;
}
