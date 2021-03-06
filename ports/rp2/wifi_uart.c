/**
 * @file esp8266.c
 * 
 * @par Copyright:
 * Copyright (c) 2015 ITEAD Intelligent Systems Co., Ltd. \n\n
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version. \n\n
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "py/stream.h"
#include "py/runtime.h"
#include "py/misc.h"
#include "py/mphal.h"
#include "py/objstr.h"
#include "extmod/misc.h"
#include "lib/netutils/netutils.h"

//#include "utils.h"
#include "modmachine.h"
#include "mphalport.h"
#include "mpconfigboard.h"
//#include "machine_uart.h"
#include "modnetwork.h"
//#include "plic.h"cd
//#include "sysctl.h"
//#include "atomic.h"
#include "hardware/uart.h"
//#include "uarths.h"
#include "wifi_uart.h"
//#include "sleep.h"

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uart_inst_t *const uart;
    uint8_t uart_id;
    uint32_t baudrate;
    uint8_t bits;
    uart_parity_t parity;
    uint8_t stop;
    uint8_t tx;
    uint8_t rx;
    uint16_t timeout;       // timeout waiting for first char (in ms)
    uint16_t timeout_char;  // timeout waiting between chars (in ms)
    uint8_t invert;
    ringbuf_t read_buffer;
    bool read_lock;
    ringbuf_t write_buffer;
    bool write_lock;
} machine_uart_obj_t;

STATIC void uart_drain_rx_fifo(machine_uart_obj_t *self) {
    while (uart_is_readable(self->uart)) {
        // try to write the data, ignore the fail
        ringbuf_put(&(self->read_buffer), uart_get_hw(self->uart)->dr);
    }
}
STATIC void kmp_get_next(const char* targe, int next[])
{  
    int targe_Len = strlen(targe);  
    next[0] = -1;  
    int k = -1;  
    int j = 0;  
    while (j < targe_Len - 1)  
    {     
        if (k == -1 || targe[j] == targe[k])  
        {  
            ++j;  
            ++k;   
            if (targe[j] != targe[k])  
                next[j] = k;    
            else   
                next[j] = next[k];  
        }  
        else  
        {  
            k = next[k];  
        }  
    }  
}  
STATIC int kmp_match(char* src,int src_len, const char* targe, int* next)  
{  
    int i = 0;  
    int j = 0;  
    int sLen = src_len;  
    int pLen = strlen(targe);  
    while (i < sLen && j < pLen)  
    {     
        if (j == -1 || src[i] == targe[j])  
        {  
            i++;  
            j++;  
        }  
        else  
        {       
            j = next[j];  
        }  
    }  
    if (j == pLen)  
        return i - j;  
    else  
        return -1;  
} 
STATIC uint32_t kmp_find(char* src,uint32_t src_len, const char* tagert)
{
	uint32_t index = 0;
	uint32_t tag_len = strlen(tagert);
	int* next = (int*)malloc(sizeof(uint32_t) * tag_len);
	kmp_get_next(tagert,next);
	index = kmp_match(src,src_len,tagert, next);
	free(next);
	return index;
}
STATIC uint32_t data_find(uint8_t* src,uint32_t src_len, const char* tagert)
{
	return kmp_find((char*)src,src_len,tagert);
}

bool kick(esp8266_obj* nic)
{
    return eAT(nic);
}

bool reset(esp8266_obj* nic)
{
    unsigned long start;
    if (eATRST(nic)) {
        sleep_ms(2000);
        start = mp_hal_ticks_ms();
        while (mp_hal_ticks_ms() - start < 3000) {
            if (eAT(nic)) {
                sleep_ms(1500); /* Waiting for stable */
                return true;
            }
            sleep_ms(100);
        }
    }
    return false;
}

char* getVersion(esp8266_obj* nic)
{
    char* version;
    eATGMR(nic,&version);
    return version;
}

bool setOprToStation(esp8266_obj* nic, int set_mode)
{
    char mode;
    if (!qATCWMODE(nic,&mode)) {
        return false;
    }

    if (sATCWMODE(nic,set_mode)) {
        return true;
    } else {
        return false;
    }
}


bool joinAP(esp8266_obj* nic, const char* ssid, const char* pwd)
{
    return sATCWJAP(nic,ssid, pwd);
}

bool enableClientDHCP(esp8266_obj* nic,char mode, bool enabled)
{
    return sATCWDHCP(nic,mode, enabled);
}

bool leaveAP(esp8266_obj* nic)
{
    return eATCWQAP(nic);
}

char* getIPStatus(esp8266_obj* nic)
{
    char* list;
    eATCIPSTATUS(nic, &list);
    return list;
}

char* getLocalIP(esp8266_obj* nic)
{
    char* list;
    eATCIFSR(nic, &list);
    return list;
}

bool enableMUX(esp8266_obj* nic)
{
    return sATCIPMUX(nic,1);
}

bool disableMUX(esp8266_obj* nic)
{
    return sATCIPMUX(nic,0);
}

bool createTCP(esp8266_obj* nic,char* addr, uint32_t port)
{
    return sATCIPSTARTSingle(nic,"TCP", addr, port);
}

bool releaseTCP(esp8266_obj* nic)
{
    return eATCIPCLOSESingle(nic);
}

bool registerUDP(esp8266_obj* nic,char* addr, uint32_t port)
{
    return sATCIPSTARTSingle(nic,"UDP", addr, port);
}

bool unregisterUDP(esp8266_obj* nic)
{
    return eATCIPCLOSESingle(nic);
}

bool createTCP_mul(esp8266_obj* nic,char mux_id, char* addr, uint32_t port)
{
    return sATCIPSTARTMultiple(nic,mux_id, "TCP", addr, port);
}

bool releaseTCP_mul(esp8266_obj* nic,char mux_id)
{
    return sATCIPCLOSEMulitple(nic,mux_id);
}

bool registerUDP_mul(esp8266_obj* nic,char mux_id, char* addr, uint32_t port)
{
    return sATCIPSTARTMultiple(nic,mux_id, "UDP", addr, port);
}

bool unregisterUDP_mul(esp8266_obj* nic,char mux_id)
{
    return sATCIPCLOSEMulitple(nic,mux_id);
}

bool setTCPServerTimeout(esp8266_obj* nic,uint32_t timeout)
{
    return sATCIPSTO(nic,timeout);
}

bool startTCPServer(esp8266_obj* nic,uint32_t port)
{
    if (sATCIPSERVER(nic,1, port)) {
        return true;
    }
    return false;
}

