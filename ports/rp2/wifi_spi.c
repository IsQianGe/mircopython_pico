#include <stdlib.h>

#include "wifi_spi.h"
#include "wifi_spi_io.h"
#include "mphalport.h"
//#include "gpiohs.h"
//#include "sleep.h"
//#include "sysctl.h"
//#include "fpioa.h"
//#include "printf.h"
//#include "errno.h"
//Wait until the ready pin goes low
// 0 get response
// -1 error, no response

static void esp8285_spi_reset(void);
static int8_t esp8285_spi_send_command(uint8_t cmd, esp8285_spi_params_t *params, uint8_t param_len_16);

void esp8285_spi_init(void)
{
    //cs
    gpio_set_dir(cs_num, GPIO_OUT);
    gpio_put(cs_num, 1);

    //ready
    gpio_set_dir(rdy_num, GPIO_IN); //ready

    if ((int8_t)rst_num > 0)
    {
        rst_num -= 0;
        gpio_set_dir(rst_num, GPIO_OUT); //reset
    }

#if ESP32_HAVE_IO0
    gpio_set_dir(ESP32_SPI_IO0_HS_NUM, GPIO_IN); //gpio0
#endif

    esp8285_spi_reset();
}

//Hard reset the ESP32 using the reset pin
static void esp8285_spi_reset(void)
{
#if ESP8285_SPI_DEBUG
    mp_printf(MP_PYTHON_PRINTER,"Reset ESP32\r\n");
#endif

#if ESP32_HAVE_IO0
    gpio_set_dir(ESP32_SPI_IO0_HS_NUM, GPIO_OUT); //gpio0
    gpio_put(ESP32_SPI_IO0_HS_NUM, 1);
#endif

    //here we sleep 1s
    gpio_put(cs_num, 1);

    if ((int8_t)rst_num > 0)
    {
        gpio_put(rst_num, 0);
        sleep_ms(500);
        gpio_put(rst_num, 1);
        sleep_ms(800);
    }
    else
    {
        //soft reset
        esp8285_spi_send_command(SOFT_RESET_CMD, NULL, 0);
        sleep_ms(1500);
    }

#if ESP32_HAVE_IO0
    gpio_set_dir(ESP32_SPI_IO0_HS_NUM, GPIO_IN); //gpio0
#endif
}

int8_t esp8285_spi_wait_for_ready(void)
{
#if (ESP8285_SPI_DEBUG >= 3)
    mp_printf(MP_PYTHON_PRINTER,"Wait for ESP32 ready\r\n");
#endif

    uint64_t tm = time_us_32();
    while ((time_us_32() - tm) < 10 * 1000 * 1000) //10s
    {
        if (gpio_get(rdy_num) == 0)
            return 0;

#if (ESP8285_SPI_DEBUG >= 3)
        mp_printf(MP_PYTHON_PRINTER,".");
#endif
        sleep_ms(1); //FIXME
    }

#if (ESP8285_SPI_DEBUG >= 3)
    mp_printf(MP_PYTHON_PRINTER,"esp8285 not responding\r\n");
#endif

    return -1;
}

#define lc_buf_len 256
uint8_t lc_send_buf[lc_buf_len];
uint8_t lc_buf_flag = 0;

