/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef USBH_RTL8152_H
#define USBH_RTL8152_H

struct usbh_rtl8152 {
    struct usbh_hubport *hport;
    struct usb_endpoint_descriptor *bulkin;  /* Bulk IN endpoint */
    struct usb_endpoint_descriptor *bulkout; /* Bulk OUT endpoint */
    struct usb_endpoint_descriptor *intin;   /* INTR IN endpoint  */
    struct usbh_urb bulkout_urb;
    struct usbh_urb bulkin_urb;
    struct usbh_urb intin_urb;

    uint8_t intf;

    uint8_t mac[6];
    bool connect_status;
    uint32_t speed[2];

    uint8_t version;
    uint8_t eee_adv;
    uint8_t eee_en;
    uint8_t supports_gmii;

    unsigned long flags;
    struct ups_info {
        uint32_t r_tune          : 1;
        uint32_t _10m_ckdiv      : 1;
        uint32_t _250m_ckdiv     : 1;
        uint32_t aldps           : 1;
        uint32_t lite_mode       : 2;
        uint32_t speed_duplex    : 4;
        uint32_t eee             : 1;
        uint32_t eee_lite        : 1;
        uint32_t eee_ckdiv       : 1;
        uint32_t eee_plloff_100  : 1;
        uint32_t eee_plloff_giga : 1;
        uint32_t eee_cmod_lv     : 1;
        uint32_t green           : 1;
        uint32_t flow_control    : 1;
        uint32_t ctap_short_off  : 1;
    } ups_info;

    uint16_t min_mtu;
    uint16_t max_mtu;
    uint16_t ocp_base;
    uint32_t saved_wolopts;
    uint32_t rx_buf_sz;

    struct rtl_ops {
        int (*init)(struct usbh_rtl8152 *tp);
        int (*enable)(struct usbh_rtl8152 *tp);
        void (*disable)(struct usbh_rtl8152 *tp);
        int (*up)(struct usbh_rtl8152 *tp);
        void (*down)(struct usbh_rtl8152 *tp);
        void (*unload)(struct usbh_rtl8152 *tp);
        bool (*in_nway)(struct usbh_rtl8152 *tp);
        void (*hw_phy_cfg)(struct usbh_rtl8152 *tp);
        void (*autosuspend_en)(struct usbh_rtl8152 *tp, bool enable);
        void (*change_mtu)(struct usbh_rtl8152 *tp);
    } rtl_ops;

    void *user_data;
};

#ifdef __cplusplus
extern "C" {
#endif

int usbh_rtl8152_get_connect_status(struct usbh_rtl8152 *rtl8152_class);

void usbh_rtl8152_run(struct usbh_rtl8152 *rtl8152_class);
void usbh_rtl8152_stop(struct usbh_rtl8152 *rtl8152_class);

uint8_t *usbh_rtl8152_get_eth_txbuf(void);
int usbh_rtl8152_eth_output(uint32_t buflen);
void usbh_rtl8152_eth_input(uint8_t *buf, uint32_t buflen);

int usbh_rtl8152_eth_tx(uint8_t *buf, uint32_t buflen);

void usbh_rtl8152_rx_thread(CONFIG_USB_OSAL_THREAD_SET_ARGV);

#ifdef __cplusplus
}
#endif

#endif /* USBH_RTL8152_H */