bool stopTCPServer(esp8266_obj* nic)
{
    sATCIPSERVER(nic,0,0);
    reset(nic);
    return false;
}

bool startServer(esp8266_obj* nic,uint32_t port)
{
    return startTCPServer(nic,port);
}

bool stopServer(esp8266_obj* nic)
{
    return stopTCPServer(nic);
}

bool get_host_byname(esp8266_obj* nic, const char* host,uint32_t len,char* out_ip, uint32_t timeout_ms)
{
	int index = 0;
	if(false == sATCIPDOMAIN(nic,host, timeout_ms))
	{
		mp_printf(&mp_plat_print, "[wio rp2040] %s | get_host_byname failed\n",__func__);
		return false;
	}
	char IP_buf[16]={0};
	index = data_find(nic->buffer.buffer,ESP8266_BUF_SIZE,"+CIPDOMAIN:");
	sscanf((char*)nic->buffer.buffer + index,"+CIPDOMAIN:\"%[^\"]\"",IP_buf);
	mp_obj_t IP = mp_obj_new_str(IP_buf, strlen(IP_buf));
	nlr_buf_t nlr;
	if (nlr_push(&nlr) == 0)
	{
		netutils_parse_ipv4_addr(IP, (uint8_t*)out_ip,NETUTILS_BIG);
		nlr_pop();
	}
	return true;
}

bool get_ipconfig(esp8266_obj* nic, ipconfig_obj* ipconfig)
{

	if(0 == qATCIPSTA_CUR(nic))
		return false;
	char* cur = NULL;
	cur = strstr((char*)nic->buffer.buffer, "ip");
	if(cur == NULL)
	{
		mp_printf(&mp_plat_print, "[wio rp2040] %s | esp8266_ipconfig could'n get ip\n",__func__);
		return false;
	}
	char ip_buf[16] = {0};
	sscanf(cur,"ip:\"%[^\"]\"",ip_buf);
	ipconfig->ip = mp_obj_new_str(ip_buf,strlen(ip_buf));
	cur = strstr((char*)nic->buffer.buffer, "gateway");
	if(cur == NULL)
	{
		mp_printf(&mp_plat_print, "[wio rp2040] %s | esp8266_ipconfig could'n get gateway\n",__func__);
		return false;
	}
	char gateway_buf[16] = {0};
	sscanf(cur,"gateway:\"%[^\"]\"",gateway_buf);
	ipconfig->gateway = mp_obj_new_str(gateway_buf,strlen(gateway_buf));
	cur = strstr((char*)nic->buffer.buffer, "netmask");
	if(cur == NULL)
	{
		mp_printf(&mp_plat_print, "[wio rp2040] %s | esp8266_ipconfig could'n get netmask\n",__func__);
		return false;
	}
	char netmask_buf[16] = {0};
	sscanf(cur,"netmask:\"%[^\"]\"",netmask_buf);
	ipconfig->netmask = mp_obj_new_str(netmask_buf,strlen(netmask_buf));
	//ssid & mac
	if(false == qATCWJAP_CUR(nic))
	{
		return false;
	}
	char ssid[50] = {0};
	char MAC[17] = {0};
	cur = strstr((char*)nic->buffer.buffer, "+CWJAP:");
	sscanf(cur, "+CWJAP:\"%[^\"]\",\"%[^\"]\"", ssid, MAC);
	ipconfig->ssid = mp_obj_new_str(ssid,strlen(ssid));
	ipconfig->MAC = mp_obj_new_str(MAC,strlen(MAC));
	return true;
}
bool get_mqttconn(esp8266_obj* nic, mqttconn_obj* mqttconn)
{

	if(0 == qMQTTCONN(nic))
		return false;
	char* cur = NULL;
	cur = strstr((char*)nic->buffer.buffer, "+MQTTCONN:");
	if(cur == NULL)
	{
		mp_printf(&mp_plat_print, " %s | MQTTCONN could'n get \n",__func__);
		return false;
	}
	int LinkID;
	int state;
	int scheme;
	char host[128] = {0};
	int port;
	char path[32] = {0};
	int reconnect;
	sscanf(cur, "+MQTTCONN:%d,%d,%d,\"%[^\"]\",%d,\"%[^\"]\",%d", &mqttconn->LinkID, &mqttconn->state, &mqttconn->scheme, mqttconn->host, &mqttconn->port, mqttconn->path, &mqttconn->reconnect);
	return true;
}
bool get_mqttsubrecv(esp8266_obj*nic, uint32_t LinkID, mqtt_msg* mqttmsg)
{
	char* cur = NULL;
	int errcode;
	uint32_t iter = 0;
	memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);
    unsigned long start = mp_hal_ticks_ms();
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	machine_uart_obj_t *self = MP_OBJ_TO_PTR(nic->uart_obj);
	while (mp_hal_ticks_ms() - start < 3000) {
        self->read_lock = true;
        uart_drain_rx_fifo(self);
        self->read_lock = false;
        while(ringbuf_avail(&self->read_buffer) > 0) {
            uart_stream->read(nic->uart_obj,&nic->buffer.buffer[iter++],1,&errcode);
        }
	}
	cur = strstr((char*)nic->buffer.buffer, "+MQTTSUBRECV");
	if(cur == NULL)
	{
		//mp_printf(MP_PYTHON_PRINTER, "test get_mqttsubrecv none\n");
		return false;
	}
	int LinkID_recv;
	int len;
	char topic[255] = {0};
	char msg[4096] = {0};	
	sscanf(cur, "+MQTTSUBRECV:%d,\"%[^\"]\",%d,%[^\n]", &LinkID_recv, topic, &len, msg);
	msg[len] = NULL;
	mqttmsg->topic = mp_obj_new_str(topic,strlen(topic));
	mqttmsg->msg = mp_obj_new_str(msg,len);
	return true;
}
bool wifi_softap_get_config(esp8266_obj* nic, softap_config* apconfig)
{
	if(0 == qATCWSAP(nic))
		return false;
	char* cur = NULL;
	cur = strstr((char*)nic->buffer.buffer, "+CWSAP:");
	if(cur == NULL)
	{
		mp_printf(&mp_plat_print, "[wio rp2040] %s | esp8266_config could'n get ip\n",__func__);
		return false;
	}
	sscanf(cur, "+CWSAP:\"%[^\"]\",\"%[^\"]\",%d,%d,%d,%d", apconfig->ssid, apconfig->password, &apconfig->channel, &apconfig->authmode,  &apconfig->max_conn, &apconfig->ssid_hidden);
	if(apconfig->authmode > 5 || apconfig->authmode < 0)
	{
		sscanf(cur, "+CWSAP:\"%[^\"]\",\"\",%d,%d,%d,%d", apconfig->ssid, &apconfig->channel, &apconfig->authmode,  &apconfig->max_conn, &apconfig->ssid_hidden);
		*apconfig->password = NULL;
	}
	//nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_OSError, "qian connect to 1.%s 2.%s 3.%d 4.%d 5.%d 6.%d", apconfig->ssid,apconfig->password,&apconfig->channel,&apconfig->authmode,&apconfig->max_conn,&apconfig->ssid_hidden));
	//mp_printf(MP_PYTHON_PRINTER, "qian connect to 1.%s 2.%s 3.%d 4.%d 5.%d 6.%d", apconfig->ssid,apconfig->password,apconfig->channel,apconfig->authmode,apconfig->max_conn,apconfig->ssid_hidden);
	return true;
}