/// Send over a command with a list of parameters
// -1 error
// other right
static int8_t esp8285_spi_send_command(uint8_t cmd, esp8285_spi_params_t *params, uint8_t param_len_16)
{
    uint32_t packet_len = 0;

    packet_len = 4; // header + end byte
    if (params != NULL)
    {
        for (uint32_t i = 0; i < params->params_num; i++)
        {
            packet_len += params->params[i]->param_len;
            packet_len += 1; // size byte
            if (param_len_16)
                packet_len += 1;
        }
    }
    while (packet_len % 4 != 0)
        packet_len += 1;

    uint8_t *sendbuf = NULL;

    if (packet_len > lc_buf_len)
    {
        sendbuf = (uint8_t *)malloc(sizeof(uint8_t) * packet_len);
        lc_buf_flag = 0;
        if (!sendbuf)
        {
#if (ESP8285_SPI_DEBUG)
            mp_printf(MP_PYTHON_PRINTER,"%s: malloc error\r\n", __func__);
#endif
            return -1;
        }
    }
    else
    {
        sendbuf = lc_send_buf;
        lc_buf_flag = 1;
    }

    sendbuf[0] = START_CMD;
    sendbuf[1] = cmd & ~REPLY_FLAG;
    if (params != NULL)
        sendbuf[2] = params->params_num;
    else
        sendbuf[2] = 0;

    uint32_t ptr = 3;

    if (params != NULL)
    {
        //handle parameters here
        for (uint32_t i = 0; i < params->params_num; i++)
        {
#if (ESP8285_SPI_DEBUG >= 2)
            mp_printf(MP_PYTHON_PRINTER,"\tSending param #%d is %d bytes long\r\n", i, params->params[i]->param_len);
#endif

            if (param_len_16)
            {
                sendbuf[ptr] = (uint8_t)((params->params[i]->param_len >> 8) & 0xFF);
                ptr += 1;
            }
            sendbuf[ptr] = (uint8_t)(params->params[i]->param_len & 0xFF);
            ptr += 1;
            memcpy(sendbuf + ptr, params->params[i]->param, params->params[i]->param_len);
            ptr += params->params[i]->param_len;
        }
    }
    sendbuf[ptr] = END_CMD;

    esp8285_spi_wait_for_ready();
    gpio_put(cs_num, 0);

    uint64_t tm = time_us_32();
    while ((time_us_32() - tm) < 1000 * 1000)
    {
        if (gpio_get(rdy_num))
            break;
        sleep_ms(1);
    }

    if ((time_us_32() - tm) > 1000 * 1000)
    {
#if (ESP8285_SPI_DEBUG)
        mp_printf(MP_PYTHON_PRINTER,"ESP32 timed out on SPI select\r\n");
#endif
        gpio_put(cs_num, 1);
        if (lc_buf_flag == 0)
        {
            free(sendbuf);
            sendbuf = NULL;
        }
        else
        {
            memset(sendbuf, 0, packet_len);
        }
        return -1;
    }

    soft_spi_rw_len(sendbuf, NULL, packet_len);
    gpio_put(cs_num, 1);

#if (ESP8285_SPI_DEBUG >= 3)
    if (packet_len < 100)
    {
        mp_printf(MP_PYTHON_PRINTER,"Wrote buf packet_len --> %d: ", packet_len);
        for (uint32_t i = 0; i < packet_len; i++)
            mp_printf(MP_PYTHON_PRINTER,"%02x ", sendbuf[i]);
        mp_printf(MP_PYTHON_PRINTER,"\r\n");
    }
#endif
    if (lc_buf_flag == 0)
    {
        free(sendbuf);
        sendbuf = NULL;
    }
    else
    {
        memset(sendbuf, 0, packet_len);
    }
    return 0;
}
uint8_t esp8285_spi_read_byte(void)
{
    uint8_t read = 0x0;

    read = soft_spi_rw(0xff);

#if (ESP8285_SPI_DEBUG >= 3)
    mp_printf(MP_PYTHON_PRINTER,"\t\tRead:%02x\r\n", read);
#endif

    return read;
}

///Read many bytes from SPI
void esp8285_spi_read_bytes(uint8_t *buffer, uint32_t len)
{
    soft_spi_rw_len(NULL, buffer, len);

#if (ESP8285_SPI_DEBUG >= 3)
    if (len < 100)
    {
        mp_printf(MP_PYTHON_PRINTER,"\t\tRead:");
        for (uint32_t i = 0; i < len; i++)
            mp_printf(MP_PYTHON_PRINTER,"%02x ", *(buffer + i));
        mp_printf(MP_PYTHON_PRINTER,"\r\n");
    }
#endif
}

///Read a byte with a time-out, and if we get it, check that its what we expect
//0 succ
//-1 error
int8_t esp8285_spi_wait_spi_char(uint8_t want)
{
    uint8_t read = 0x0;
    uint64_t tm = time_us_32();

    while ((time_us_32() - tm) < 100 * 1000)
    {
        read = esp8285_spi_read_byte();

        if (read == ERR_CMD)
        {
#if ESP8285_SPI_DEBUG
            mp_printf(MP_PYTHON_PRINTER,"Error response to command\r\n");
#endif
            return -1;
        }
        else if (read == want)
            return 0;
    }

#if 0
    if ((time_us_32() - tm) > 100 * 1000)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"Timed out waiting for SPI char\r\n");
#endif
        return -1;
    }
#endif

    return -1;
}

///Read a byte and verify its the value we want
//0 right
//-1 error
uint8_t esp8285_spi_check_data(uint8_t want)
{
    uint8_t read = esp8285_spi_read_byte();

    if (read != want)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"Expected %02X but got %02X\r\n", want, read);
