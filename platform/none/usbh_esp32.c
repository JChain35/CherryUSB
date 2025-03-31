#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif_net_stack.h"
#include "lwip/esp_netif_net_stack.h"
#include "lwip/esp_pbuf_ref.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/ethip6.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "netif/etharp.h"
#include "usbh_core.h"
#include "usbh_hub.h"
#include "usbh_rtl8152.h"

#if LWIP_DHCP
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#endif

#include "usbh_core.h"

#if LWIP_TCPIP_CORE_LOCKING_INPUT != 1
#warning suggest you to set LWIP_TCPIP_CORE_LOCKING_INPUT to 1, usb handles eth input with own thread
#endif

#if LWIP_TCPIP_CORE_LOCKING != 1
#error must set LWIP_TCPIP_CORE_LOCKING to 1
#endif

#if PBUF_POOL_BUFSIZE < 1600
#error PBUF_POOL_BUFSIZE must be larger than 1600
#endif

#if TCPIP_THREAD_STACKSIZE < 1024
#error TCPIP_THREAD_STACKSIZE must be >= 1024
#endif

#define TAG "USBH_ETH"
#define USBH_IFNAME0 'u'
#define USBH_IFNAME1 'e'

static esp_netif_t *usbh_netif = NULL;

static err_t usbh_eth_low_level_output(struct netif *netif, struct pbuf *p) {
    
    if( netif == NULL ) {
        ESP_LOGE(TAG, "%s:L%d, netif is NULL", __func__, __LINE__);
        return ERR_IF;
    }

    struct pbuf *q = p;
    esp_netif_t *esp_netif = esp_netif_get_handle_from_netif_impl(netif);
    esp_err_t ret = ESP_FAIL;

    if (!esp_netif) {
        ESP_LOGE(TAG, "corresponding esp-netif is NULL: netif=%p pbuf=%p len=%d\n", netif, p, p->len);
        return ERR_IF;
    }
    if( p == NULL ) {
        ESP_LOGE(TAG, "%s:L%d, pbuf is NULL", __func__, __LINE__);
        return ERR_MEM;
    }

    if (q->next == NULL) {
        ret = esp_netif_transmit(esp_netif, q->payload, q->len);
    } 
    else {
        ESP_LOGE(TAG, "low_level_output: pbuf is a list, application may has bug");
        q = pbuf_alloc(PBUF_RAW_TX, p->tot_len, PBUF_RAM);
        if (q != NULL) {
            pbuf_copy(q, p);
        } else {
            return ERR_MEM;
        }
        ret = esp_netif_transmit(esp_netif, q->payload, q->len);
        /* content in payload has been copied to DMA buffer, it's safe to free pbuf now */
        pbuf_free(q);
    }
    /* Check error */
    if (likely(ret == ESP_OK)) {
        return ERR_OK;
    }
    if (ret == ESP_ERR_NO_MEM) {
        return ERR_MEM;
    }
    return ERR_IF;
}

static esp_netif_recv_ret_t usbh_eth_lwip_input(void *h, void *buffer, size_t len, void *l2_buff) {
    (void)l2_buff;
    if( h == NULL ) {
        ESP_LOGE(TAG, "%s:L%d, netif is NULL", __func__, __LINE__);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }
    struct netif *netif = h;
    esp_netif_t *esp_netif = esp_netif_get_handle_from_netif_impl(netif);
    struct pbuf *p;

    if (unlikely(buffer == NULL || !netif_is_up(netif))) {
        if (buffer) {
            esp_netif_free_rx_buffer(esp_netif, buffer);
        }
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }

    if(len == 0) {
        ESP_LOGE(TAG, "%s:L%d, len is 0", __func__, __LINE__);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }

    /* allocate custom pbuf to hold  */
    p = esp_pbuf_allocate(esp_netif, buffer, len, buffer);
    if (p == NULL) {
        esp_netif_free_rx_buffer(esp_netif, buffer);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_ERR_NO_MEM);
    }
    /* full packet send to tcpip_thread to process */
    if (unlikely(netif->input(p, netif) != ERR_OK)) {
        ESP_LOGE(TAG, "ethernetif_input: IP input error\n");
        pbuf_free(p);
        return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_FAIL);
    }
    /* the pbuf will be free in upper layer, eg: ethernet_input */
    return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_OK);
}

static err_t usbh_eth_lwip_init(struct netif *netif) {
    if( netif == NULL ) {
        ESP_LOGE(TAG, "%s:L%d, netif is NULL", __func__, __LINE__);
        return ERR_IF;
    }
    /* Initialize interface hostname */

    /* set MAC hardware address length */
    netif->hwaddr_len = ETH_HWADDR_LEN;
    /* maximum transfer unit */
    netif->mtu = 1500;
    /* device capabilities */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    netif->name[0] = USBH_IFNAME0;
    netif->name[1] = USBH_IFNAME1;
    
    netif->output = etharp_output;
    netif->linkoutput = usbh_eth_low_level_output;

    return ERR_OK;
}