bool wifi_softap_set_config(esp8266_obj* nic, softap_config* apconfig)
{
	apconfig->max_conn = 8;
	return sATCWSAP(nic, apconfig->ssid, apconfig->password, apconfig->channel, apconfig->authmode, apconfig->max_conn, apconfig->ssid_hidden);
}

bool esp_send(esp8266_obj* nic,const char* buffer, uint32_t len, uint32_t timeout)
{
    bool ret = false;
    uint32_t send_total_len = 0;
    uint16_t send_len = 0;

    while(send_total_len < len)
    {
        send_len = ((len-send_total_len) > ESP8266_MAX_ONCE_SEND)?ESP8266_MAX_ONCE_SEND : (len-send_total_len);
        ret = sATCIPSENDSingle(nic,buffer+send_total_len, send_len, timeout);
        if(!ret)
            return false;
        send_total_len += send_len;
    }
    return true;
}

bool esp_send_mul(esp8266_obj* nic,char mux_id, const char* buffer, uint32_t len)
{
    return sATCIPSENDMultiple(nic,mux_id, buffer, len);
}

int esp_recv(esp8266_obj* nic,char* buffer, uint32_t buffer_size, uint32_t* read_len, uint32_t timeout, bool* peer_closed, bool first_time_recv)
{
    return recvPkg(nic,buffer, buffer_size, read_len, timeout, NULL, peer_closed, first_time_recv);
}

uint32_t esp_recv_mul(esp8266_obj* nic,char mux_id, char* buffer, uint32_t buffer_size, uint32_t timeout)
{
    char id;
    uint32_t ret;
    ret = recvPkg(nic,buffer, buffer_size, NULL, timeout, &id, NULL, false);
    if (ret > 0 && id == mux_id) {
        return ret;
    }
    return 0;
}

uint32_t esp_recv_mul_id(esp8266_obj* nic,char* coming_mux_id, char* buffer, uint32_t buffer_size, uint32_t timeout)
{
    return recvPkg(nic,buffer, buffer_size, NULL, timeout, coming_mux_id, NULL, false);
}
//#include "printf.h"
/*----------------------------------------------------------------------------*/
/* +IPD,<id>,<len>:<data> */
/* +IPD,<len>:<data> */
/**
 * 
 * @return -1: parameters error, -2: EOF, -3: timeout, -4:peer closed and no data in buffer
 */