#endif
        return -1;
    }
    return 0;
}
esp8285_spi_params_t *esp8285_spi_wait_response_cmd(uint8_t cmd, uint32_t *num_responses, uint8_t param_len_16)
{
    uint32_t num_of_resp = 0;

    esp8285_spi_wait_for_ready();

    gpio_put(cs_num, 0);

    uint64_t tm = time_us_32();
    while ((time_us_32() - tm) < 1000 * 1000)
    {
        if (gpio_get(rdy_num))
            break;
        sleep_ms(1);
    }

    if ((time_us_32() - tm) > 1000 * 1000)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"ESP32 timed out on SPI select\r\n");
#endif
        gpio_put(cs_num, 1);
        return NULL;
    }

    if (esp8285_spi_wait_spi_char(START_CMD) != 0)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"esp8285_spi_wait_spi_char START_CMD error\r\n");
#endif
        gpio_put(cs_num, 1);
        return NULL;
    }

    if (esp8285_spi_check_data(cmd | REPLY_FLAG) != 0)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"esp8285_spi_check_data cmd | REPLY_FLAG error\r\n");
#endif
        gpio_put(cs_num, 1);
        return NULL;
    }

    if (num_responses)
    {
        if (esp8285_spi_check_data(*num_responses) != 0)
        {
#if ESP8285_SPI_DEBUG
            mp_printf(MP_PYTHON_PRINTER,"esp8285_spi_check_data num_responses error\r\n");
#endif
            gpio_put(cs_num, 1);
            return NULL;
        }
        num_of_resp = *num_responses;
    }
    else
    {
        num_of_resp = esp8285_spi_read_byte();
    }

    esp8285_spi_params_t *params_ret = (esp8285_spi_params_t *)malloc(sizeof(esp8285_spi_params_t));

    params_ret->del = delete_esp8285_spi_params;
    params_ret->params_num = num_of_resp;
    params_ret->params = (void *)malloc(sizeof(void *) * num_of_resp);

    for (uint32_t i = 0; i < num_of_resp; i++)
    {
        params_ret->params[i] = (esp8285_spi_param_t *)malloc(sizeof(esp8285_spi_param_t));
        params_ret->params[i]->param_len = esp8285_spi_read_byte();

        if (param_len_16)
        {
            params_ret->params[i]->param_len <<= 8;
            params_ret->params[i]->param_len |= esp8285_spi_read_byte();
        }

#if (ESP8285_SPI_DEBUG >= 2)
        mp_printf(MP_PYTHON_PRINTER,"\tParameter #%d length is %d\r\n", i, params_ret->params[i]->param_len);
#endif

        params_ret->params[i]->param = (uint8_t *)malloc(sizeof(uint8_t) * params_ret->params[i]->param_len);
        esp8285_spi_read_bytes(params_ret->params[i]->param, params_ret->params[i]->param_len);
    }

    if (esp8285_spi_check_data(END_CMD) != 0)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"esp8285_spi_check_data END_CMD error\r\n");
#endif
        gpio_put(cs_num, 1);
        return NULL;
    }

    gpio_put(cs_num, 1);

    return params_ret;
}
esp8285_spi_params_t *esp8285_spi_send_command_get_response(uint8_t cmd, esp8285_spi_params_t *params, uint32_t *num_resp, uint8_t sent_param_len_16, uint8_t recv_param_len_16)
{
    uint32_t resp_num;

    if (!num_resp)
        resp_num = 1;
    else
        resp_num = *num_resp;

    esp8285_spi_send_command(cmd, params, sent_param_len_16);
    return esp8285_spi_wait_response_cmd(cmd, &resp_num, recv_param_len_16);
}
static void delete_esp8285_spi_params(void *arg)
{
    esp8285_spi_params_t *params = (esp8285_spi_params_t *)arg;

    for (uint8_t i = 0; i < params->params_num; i++)
    {
        esp8285_spi_param_t *param = params->params[i];
        free(param->param);
        free(param);
    }
    free(params->params);
    free(params);
}

static esp8285_spi_params_t *esp8285_spi_params_alloc_1param(uint32_t len, uint8_t *buf)
{
    esp8285_spi_params_t *ret = (esp8285_spi_params_t *)malloc(sizeof(esp8285_spi_params_t));

    ret->del = delete_esp8285_spi_params;

    ret->params_num = 1;
    ret->params = (void *)malloc(sizeof(void *) * ret->params_num);
    ret->params[0] = (esp8285_spi_param_t *)malloc(sizeof(esp8285_spi_param_t));
    ret->params[0]->param_len = len;
    ret->params[0]->param = (uint8_t *)malloc(sizeof(uint8_t) * len);
    memcpy(ret->params[0]->param, buf, len);

    return ret;
}