// Callback to pass data from esp netif stack to usb stack
static esp_err_t netif_transmit(void *h, void *buffer, size_t len) {
    int ret = 0;
    if( buffer == NULL || len == 0 ) {
        ESP_LOGE(TAG, "%s:L%d, buffer is NULL or len is 0", __func__, __LINE__);
        return ESP_ERR_INVALID_ARG;
    }
    ret = usbh_rtl8152_eth_tx(buffer, len);
    if (ret != 0 ) {
        if( ret == -USB_ERR_NOTCONN ) {
            ESP_LOGW(TAG, "Wait USB Ethernet Link UP, %d", ret);
        } else {
            ESP_LOGE(TAG, "Failed to tx, %d", ret);
        }
        return ret;
    }
    return ESP_OK;
}

// Clear buffer after tx 
static void rx_buffer_free(void *h, void *buffer) {
    if( buffer != NULL ) {
        free(buffer);
    }
}

// Callback to pass data from esp netif stack to usb stack
static esp_err_t netif_receive(void *h, void *buffer, size_t len) {
    if (usbh_netif)
    {
        void *buf_copy = malloc(len);
        if (!buf_copy)
        {
            return ESP_ERR_NO_MEM;
        }
        memcpy(buf_copy, buffer, len);
        return esp_netif_receive(usbh_netif, buf_copy, len, NULL);
    }
    return ESP_OK;
}

esp_netif_t *esp_usbh_eth_netif_create(void) {
    esp_netif_inherent_config_t base_cfg = {
        .flags = (esp_netif_flags_t)(ESP_NETIF_DEFAULT_ARP_FLAGS | ESP_NETIF_FLAG_EVENT_IP_MODIFIED | ESP_NETIF_FLAG_AUTOUP),
        ESP_COMPILER_DESIGNATED_INIT_AGGREGATE_TYPE_EMPTY(mac)
        ESP_COMPILER_DESIGNATED_INIT_AGGREGATE_TYPE_EMPTY(ip_info)
        .get_ip_event = IP_EVENT_ETH_GOT_IP,
        .lost_ip_event = IP_EVENT_ETH_LOST_IP,
        .if_key = "USBH_ETH",
        .if_desc = "usbh",
        .route_prio = 50,
        .bridge_info = NULL
    };
    
    esp_netif_driver_ifconfig_t driver_cfg = {
        .handle = (void *)1,             // not using an instance, USB-NCM is a static singleton (must be != NULL)
        .transmit = netif_transmit,      // point to static Tx function
        .driver_free_rx_buffer = rx_buffer_free // point to Free Rx buffer function
    };

    struct esp_netif_netstack_config lwip_netif_config = {
        .lwip = {
            .init_fn = usbh_eth_lwip_init,
            .input_fn = usbh_eth_lwip_input
        }
    };

    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = &driver_cfg,
        .stack = &lwip_netif_config
    };

    usbh_netif = esp_netif_new(&cfg);
    if( usbh_netif == NULL ) {
        ESP_LOGE(TAG, "%s:L%d, Failed to create netif", __func__, __LINE__);
        return NULL;
    }

    return usbh_netif;
}

#ifdef CONFIG_USBHOST_PLATFORM_RTL8152
void usbh_rtl8152_eth_input(uint8_t *buf, uint32_t buflen) {
    netif_receive(NULL, buf, buflen);
}

void usbh_rtl8152_run(struct usbh_rtl8152 *rtl8152_class) {
    esp_err_t ret = ESP_OK;

    do {
        usbh_netif = esp_usbh_eth_netif_create();
        if( usbh_netif == NULL ) {
            ESP_LOGE(TAG, "%s:L%d, Failed to create netif", __func__, __LINE__);
            break;
        }
        ret = esp_netif_set_mac( usbh_netif, rtl8152_class->mac);
        if( ret != ESP_OK ) {
            ESP_LOGE(TAG, "%s:L%d, Failed to set MAC", __func__, __LINE__);
            break;
        }
        
        esp_netif_action_start(usbh_netif, 0, 0, 0);

        while (!esp_netif_is_netif_up(usbh_netif)) {
            ESP_LOGW(TAG, "Waiting for netif to be up...");
            usb_osal_msleep(1000);
        }
        ESP_LOGI(TAG, "Ethernet Link Up");

        usb_osal_thread_create("usbh_rtl8152_rx", 2048*5, CONFIG_USBHOST_PSC_PRIO + 1, usbh_rtl8152_rx_thread, NULL);

        ret = esp_netif_dhcpc_start(usbh_netif);
        if( ret != ESP_OK ) {
            ESP_LOGE(TAG, "%s:L%d, Failed to start DHCP", __func__, __LINE__);
            break;
        }
    } while(0);
}

void usbh_rtl8152_stop(struct usbh_rtl8152 *rtl8152_class)
{
    (void)rtl8152_class;
    esp_netif_destroy(usbh_netif);
}
#endif