uint32_t recvPkg(esp8266_obj*nic,char* out_buff, uint32_t out_buff_len, uint32_t *data_len, uint32_t timeout, char* coming_mux_id, bool* peer_closed, bool first_time_recv)
{
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);

    uint8_t temp_buff[16];
    uint8_t temp_buff2[16];
    uint8_t temp_buff_len = 0;
    uint8_t temp_buff2_len = 0;
    uint8_t find_frame_flag_index = 0;
    static int8_t mux_id = -1;
    static int16_t frame_len = 0;
    static int32_t frame_len_sum = 0;//only for single socket TODO:
    // bool    overflow = false;
    int ret = 0;
    uint32_t size = 0;
	int errcode;
    mp_uint_t start = 0, start2 = 0;
    bool no_frame_flag = false;
    bool new_frame = false;
    mp_uint_t data_len_in_uart_buff = 0;
    bool peer_just_closed = false;
    
    // parameters check
    if (out_buff == NULL) {
        return -1;
    }
    // init vars
    memset(temp_buff, 0, sizeof(temp_buff));
    memset(temp_buff, 0, sizeof(temp_buff2));
    if(first_time_recv)
    {
        frame_len = 0;
        frame_len_sum = 0;
    }

    // required data already in buf, just return data
    uint32_t buff_size = Buffer_Size(&nic->buffer);
    if(buff_size >= out_buff_len)
    {
        Buffer_Gets(&nic->buffer, (uint8_t*)out_buff, out_buff_len);
        if(data_len)
            *data_len = out_buff_len;
        frame_len_sum -= size;
        if(frame_len_sum <= 0)
        {
            frame_len = 0;
            frame_len_sum = 0;
            Buffer_Clear(&nic->buffer);
            if(*peer_closed)//buffer empty, return EOF
            {
                return -2;
            }
        }
        return out_buff_len;
    }
    
    // read from uart buffer, if not frame start flag, put into nic buffer
    // and need wait for full frame flag in 200ms(can be fewer), frame format: '+IPD,id,len:data' or '+IPD,len:data'
    // wait data from uart buffer if not timeout
    start2 = mp_hal_ticks_ms();
	machine_uart_obj_t *self = MP_OBJ_TO_PTR(nic->uart_obj);
    do{
        self->read_lock = true;
        uart_drain_rx_fifo(self);
        self->read_lock = false;
        if(ringbuf_avail(&self->read_buffer) > 0)
        {
            uart_stream->read(nic->uart_obj,temp_buff + temp_buff_len,1,&errcode);
            if(find_frame_flag_index == 0 && temp_buff[temp_buff_len] == '+'){
                ++find_frame_flag_index;
                start = mp_hal_ticks_ms();
            }
            else if(find_frame_flag_index == 1 && temp_buff[temp_buff_len] == 'I'){
                ++find_frame_flag_index;
            }
            else if(find_frame_flag_index == 2 && temp_buff[temp_buff_len] == 'P'){
                ++find_frame_flag_index;
            }
            else if(find_frame_flag_index == 3 && temp_buff[temp_buff_len] == 'D'){
                ++find_frame_flag_index;
            }
            else if(find_frame_flag_index == 4 && temp_buff[temp_buff_len] == ','){
                ++find_frame_flag_index;
            }
            else if(find_frame_flag_index == 5){
                    if(temp_buff[temp_buff_len] == ':'){ // '+IPD,3,1452:' or '+IPD,1452:'
                        temp_buff[temp_buff_len+1] = '\0';
                        char* index = strstr((char*)temp_buff+5, ",");
                        if(index){ // '+IPD,3,1452:'
                            ret = sscanf((char*)temp_buff,"+IPD,%hhd,%hd:",&mux_id,&frame_len);
                            if (ret!=2 || mux_id < 0 || mux_id > 4 || frame_len<=0) { // format not satisfy, it's data
                                no_frame_flag = true;
                            }else{// find frame start flag, although it may also data
                                new_frame = true;
                            }
                        }else{ // '+IPD,1452:'
                            ret = sscanf((char*)temp_buff,"+IPD,%hd:",&frame_len);
                            if (ret !=1 || frame_len<=0) { // format not satisfy, it's data
                                no_frame_flag = true;
                            }else{// find frame start flag, although it may also data
                                new_frame = true;
                                // printk("new frame:%d\r\n", frame_len);
                            }
                        }
                    }
            }
            else{ // not match frame start flag, put into nic buffer
                no_frame_flag = true;
            }
            // new frame or data
            // or wait for frame start flag timeout(300ms, can be fewer), maybe they're data
            if(new_frame || no_frame_flag || temp_buff_len >= 12 ||
                (find_frame_flag_index && (mp_hal_ticks_ms() - start > 300) )
            ) // '+IPD,3,1452:'
            {
                if(!new_frame){
                    if(frame_len_sum > 0){
                        if(!Buffer_Puts(&nic->buffer, temp_buff, temp_buff_len+1))
                        {
                            // overflow = true;
                            // break;//TODO:
                        }
                    }
                    else{
                        if(temp_buff[0]=='C'){
                            memset(temp_buff2, 0, sizeof(temp_buff2));
                        }
                        temp_buff2[temp_buff2_len++] = temp_buff[0];
                        // printk("%c", temp_buff[0]); //TODO: optimize uart overflow, if uart overflow, uncomment this will print some data
                        // printk("-%d:%s\r\n", temp_buff2_len, temp_buff2);
                        if(strstr((const char*)temp_buff2, "CLOSED\r\n") != NULL){
                            // printk("pear closed\r\n");
                            *peer_closed = true;
                            peer_just_closed = true;
                            break;
                        }
                    }
                }else{
                    frame_len_sum += frame_len;
                }
                find_frame_flag_index = 0;
                temp_buff_len = 0;
                new_frame = false;
                no_frame_flag = false;
                // enough data as required
                size = Buffer_Size(&nic->buffer);
                if( size >= out_buff_len) // data enough
                    break;
                if(frame_len_sum!=0 && frame_len_sum <= size) // read at least one frame ok
                {
                    break;
                }
                continue;
            }
            ++temp_buff_len;
        }
        if(timeout!=0 && (mp_hal_ticks_ms() - start2 > timeout) && !find_frame_flag_index )
        {
            return -3;
        }
        self->read_lock = true;
        uart_drain_rx_fifo(self);
        self->read_lock = false;
    }while( (timeout || find_frame_flag_index) && (!*peer_closed || ringbuf_avail(&self->read_buffer) > 0) );
    size = Buffer_Size(&nic->buffer);
    if( size == 0 && !peer_just_closed && *peer_closed)//peer closed and no data in buffer
    {
        frame_len = 0;
        frame_len_sum = 0;
        return -4;
    }
    size = size > out_buff_len ? out_buff_len : size;
    Buffer_Gets(&nic->buffer, (uint8_t*)out_buff, size);
    if(data_len)
        *data_len = size;
    frame_len_sum -= size;
    if(frame_len_sum <= 0 || peer_just_closed)
    {
        frame_len = 0;
        frame_len_sum = 0;
        Buffer_Clear(&nic->buffer);
        if(peer_just_closed)
        {
            return -2;
        }
    }
    return size;
}

void rx_empty(esp8266_obj* nic) 
{
	int errcode;
	char data = 0;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	machine_uart_obj_t *self = MP_OBJ_TO_PTR(nic->uart_obj);
    self->read_lock = true;
    uart_drain_rx_fifo(self);
    self->read_lock = false;
    while(ringbuf_avail(&self->read_buffer) > 0) {
        uart_stream->read(nic->uart_obj,&data,1,&errcode); 
    }
}

char* recvString_1(esp8266_obj* nic, const char* target1,uint32_t timeout)
{
	int errcode;
	uint32_t iter = 0;
	memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);
    unsigned long start = mp_hal_ticks_ms();
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	machine_uart_obj_t *self = MP_OBJ_TO_PTR(nic->uart_obj);
    while (mp_hal_ticks_ms() - start < timeout) {
        self->read_lock = true;
        uart_drain_rx_fifo(self);
        self->read_lock = false;
        while(ringbuf_avail(&self->read_buffer) > 0) {
            uart_stream->read(nic->uart_obj,&nic->buffer.buffer[iter++],1,&errcode);
        }
        if (data_find(nic->buffer.buffer,iter,target1) != -1) {
            return (char*)nic->buffer.buffer;
        } 
    }
    return NULL;
}


char* recvString_2(esp8266_obj* nic,char* target1, char* target2, uint32_t timeout, int8_t* find_index)
{
	int errcode;
	uint32_t iter = 0;
    *find_index = -1;
	memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);
    unsigned int start = mp_hal_ticks_ms();
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	machine_uart_obj_t *self = MP_OBJ_TO_PTR(nic->uart_obj);
    while (mp_hal_ticks_ms() - start < timeout) {
        self->read_lock = true;
        uart_drain_rx_fifo(self);
        self->read_lock = false;
        while(ringbuf_avail(&self->read_buffer) > 0 && iter < ESP8266_BUF_SIZE) {
            uart_stream->read(nic->uart_obj,&nic->buffer.buffer[iter++],1,&errcode);
        }
        if (data_find(nic->buffer.buffer,iter,target1) != -1) {
            *find_index = 0;
            return (char*)nic->buffer.buffer;
        } else if (data_find(nic->buffer.buffer,iter,target2) != -1) {
            *find_index = 1;
            return (char*)nic->buffer.buffer;
        }
    }
    return NULL;
}

