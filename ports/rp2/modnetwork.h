/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MICROPY_INCLUDED_MAIX_MODNETWORK_H
#define MICROPY_INCLUDED_MAIX_MODNETWORK_H

#define MOD_NETWORK_IPADDR_BUF_SIZE (4)

#define MOD_NETWORK_AF_INET (2)
#define MOD_NETWORK_AF_INET6 (10)

#define MOD_NETWORK_SOCK_STREAM (1)
#define MOD_NETWORK_SOCK_DGRAM (2)
#define MOD_NETWORK_SOCK_RAW (3)

#if MICROPY_PY_LWIP

struct netif;

typedef struct _mod_network_nic_type_t {
    mp_obj_base_t base;
    void (*poll_callback)(void *data, struct netif *netif);
} mod_network_nic_type_t;

extern const mp_obj_type_t mod_network_nic_type_wiznet5k;

mp_obj_t mod_network_nic_ifconfig(struct netif *netif, size_t n_args, const mp_obj_t *args);

#else

struct _mod_network_socket_obj_t;
struct _mqtt_obj_t;
struct _mqtt_msg;
typedef struct _mod_network_nic_type_t {
    mp_obj_type_t base;

    // API for non-socket operations
    int (*gethostbyname)(mp_obj_t nic, const char *name, mp_uint_t len, uint8_t *ip_out);

    // API for socket operations; return -1 on error
    int (*socket)(struct _mod_network_socket_obj_t *socket, int *_errno);
    void (*close)(struct _mod_network_socket_obj_t *socket);
    int (*bind)(struct _mod_network_socket_obj_t *socket, byte *ip, mp_uint_t port, int *_errno);
    int (*listen)(struct _mod_network_socket_obj_t *socket, mp_int_t backlog, int *_errno);
    int (*accept)(struct _mod_network_socket_obj_t *socket, struct _mod_network_socket_obj_t *socket2, byte *ip, mp_uint_t *port, int *_errno);
    int (*connect)(struct _mod_network_socket_obj_t *socket, byte *ip, mp_uint_t port, int *_errno);
    mp_uint_t (*send)(struct _mod_network_socket_obj_t *socket, const byte *buf, mp_uint_t len, int *_errno);
    mp_uint_t (*recv)(struct _mod_network_socket_obj_t *socket, byte *buf, mp_uint_t len, int *_errno);
    mp_uint_t (*sendto)(struct _mod_network_socket_obj_t *socket, const byte *buf, mp_uint_t len, byte *ip, mp_uint_t port, int *_errno);
    mp_uint_t (*recvfrom)(struct _mod_network_socket_obj_t *socket, byte *buf, mp_uint_t len, byte *ip, mp_uint_t *port, int *_errno);
    int (*setsockopt)(struct _mod_network_socket_obj_t *socket, mp_uint_t level, mp_uint_t opt, const void *optval, mp_uint_t optlen, int *_errno);
    int (*settimeout)(struct _mod_network_socket_obj_t *socket, mp_uint_t timeout_ms, int *_errno);
    int (*ioctl)(struct _mod_network_socket_obj_t *socket, mp_uint_t request, mp_uint_t arg, int *_errno);
	int (*mqtt)(struct _mqtt_obj_t *mqtt, int *_errno);
	int (*mqtt_setcfg)(struct _mqtt_obj_t *mqtt, const char* client_id, const char* username, const char* password, int cert_key_ID, int CA_ID, const char* path);
	int (*mqtt_set_last_will)(struct _mqtt_obj_t *mqtt, const char *name, mp_uint_t len, uint8_t *out_ip);
	int (*mqtt_connect)(struct _mqtt_obj_t *mqtt, const char *name, mp_uint_t len, uint8_t *out_ip);
	int (*mqtt_disconnect)(struct _mqtt_obj_t *mqtt);
	int (*mqtt_ping)(struct _mqtt_obj_t *mqtt, const char *name, mp_uint_t len, uint8_t *out_ip);
	int (*mqtt_publish)(struct _mqtt_obj_t *mqtt, const char *topic, const char *data, uint8_t qos, uint8_t retain);
	int (*mqtt_subscribe)(struct _mqtt_obj_t *mqtt, const char *topic, uint8_t qos);
	int (*mqtt_wait_msg)(struct _mqtt_obj_t *mqtt,struct _mqtt_msg *mqttmsg);
	int (*mqtt_check_msg)(struct _mqtt_obj_t *mqtt, const char *name, mp_uint_t len, uint8_t *out_ip);

} mod_network_nic_type_t;

typedef struct _mod_network_socket_obj_t {
    mp_obj_base_t base;
    mp_obj_t nic;
    mod_network_nic_type_t *nic_type;
    union {
        struct {
            uint8_t domain;
            uint8_t type;
            int8_t fileno;
        } u_param;
        mp_uint_t u_state;
    };
    int8_t fd;
    float timeout;
    bool peer_closed;
    bool first_read_after_write;
} mod_network_socket_obj_t;
typedef void (*mqtt_callback)(const char * topic, const char * msg);
typedef struct _mqtt_obj_t {
    mp_obj_base_t base;
    mp_obj_t nic;
    mod_network_nic_type_t *nic_type;	
	int LinkID;
    char *client_id;
    char *server;
    uint32_t port;
    char *user;
    char *password;
	int keepalive ;
    int ssl;
	char *ssl_params;
	int timeout;
	mqtt_callback mqtt_callback_fn;
} mqtt_obj_t;
typedef struct _mqtt_msg
{
	mp_obj_t topic;
	mp_obj_t msg;
}mqtt_msg;

extern const mod_network_nic_type_t mod_network_nic_type_esp8285;
extern const mod_network_nic_type_t mod_network_nic_type_esp8266;

#endif

void mod_network_init(void);
void mod_network_deinit(void);
void mod_network_register_nic(mp_obj_t nic);
mp_obj_t mod_network_find_nic(const uint8_t *ip);

#endif // MICROPY_INCLUDED_MAIX_MODNETWORK_H
