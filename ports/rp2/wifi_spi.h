#include <stdint.h>
#define ESP8285_SPI_DEBUG                 (3)
static void delete_esp8285_spi_params(void *arg);

typedef enum
{
    SET_NET_CMD                 = (0x10),
    SET_PASSPHRASE_CMD          = (0x11),
    GET_CONN_STATUS_CMD         = (0x20),
    GET_IPADDR_CMD              = (0x21),
    GET_MACADDR_CMD             = (0x22),
    GET_CURR_SSID_CMD           = (0x23),
    GET_CURR_RSSI_CMD           = (0x25),
    GET_CURR_ENCT_CMD           = (0x26),
    SCAN_NETWORKS               = (0x27),
    GET_SOCKET_CMD              = (0x3F),
    GET_STATE_TCP_CMD           = (0x29),
    DATA_SENT_TCP_CMD           = (0x2A),
    AVAIL_DATA_TCP_CMD          = (0x2B),
    GET_DATA_TCP_CMD            = (0x2C),
    START_CLIENT_TCP_CMD        = (0x2D),
    STOP_CLIENT_TCP_CMD         = (0x2E),
    GET_CLIENT_STATE_TCP_CMD    = (0x2F),
    DISCONNECT_CMD              = (0x30),
    GET_IDX_RSSI_CMD            = (0x32),
    GET_IDX_ENCT_CMD            = (0x33),
    REQ_HOST_BY_NAME_CMD        = (0x34),
    GET_HOST_BY_NAME_CMD        = (0x35),
    START_SCAN_NETWORKS         = (0x36),
    GET_FW_VERSION_CMD          = (0x37),
    SEND_UDP_DATA_CMD           = (0x39), // START_CLIENT_TCP_CMD set ip,port, then ADD_UDP_DATA_CMD to add data then SEND_UDP_DATA_CMD to call sendto
    GET_REMOTE_INFO_CMD         = (0x3A),
    PING_CMD                    = (0x3E),
    SEND_DATA_TCP_CMD           = (0x44),
    GET_DATABUF_TCP_CMD         = (0x45),
    ADD_UDP_DATA_CMD            = (0x46),
    GET_ADC_VAL_CMD             = (0x53),
    SOFT_RESET_CMD              = (0x54),
    START_CMD                   = (0xE0),
    END_CMD                     = (0xEE),
    ERR_CMD                     = (0xEF)
}esp8285_cmd_enum_t;

typedef enum {
    CMD_FLAG                    = (0),
    REPLY_FLAG                  = (1<<7)
}esp8285_flag_t;

typedef void (*esp8285_spi_params_del)(void *arg);

typedef struct
{
    uint32_t param_len;
    uint8_t *param;
} esp8285_spi_param_t;

typedef struct
{
    uint32_t params_num;
    esp8285_spi_param_t **params;
    esp8285_spi_params_del del;
} esp8285_spi_params_t;

typedef void (*esp8285_spi_aps_list_del)(void *arg);

typedef struct
{
    int8_t rssi;
    uint8_t encr;
    uint8_t ssid[33];
} esp8285_spi_ap_t;

typedef struct
{
    uint32_t aps_num;
    esp8285_spi_ap_t **aps;
    esp8285_spi_aps_list_del del;
} esp8285_spi_aps_list_t;