char* recvString_3(esp8266_obj* nic,char* target1, char* target2,char* target3,uint32_t timeout, int8_t* find_index)
{

	int errcode;
	uint32_t iter = 0;
    *find_index = -1;
	memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);
    unsigned long start = mp_hal_ticks_ms();
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	machine_uart_obj_t *self = MP_OBJ_TO_PTR(nic->uart_obj);
    while (mp_hal_ticks_ms() - start < timeout) {
        self->read_lock = true;
        uart_drain_rx_fifo(self);
        self->read_lock = false;
        while(ringbuf_avail(&self->read_buffer) > 0) {
            uart_stream->read(nic->uart_obj,&nic->buffer.buffer[iter++],1,&errcode);
        }
        if (data_find(nic->buffer.buffer,iter,target1) != -1) {
            *find_index = 0;
            return (char*)nic->buffer.buffer;
        } else if (data_find(nic->buffer.buffer,iter,target2) != -1) {
            *find_index = 1;
            return (char*)nic->buffer.buffer;
        } else if (data_find(nic->buffer.buffer,iter,target3) != -1) {
            *find_index = 2;
            return (char*)nic->buffer.buffer;
        }
    }
    return NULL;
}

bool recvFind(esp8266_obj* nic, const char* target, uint32_t timeout)
{
    recvString_1(nic, target, timeout);
    if (data_find(nic->buffer.buffer,ESP8266_BUF_SIZE,target) != -1) {
        return true;
    }
    return false;
}

bool recvFindAndFilter(esp8266_obj* nic,const char* target, const char* begin, const char* end, char** data, uint32_t timeout)
{
    recvString_1(nic,target, timeout);
    if (data_find(nic->buffer.buffer,ESP8266_BUF_SIZE,target) != -1) {
        int32_t index1 = data_find(nic->buffer.buffer,ESP8266_BUF_SIZE,begin);
        int32_t index2 = data_find(nic->buffer.buffer,ESP8266_BUF_SIZE,end);
        if (index1 != -1 && index2 != -1) {
            index1 += strlen(begin);
			*data = m_new(char, index2 - index1);
			memcpy(*data,nic->buffer.buffer+index1, index2 - index1);
            return true;
        }
    }
    return false;
}

bool eAT(esp8266_obj* nic)
{	
	int errcode = 0;
	const char* cmd = "AT\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    rx_empty(nic);// clear rx
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    if(recvFind(nic,"OK",1000))
        return true;
    rx_empty(nic);
    uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    if(recvFind(nic,"OK",1000))
        return true;
    rx_empty(nic);
    uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    return recvFind(nic,"OK",1000);
}

bool eATE(esp8266_obj* nic,bool enable)
{	
	int errcode = 0;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    rx_empty(nic);// clear rx
    if(enable)
    {
    	const char* cmd = "ATE0\r\n";
		uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    	return recvFind(nic,"OK",1000);
    }
	else
	{
    	const char* cmd = "ATE1\r\n";
		uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    	return recvFind(nic,"OK",1000);		
	}
}


bool eATRST(esp8266_obj* nic) 
{
	int errcode = 0;
	const char* cmd = "AT+RST\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    rx_empty(nic);// clear rx
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    return recvFind(nic,"OK",1000);
}

bool eATGMR(esp8266_obj* nic,char** version)
{

	int errcode = 0;
	const char* cmd = "AT+GMR\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    rx_empty(nic);// clear rx
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);

    return recvFindAndFilter(nic,"OK", "\r\r\n", "\r\n\r\nOK", version, 5000); 
}

bool qATCWMODE(esp8266_obj* nic,char* mode) 
{
	int errcode = 0;
	const char* cmd = "AT+CWMODE?\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char* str_mode;
    bool ret;
    if (!mode) {
        return false;
    }
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    ret = recvFindAndFilter(nic,"OK", "+CWMODE:", "\r\n\r\nOK", &str_mode,1000); 
    if (ret) {
        *mode = atoi(str_mode);
        return true;
    } else {
        return false;
    }
}

bool sATCWMODE(esp8266_obj* nic,char mode)
{
	int errcode = 0;
	const char* cmd = "AT+CWMODE=";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
	char mode_str[10] = {0};
    int8_t find;
	itoa(mode, mode_str, 10);
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,mode_str,strlen(mode_str),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);    
    if(recvString_2(nic,"OK", "no change",1000, &find) != NULL)
        return true;
    return false;
}

bool sATCWJAP(esp8266_obj* nic, const char* ssid, const char* pwd)
{
	int errcode = 0;
	const char* cmd = "AT+CWJAP=\"";
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);	
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,ssid,strlen(ssid),&errcode);
	uart_stream->write(nic->uart_obj,"\",\"",strlen("\",\""),&errcode);
	uart_stream->write(nic->uart_obj,pwd,strlen(pwd),&errcode);
	uart_stream->write(nic->uart_obj,"\"",strlen("\""),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_2(nic,"OK", "FAIL", 10000, &find) != NULL && find==0 )
        return true;
    return false;
}

bool sATCWDHCP(esp8266_obj* nic,char mode, bool enabled)
{
	int errcode = 0;
	const char* cmd = "AT+CWDHCP=";
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
	char strEn[2] = {0};
	if (enabled) {
		strcpy(strEn, "1");
	}
	else
	{
		strcpy(strEn, "0");
	}
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,strEn,strlen(strEn),&errcode);
	uart_stream->write(nic->uart_obj,",",strlen(","),&errcode);
	uart_stream->write(nic->uart_obj, &mode,1,&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if( recvString_2(nic,"OK", "FAIL", 10000, &find) != NULL && find==0)
        return true;    
    return false;
}


bool eATCWQAP(esp8266_obj* nic)
{
	int errcode = 0;
	const char* cmd = "AT+CWQAP\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    return recvFind(nic,"OK",1000);
}