int8_t esp8285_spi_start_scan_networks(void)
{
#if ESP8285_SPI_DEBUG
    mp_printf(MP_PYTHON_PRINTER,"Start scan\r\n");
#endif

    esp8285_spi_params_t *resp = esp8285_spi_send_command_get_response(START_SCAN_NETWORKS, NULL, NULL, 0, 0);

    if (resp == NULL)
    {
        mp_printf(MP_PYTHON_PRINTER,"%s: get resp error!\r\n", __func__);
        return -1;
    }

    if (resp->params[0]->param[0] != 1)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"Failed to start AP scan\r\n");
#endif
        resp->del(resp);
        return -1;
    }

    resp->del(resp);
    return 0;
}
static void delete_esp8285_spi_aps_list(void *arg)
{
    esp8285_spi_aps_list_t *aps = (esp8285_spi_aps_list_t *)arg;

    for (uint8_t i = 0; i < aps->aps_num; i++)
    {
        free(aps->aps[i]);
    }
    free(aps->aps);
    free(aps);
}
esp8285_spi_aps_list_t *esp8285_spi_get_scan_networks(void)
{

    esp8285_spi_send_command(SCAN_NETWORKS, NULL, 0);
    esp8285_spi_params_t *resp = esp8285_spi_wait_response_cmd(SCAN_NETWORKS, NULL, 0);

    if (resp == NULL)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"%s: get resp error!\r\n", __func__);
#endif
        return NULL;
    }

    esp8285_spi_aps_list_t *aps = (esp8285_spi_aps_list_t *)malloc(sizeof(esp8285_spi_aps_list_t));
    aps->del = delete_esp8285_spi_aps_list;

    aps->aps_num = resp->params_num;
    aps->aps = (void *)malloc(sizeof(void *) * aps->aps_num);

    for (uint32_t i = 0; i < aps->aps_num; i++)
    {
        aps->aps[i] = (esp8285_spi_ap_t *)malloc(sizeof(esp8285_spi_ap_t));
        memcpy(aps->aps[i]->ssid, resp->params[i]->param, (resp->params[i]->param_len > 32) ? 32 : resp->params[i]->param_len);
        aps->aps[i]->ssid[resp->params[i]->param_len] = 0;

        uint8_t data = i;
        esp8285_spi_params_t *send = esp8285_spi_params_alloc_1param(1, &data);

        esp8285_spi_params_t *rssi = esp8285_spi_send_command_get_response(GET_IDX_RSSI_CMD, send, NULL, 0, 0);

        aps->aps[i]->rssi = (int8_t)(rssi->params[0]->param[0]);
#if ESP8285_SPI_DEBUG
	mp_printf(MP_PYTHON_PRINTER,"\tSSID:%s", aps->aps[i]->ssid);
        mp_printf(MP_PYTHON_PRINTER,"\t\t\trssi:%02x\r\n", rssi->params[0]->param[0]);
#endif
        rssi->del(rssi);

        esp8285_spi_params_t *encr = esp8285_spi_send_command_get_response(GET_IDX_ENCT_CMD, send, NULL, 0, 0);
        aps->aps[i]->encr = encr->params[0]->param[0];
        encr->del(encr);

        send->del(send);
    }
    resp->del(resp);

    return aps;
}

esp8285_spi_aps_list_t *esp8285_spi_scan_networks(void)
{
    if (esp8285_spi_start_scan_networks() != 0)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"esp8285_spi_start_scan_networks failed \r\n");
#endif
        return NULL;
    }
	mp_printf(MP_PYTHON_PRINTER,"esp8285_spi_start_scan_networks failed \r\n");
    esp8285_spi_aps_list_t *retaps = esp8285_spi_get_scan_networks();

    if (retaps == NULL)
    {
#if ESP8285_SPI_DEBUG
        mp_printf(MP_PYTHON_PRINTER,"%s: get retaps error!\r\n", __func__);
#endif
        return NULL;
    }

#if (ESP8285_SPI_DEBUG >= 2)
    for (uint32_t i = 0; i < retaps->aps_num; i++)
    {
        mp_printf(MP_PYTHON_PRINTER,"\t#%d %s RSSI: %d ENCR:%d\r\n", i, retaps->aps[i]->ssid, retaps->aps[i]->rssi, retaps->aps[i]->encr);
    }
#endif
    //need free in call
    return retaps;
}

