/*
 * proxy.h — public types for RADIUS proxy
 */
#ifndef PROXY_H
#define PROXY_H

#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define MAX_PKT 4096
#define MAX_EVENTS 64

struct slot {
	u8 id;
	u8 orig_id;
	u8 auth[16];

	struct sockaddr_storage nas;
	socklen_t nas_len;

	struct in_addr dst_ip;
	struct sockaddr_in srv_addr;
	int ifindex;

	int nas_fd;
	int sock;
	int used;
};

struct ctx {
	int ep;
	int nas_fd;

	int *srv_sock;
	int srv_cnt;
	int srv_head;

	struct slot *ring;
	int ring_sz;
	u32 ring_head;

	struct sockaddr_in srv_addr;

	char *secret;
};

#endif /* PROXY_H */