bool eATCIPSTATUS(esp8266_obj* nic,char** list)
{
    int errcode = 0;
	const char* cmd = "AT+CIPSTATUS\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    sleep_ms(100);
    rx_empty(nic);
    uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    return recvFindAndFilter(nic,"OK", "\r\r\n", "\r\n\r\nOK", list,1000);
}
bool sATCIPSTARTSingle(esp8266_obj* nic,const char* type, char* addr, uint32_t port)
{
    int errcode = 0;
	const char* cmd = "AT+CIPSTART=\"";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	mp_obj_t IP = netutils_format_ipv4_addr((uint8_t*)addr,NETUTILS_BIG);
	const char* host = mp_obj_str_get_str(IP);
	char port_str[10] = {0};
    int8_t find_index;
	itoa(port, port_str, 10);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,type,strlen(type),&errcode);
	uart_stream->write(nic->uart_obj,"\",\"",strlen("\",\""),&errcode);
	uart_stream->write(nic->uart_obj,host,strlen(host),&errcode);
	uart_stream->write(nic->uart_obj,"\",",strlen("\","),&errcode);
	uart_stream->write(nic->uart_obj,port_str,strlen(port_str),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_3(nic,"OK", "ERROR", "ALREADY CONNECT", 10000, &find_index)!=NULL && (find_index==0 || find_index==2) )
        return true;
    return false;
}
bool sATCIPSTARTMultiple(esp8266_obj*nic,char mux_id, char* type, char* addr, uint32_t port)
{
    int errcode = 0;
	const char* cmd = "AT+CIPSTART=";
	char port_str[10] = {0};
    int8_t find_index;
	itoa(port,port_str ,10);
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode); 
	uart_stream->write(nic->uart_obj, &mux_id,1,&errcode);
	uart_stream->write(nic->uart_obj,",\"",strlen(",\""),&errcode);
	uart_stream->write(nic->uart_obj,type,strlen(type),&errcode);
	uart_stream->write(nic->uart_obj,"\",\"",strlen("\",\""),&errcode);
	uart_stream->write(nic->uart_obj,addr,strlen(addr),&errcode);
	uart_stream->write(nic->uart_obj,"\",",strlen("\","),&errcode);
	uart_stream->write(nic->uart_obj,port_str,strlen(port_str),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_3(nic,"OK", "ERROR", "ALREADY CONNECT", 10000, &find_index) != NULL  && (find_index==0 || find_index==2) )
        return true;
    return false;
}
bool sATCIPSENDSingle(esp8266_obj*nic,const char* buffer, uint32_t len, uint32_t timeout)
{
	int errcode = 0;
	const char* cmd = "AT+CIPSEND=";
	char len_str[10] = {0};
	itoa(len,len_str ,10);
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,len_str,strlen(len_str),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if (recvFind(nic,">", 5000)) {
        rx_empty(nic);
		uart_stream->write(nic->uart_obj,buffer,len,&errcode);
        return recvFind(nic,"SEND OK", timeout);
    }
    return false;
}
bool sATCIPSENDMultiple(esp8266_obj* nic,char mux_id, const char* buffer, uint32_t len)
{
   	int errcode = 0;
	const char* cmd = "AT+CIPSEND=";
	char len_str[10] = {0};
	itoa(len,len_str ,10);
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj, &mux_id,1,&errcode);
	uart_stream->write(nic->uart_obj,",",strlen(","),&errcode);
	uart_stream->write(nic->uart_obj,len_str,strlen(len_str),&errcode);
    if (recvFind(nic,">", 5000)) {
        rx_empty(nic);
		uart_stream->write(nic->uart_obj,buffer,len,&errcode);
		uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
        return recvFind(nic,"SEND OK", 10000);
    }
    return false;
}
bool sATCIPCLOSEMulitple(esp8266_obj* nic,char mux_id)
{
	int errcode = 0;
	const char* cmd = "AT+CIPCLOSE=";
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,(const char*)&mux_id,1,&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_2(nic,"OK", "link is not", 5000, &find) != NULL)
        return true;
    return false;
}
bool eATCIPCLOSESingle(esp8266_obj* nic)
{

    int8_t find;
	int errcode = 0;
	const char* cmd = "AT+CIPCLOSE\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    if (recvString_2(nic, "OK", "ERROR", 5000, &find) != NULL)
    {
        if( find == 0)
            return true;
    }
    return false;
}
bool eATCIFSR(esp8266_obj* nic,char** list)
{
	int errcode = 0;
	const char* cmd = "AT+CIFSR\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    return recvFindAndFilter(nic,"OK", "\r\r\n", "\r\n\r\nOK", list,5000);
}
bool sATCIPMUX(esp8266_obj* nic,char mode)
{
	int errcode = 0;
	const char* cmd = "AT+CIPMUX=";
	char mode_str[10] = {0};
    int8_t find;
	itoa(mode, mode_str, 10);
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,mode_str,strlen(mode_str),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_2(nic,"OK", "Link is builded",5000, &find) != NULL && find==0)
        return true;
    return false;
}
bool sATCIPSERVER(esp8266_obj* nic,char mode, uint32_t port)
{
	int errcode = 0;
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    if (mode) {
		const char* cmd = "AT+CIPSERVER=1,";
		char port_str[10] = {0};
		itoa(port, port_str, 10);
        rx_empty(nic);
		uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
		uart_stream->write(nic->uart_obj,port_str,strlen(port_str),&errcode);
		uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
        if(recvString_2(nic,"OK", "no change",1000, &find) != NULL)
            return true;
        return false;
    } else {
        rx_empty(nic);
		const char* cmd = "AT+CIPSERVER=0";
		uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
		uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
        return recvFind(nic,"\r\r\n",1000);
    }
}
bool sATCIPSTO(esp8266_obj* nic,uint32_t timeout)
{

	int errcode = 0;
	const char* cmd = "AT+CIPSTO=";
	char timeout_str[10] = {0};
	itoa(timeout, timeout_str, 10);
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,timeout_str,strlen(timeout_str),&errcode);
    uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    return recvFind(nic,"OK",1000);
}

bool sATCIPMODE(esp8266_obj* nic,char mode)
{

	int errcode = 0;
	const char* cmd = "AT+CIPMODE=";
	char mode_str[10] = {0};
	itoa(mode, mode_str, 10);
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,mode_str,strlen(mode_str),&errcode);
    uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    return recvFind(nic,"OK",1000);
}

bool sATCIPDOMAIN(esp8266_obj* nic, const char* domain_name, uint32_t timeout)
{
	int errcode = 0;
	const char* cmd = "AT+CIPDOMAIN=";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\"",strlen("\""),&errcode);
	uart_stream->write(nic->uart_obj,domain_name,strlen(domain_name),&errcode);
	uart_stream->write(nic->uart_obj,"\"",strlen("\""),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);  
    return recvFind(nic,"OK",timeout);
}

bool qATCIPSTA_CUR(esp8266_obj* nic)
{
	int errcode = 0;
	const char* cmd = "AT+CIPSTA?";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
	return recvFind(nic,"OK",1000);
}
bool sATCIPSTA_CUR(esp8266_obj* nic, const char* ip,char* gateway,char* netmask)
{
	int errcode = 0;
	const char* cmd = "AT+CIPSTA_CUR=";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	if(NULL == ip)
	{
		return false;
	}
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,ip,strlen(ip),&errcode);
	if(NULL == gateway)
	{
		uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
		return recvFind(nic,"OK",1000);
	}
	uart_stream->write(nic->uart_obj,",",strlen(","),&errcode);
	uart_stream->write(nic->uart_obj,gateway,strlen(gateway),&errcode);
	if(NULL == netmask)
	{
		uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
		return recvFind(nic,"OK",1000);
	}
	uart_stream->write(nic->uart_obj,",",strlen(","),&errcode);
	uart_stream->write(nic->uart_obj,netmask,strlen(netmask),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
	return recvFind(nic,"OK",1000);
}

bool qATCWJAP_CUR(esp8266_obj* nic)
{
	int errcode = 0;
	const char* cmd = "AT+CWJAP?";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
	return recvFind(nic,"OK",1000);
}

bool eINIT(esp8266_obj* nic, int mode)
{
	bool init_flag = 1;
	init_flag = init_flag && eAT(nic);
	init_flag = init_flag && eATE(nic,1);
	init_flag = init_flag && disableMUX(nic);
	init_flag = init_flag && sATCIPMODE(nic,0);
	init_flag = init_flag && setOprToStation(nic, mode);
    if(mode & SOFTAP_MODE){
		init_flag = init_flag && enableMUX(nic);
	}
	else{
		init_flag = init_flag && disableMUX(nic);
	}
	if(!mode & SOFTAP_MODE){
		init_flag = init_flag && leaveAP(nic);
	}
	return init_flag;
}

bool eATCWLAP(esp8266_obj* nic)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    const char cmd[] = {"AT+CWLAP"};

    rx_empty(nic);
    uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);

    if (recvString_1(nic, "\r\n\r\nOK", 10000) != NULL)
        return true;
    
    return false;
}

bool eATCWLAP_Start(esp8266_obj* nic)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    const char cmd[] = {"AT+CWLAP"};

    rx_empty(nic);
    uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);  
    return true;
}

bool eATCWLAP_Get(esp8266_obj* nic, bool* end)
{
    int8_t find;
    *end = false;
    if (recvString_2(nic, "\r\n+CWLAP:", "\r\n\r\nOK", 10000, &find) != NULL)
    {
        if( find == 1)
            *end = true;
        return true;
    }
    return false;
}

bool qATCWSAP(esp8266_obj* nic)
{
	int errcode = 0;
	const char* cmd = "AT+CWSAP?";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
	return recvFind(nic,"OK",1000);
}
bool eATCWSAP(esp8266_obj* nic, const char* ssid, const char* key, int chl, int ecn)
{
    int errcode;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char ap_cmd[128] = {0};

    if (sATCWMODE(nic, 3) == false)
    {
        return false;
    }

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(ap_cmd, "AT+CWSAP=\"%s\",\"%s\",%d,%d", ssid, key, chl, ecn);
    uart_stream->write(nic->uart_obj,ap_cmd, strlen(ap_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }

    return true;
}
bool sATCWSAP(esp8266_obj* nic, const char* ssid, const char* key, int chl, int ecn, int max_conn, int ssid_hidden)
{
    int errcode;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char ap_cmd[128] = {0};

    if (sATCWMODE(nic, 3) == false)
    {
        return false;
    }

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(ap_cmd, "AT+CWSAP=\"%s\",\"%s\",%d,%d,%d,%d", ssid, key, chl, ecn, max_conn, ssid_hidden);
    uart_stream->write(nic->uart_obj,ap_cmd, strlen(ap_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }

    return true;
}
bool sCIPSTAMAC(esp8266_obj* nic, const char* mac)
{
	int errcode = 0;
	const char* cmd = "AT+CIPSTAMAC=\"";
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);	
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,mac,strlen(mac),&errcode);
	uart_stream->write(nic->uart_obj,"\"",strlen("\""),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_2(nic,"OK", "ERROR", 10000, &find) != NULL && find==0 )
        return true;
    return false;
}
bool sCIPAPMAC(esp8266_obj* nic, const char* mac)
{
	int errcode = 0;
	const char* cmd = "AT+CIPAPMAC=\"";
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);	
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,mac,strlen(mac),&errcode);
	uart_stream->write(nic->uart_obj,"\"",strlen("\""),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_2(nic,"OK", "ERROR", 10000, &find) != NULL && find==0 )
        return true;
    return false;
}
bool qCIPSTAMAC(esp8266_obj* nic,char* mac) 
{
	int errcode = 0;
    char* cur = NULL;
	const char* cmd = "AT+CIPSTAMAC?\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    bool ret;
    if (!mac) {
        return false;
    }
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    ret = recvFind(nic,"OK",1000); 
    cur = strstr((char*)nic->buffer.buffer, "+CIPSTAMAC:");
	sscanf(cur, "+CIPSTAMAC:\"%[^\"]\"", mac);
    if (ret) {
        return true;
    } else {
        return false;
    }
}
bool qCIPAPMAC(esp8266_obj* nic,char* mac) 
{
	int errcode = 0;
    char* cur = NULL;
	const char* cmd = "AT+CIPAPMAC?\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    bool ret;
    if (!mac) {
        return false;
    }
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    ret = recvFind(nic,"OK",1000); 
    cur = strstr((char*)nic->buffer.buffer, "+CIPAPMAC:");
	sscanf(cur, "+CIPAPMAC:\"%[^\"]\"", mac);
    if (ret) {
        return true;
    } else {
        return false;
    }
}
bool sCWHOSTNAME(esp8266_obj* nic, const char* ssid)
{
	int errcode = 0;
	const char* cmd = "AT+CWHOSTNAME=\"";
    int8_t find;
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);	
    rx_empty(nic);	
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,ssid,strlen(ssid),&errcode);
    uart_stream->write(nic->uart_obj,"\"",strlen("\""),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
    if(recvString_2(nic,"OK", "ERROR", 10000, &find) != NULL && find==0 )
        return true;
    return false;
}
bool qCWHOSTNAME(esp8266_obj* nic,char* ssid) 
{
	int errcode = 0;
    char* cur = NULL;
	const char* cmd = "AT+CWHOSTNAME?\r\n";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    bool ret;
    if (!ssid) {
        return false;
    }
    rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
    ret = recvFind(nic,"OK",1000); 
    cur = strstr((char*)nic->buffer.buffer, "+CWHOSTNAME:");
	sscanf(cur, "+CWHOSTNAME:%s", ssid);
    if (ret) {
        return true;
    } else {
        return false;
    }
}
bool sMQTTUSERCFG(esp8266_obj* nic,int LinkID, int scheme, const char* client_id, const char* username, const char* password, int cert_key_ID, int CA_ID, const char* path)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTUSERCFG=%d,%d,\"%s\",\"%s\",\"%s\",%d,%d,\"%s\"", LinkID, scheme, client_id, username, password, cert_key_ID, CA_ID, path);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }

    return true;
}
bool sMQTTUSERNAME(esp8266_obj* nic,int LinkID, int username)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTUSERNAME=%d,\"%s\"", LinkID, username);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }

    return true;
}
bool sMQTTPASSWORD(esp8266_obj* nic,int LinkID, int password)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTUSERNAME=%d,\"%s\"", LinkID, password);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }
    return true;
}
bool sMQTTCONNCFG(esp8266_obj* nic,int LinkID, int keepalive, int disable_clean_session, const char* lwt_topic, const char* wt_msg, int lwt_qos, int lwt_retain)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTCONNCFG=%d,%d,%d,\"%s\",\"%s\",%d,%d", LinkID, keepalive, disable_clean_session, lwt_topic, wt_msg, lwt_qos, lwt_retain);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }

    return true;
}
bool sMQTTCONN(esp8266_obj* nic,int LinkID, const char* host, int port, int reconnect)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTCONN=%d,\"%s\",%d,%d", LinkID, host, port, reconnect);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }
    return true;
}
bool qMQTTCONN(esp8266_obj* nic)
{
	int errcode = 0;
	const char* cmd = "AT+MQTTCONN?";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
	return recvFind(nic,"OK",1000);
}
bool sMQTTPUB(esp8266_obj*nic, uint32_t LinkID,  const char* topic,  const char* data, uint32_t qos, uint32_t retain)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTPUB=%d,\"%s\",\"%s\",%d,%d", LinkID, topic, data, qos, retain);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }
    return true;
}
bool qMQTTSUB(esp8266_obj*nic, uint32_t LinkID, const char* topic, uint32_t qos)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    sprintf(mqtt_cmd, "AT+MQTTSUB=%d,\"%s\",%d", LinkID, topic, qos);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);
    return recvFind(nic,"OK",1000);
}
bool eMQTTSUB_Start(esp8266_obj* nic)
{
	int errcode = 0;
	const char* cmd = "AT+MQTTSUB?";
	const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	rx_empty(nic);
	uart_stream->write(nic->uart_obj,cmd,strlen(cmd),&errcode);
	uart_stream->write(nic->uart_obj,"\r\n",strlen("\r\n"),&errcode);
	return true;
}
bool eMQTTSUB_Get(esp8266_obj* nic, bool* end)
{
    int8_t find;
    *end = false;
    if (recvString_2(nic, "\r\n+MQTTSUB:", "\r\n\r\nOK", 10000, &find) != NULL)
    {
        if( find == 1)
            *end = true;
        return true;
    }
    return false;
}
bool sMQTTUNSUB(esp8266_obj*nic, uint32_t LinkID,  const char* topic)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTUNSUB=%d,\"%s\"", LinkID, topic);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }
    return true;
}
bool sMQTTCLEAN(esp8266_obj*nic, uint32_t LinkID)
{
    int errcode = 0;
    const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
    char mqtt_cmd[128] = {0};

    rx_empty(nic);
    memset(nic->buffer.buffer,0,ESP8266_BUF_SIZE);

    sprintf(mqtt_cmd, "AT+MQTTCLEAN=%d", LinkID);
    uart_stream->write(nic->uart_obj,mqtt_cmd, strlen(mqtt_cmd), &errcode);
    uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);

    if (recvString_1(nic, "\r\nOK", 3000) == NULL)
    {
        return false;
    }
    return true;
}
// bool sMQTTPUBRAW(esp8266_obj*nic, uint32_t LinkID, const char* topic, uint32_t length, uint32_t qos, uint32_t retain)
// {
	// int errcode = 0;
	// const char* cmd = "AT+MQTTPUBRAW=";
	// char len_str[10] = {0};
	// itoa(len,len_str ,10);
	// const mp_stream_p_t * uart_stream = mp_get_stream(nic->uart_obj);
	// rx_empty(nic);
    // sprintf(ap_cmd, "AT+MQTTPUB=%d,\"%s\",%d,%d,%d", LinkID, topic, length, qos, retain);
    // uart_stream->write(nic->uart_obj,ap_cmd, strlen(ap_cmd), &errcode);
    // uart_stream->write(nic->uart_obj, "\r\n", strlen("\r\n"), &errcode);
    // if (recvFind(nic,">", 5000)) {
        // rx_empty(nic);
		// uart_stream->write(nic->uart_obj,buffer,len,&errcode);
        // return recvFind(nic,"SEND OK", timeout);
    // }
    // return false;
// }
//bool setOprToSoftAP(void)
//{
//    char mode;
//    if (!qATCWMODE(&mode)) {
//        return false;
//    }
//    if (mode == 2) {
//        return true;
//    } else {
//        if (sATCWMODE(2) && reset()) {
//            return true;
//        } else {
//            return false;
//        }
//    }
//}

//bool setOprToStationSoftAP(void)
//{
//    char mode;
//    if (!qATCWMODE(&mode)) {
//        return false;
//    }
//    if (mode == 3) {
//        return true;
//    } else {
//        if (sATCWMODE(3) && reset()) {
//            return true;
//        } else {
//            return false;
//        }
//    }
//}

//String getAPList(void)
//{
//    String list;
//    eATCWLAP(list);
//    return list;
//}


//bool setSoftAPParam(String ssid, String pwd, char chl, char ecn)
//{
//    return sATCWSAP(ssid, pwd, chl, ecn);
//}

//String getJoinedDeviceIP(void)
//{
//    String list;
//    eATCWLIF(list);
//    return list;
//}

//bool eATCWLAP(String &list)
//{
//    String data;
//    rx_empty();
//    m_puart->println("AT+CWLAP");
//    return recvFindAndFilter("OK", "\r\r\n", "\r\n\r\nOK", list, 10000);
//}


//bool sATCWSAP(String ssid, String pwd, char chl, char ecn)
//{
//    String data;
//    rx_empty();
//    m_puart->print("AT+CWSAP=\"");
//    m_puart->print(ssid);
//    m_puart->print("\",\"");
//    m_puart->print(pwd);
//    m_puart->print("\",");
//    m_puart->print(chl);
//    m_puart->print(",");
//    m_puart->println(ecn);
//    
//    data = recvString("OK", "ERROR", 5000);
//    if (data.indexOf("OK") != -1) {
//        return true;
//    }
//    return false;
//}

//bool eATCWLIF(String &list)
//{
//    String data;
//    rx_empty();
//    m_puart->println("AT+CWLIF");
//    return recvFindAndFilter("OK", "\r\r\n", "\r\n\r\nOK", list);
//}

