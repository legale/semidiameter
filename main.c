/*
Verdict:
однопоточный UDP RADIUS proxy без socket-per-request.
используется:
- кольцевой буфер слотов (mapping request -> NAS)
- кольцевой буфер сокетов (pool UDP sockets к серверу)
- epoll
- IP_PKTINFO для сохранения dst ip интерфейса NAS

таймеров нет. старые записи перезаписываются кольцевым буфером.

ограничения:
- поиск slot линейный
- RADIUS корреляция: id + request authenticator (RFC2865)
*/

#define _GNU_SOURCE
#include "proxy_internal.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "md5.h"
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <signal.h>

struct listen_config {
	int fd;
	struct sockaddr_in srv_addr;
};

static int debug_enabled = 0;
static volatile int running = 1;

static void handle_signal(int sig)
{
	(void)sig;
	running = 0;
}

static void log_ts(void)
{
	struct timeval tv;
	struct tm *tm;
	char buf[64];

	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
	fprintf(stderr, "[%s.%06ld] ", buf, tv.tv_usec);
}

#define LOG(...) do { if (debug_enabled) { log_ts(); fprintf(stderr, __VA_ARGS__); } } while (0)

static int set_nonblock(int fd)
{
	int fl;

	fl = fcntl(fd, F_GETFL, 0);
	if (fl < 0)
		return -1;

	return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int mk_udp_pktinfo(struct sockaddr_in *sa)
{
	int fd;
	int one = 1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	if (setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one)) < 0)
		goto err;

	if (bind(fd, (struct sockaddr *)sa, sizeof(*sa)) < 0)
		goto err;

	if (set_nonblock(fd) < 0)
		goto err;

	return fd;

err:
	close(fd);
	return -1;
}

static ssize_t send_to_nas(struct ctx *c, struct slot *s, void *buf, size_t len)
{
	struct msghdr msg;
	struct iovec iov;

	char cbuf[CMSG_SPACE(sizeof(struct in_pktinfo))];
	struct cmsghdr *cmsg;
	struct in_pktinfo *pi;
	ssize_t r;

	memset(&msg, 0, sizeof(msg));
	memset(cbuf, 0, sizeof(cbuf));

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_name = &s->nas;
	msg.msg_namelen = s->nas_len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);

	cmsg = CMSG_FIRSTHDR(&msg);

	cmsg->cmsg_level = IPPROTO_IP;
	cmsg->cmsg_type = IP_PKTINFO;
	cmsg->cmsg_len = CMSG_LEN(sizeof(*pi));

	pi = (struct in_pktinfo *)CMSG_DATA(cmsg);

	memset(pi, 0, sizeof(*pi));

	pi->ipi_spec_dst = s->dst_ip;
	pi->ipi_ifindex = s->ifindex;

	msg.msg_controllen = sizeof(cbuf);

	r = sendmsg(s->nas_fd, &msg, 0);
	if (r < 0) {
		LOG("ERROR: sendmsg back to source failed: %s\n", strerror(errno));
	}
	return r;
}

static void update_msg_auth(u8 *pkt, size_t len, const u8 *req_auth, const char *secret)
{
	if (len < 20) return;
	u8 *attr = pkt + 20;
	u8 *end = pkt + len;

	while (attr + 2 <= end) {
		u8 type = attr[0];
		u8 l = attr[1];
		if (l < 2 || attr + l > end) break;
		if (type == 80 && l == 18) {
			/* Found Message-Authenticator. RFC 3579: Zero it and calculate HMAC-MD5.
			 * For Response packets, the HMAC input MUST use the original Request Authenticator.
			 */
			u8 old_auth[16];
			memcpy(old_auth, pkt + 4, 16);
			memcpy(pkt + 4, req_auth, 16);

			memset(attr + 2, 0, 16);
			HMAC_MD5(pkt, (int)len, (const u8 *)secret, (int)strlen(secret), attr + 2);

			memcpy(pkt + 4, old_auth, 16); /* Restore actual header authenticator */
			return;
		}
		attr += l;
	}
}

static void recalc_req_auth(u8 *pkt, size_t len, const char *secret)
{
	MD5_CTX ctx;
	u8 digest[16];
	u8 zeros[16] = {0};

	/* RFC 2866: Accounting Request Auth = MD5(Code + ID + Length + 16 zero octets + Attributes + Secret) */
	MD5_Init(&ctx);
	MD5_Update(&ctx, pkt, 4);   /* Code, ID, Length */
	MD5_Update(&ctx, zeros, 16); /* Zeros instead of original auth */
	if (len > 20)
		MD5_Update(&ctx, pkt + 20, (unsigned int)(len - 20)); /* Attributes */
	MD5_Update(&ctx, (const u8 *)secret, (unsigned int)(strlen(secret)));
	MD5_Final(digest, &ctx);

	memcpy(pkt + 4, digest, 16);

	/* Update Message-Authenticator if present using the NEW header auth as HMAC input */
	update_msg_auth(pkt, len, pkt + 4, secret);
}

static void handle_req(struct ctx *c, struct listen_config *l_cfg)
{
	int fd = l_cfg->fd;
	u8 buf[MAX_PKT];
	struct sockaddr_storage src;
	struct msghdr msg;
	struct iovec iov;
	char cbuf[CMSG_SPACE(sizeof(struct in_pktinfo))];
	struct cmsghdr *cmsg;
	struct in_pktinfo *pi = NULL;
	ssize_t r;

	memset(&msg, 0, sizeof(msg));
	memset(cbuf, 0, sizeof(cbuf));

	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);

	msg.msg_name = &src;
	msg.msg_namelen = sizeof(src);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);

	r = recvmsg(fd, &msg, 0);

	if (r <= 0)
		return;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_PKTINFO) {
			pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
			break;
		}
	}

	if (!pi)
		return;

	if (r < 20)
		return;

	u8 code = buf[0];
	u8 nas_id = buf[1];
	if (debug_enabled) {
		char src_str[INET_ADDRSTRLEN];
		char dst_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &((struct sockaddr_in *)&src)->sin_addr, src_str, sizeof(src_str));
		inet_ntop(AF_INET, &pi->ipi_addr, dst_str, sizeof(dst_str));

		LOG("REQ: code=%u id=%u from=%s:%d to=%s\n",
		    code, nas_id, src_str, ntohs(((struct sockaddr_in *)&src)->sin_port), dst_str);
	}

	/* RFC Compliance: get real RADIUS length from header */
	u16 rad_len;
	memcpy(&rad_len, buf + 2, 2);
	rad_len = ntohs(rad_len);

	if (rad_len < 20 || rad_len > (u16)r) {
		LOG("ERROR: invalid RADIUS length %u (UDP r=%zd)\n", rad_len, r);
		return;
	}

	u32 slot_idx;
	struct slot *s = slot_alloc(c, &slot_idx, nas_id);

	if (!s) {
		LOG("ERROR: failed to allocate slot for ID %u\n", nas_id);
		return;
	}

	/* Store NAS and destination info */
	memcpy(s->auth, buf + 4, 16);
	s->orig_id = nas_id;
	s->used = 1;
	s->nas_fd = fd;
	memcpy(&s->nas, &src, msg.msg_namelen);
	s->nas_len = msg.msg_namelen;
	s->dst_ip = pi->ipi_addr;
	s->ifindex = pi->ipi_ifindex;
	s->srv_addr = l_cfg->srv_addr;

	/* Determine socket and proxy ID */
	int sock_idx = (int)(slot_idx / 256);
	u8 proxy_id = (u8)(slot_idx % 256);

	int sock = c->srv_sock[sock_idx];
	s->sock = sock;

	if (c->secret) {
		/* REMAPPED MODE: Change ID to proxy's and recalculate sig */
		s->id = proxy_id;
		buf[1] = proxy_id;

		if (code == 4) {
			recalc_req_auth(buf, (size_t)rad_len, c->secret);
		} else if (code == 1) {
			/* Access-Request MAC uses its own (possibly remapped) ID and its header auth */
			update_msg_auth(buf, (size_t)rad_len, buf + 4, c->secret);
		}

		LOG("REQ: code=%u id=%u -> proxy_id=%u via_pool=%d\n",
		    code, nas_id, proxy_id,
		    sock_idx);
	} else {
		/* TRANSPARENT MODE: Preserve original ID.
		 * Collision risk exists if multiple clients use same ID on same pool,
		 * but without secret we have NO CHOICE.
		 */
		s->id = nas_id;
		LOG("REQ: code=%u id=%u (TRANSPARENT) via_pool=%d\n",
		    code, nas_id,
		    sock_idx);
	}

	if (sendto(sock, buf, (size_t)r, 0, (struct sockaddr *)&s->srv_addr, sizeof(s->srv_addr)) < 0) {
		LOG("ERROR: sendto server failed: %s\n", strerror(errno));
	}
}

static void recalc_auth(u8 *pkt, size_t len, const u8 *req_auth, const char *secret)
{
	MD5_CTX ctx;
	u8 digest[16];

	/* RFC 2869: Update Message-Authenticator if present BEFORE recalculating Response Authenticator.
	 * Pass the original Request-Authenticator (req_auth) for the HMAC header field.
	 * update_msg_auth will zero the field, calculate HMAC with req_auth in header, and put it back.
	 */
	update_msg_auth(pkt, len, req_auth, secret);

	/* RFC 2865: Response Auth = MD5(Code + ID + Length + RequestAuth + Attributes + Secret)
	 * The Attributes part now includes the finalized Message-Authenticator.
	 */
	MD5_Init(&ctx);
	MD5_Update(&ctx, pkt, 4);         /* Code, ID, Length */
	MD5_Update(&ctx, req_auth, 16);   /* Request Authenticator */
	if (len > 20)
		MD5_Update(&ctx, pkt + 20, (unsigned int)(len - 20)); /* Attributes */
	MD5_Update(&ctx, (const u8 *)secret, (unsigned int)strlen(secret));
	MD5_Final(digest, &ctx);

	memcpy(pkt + 4, digest, 16);
}

static void handle_resp(struct ctx *c, int fd)
{
	u8 buf[MAX_PKT];
	ssize_t r;
	int sock_idx = -1;

	for (int i = 0; i < c->srv_cnt; i++) {
		if (c->srv_sock[i] == fd) {
			sock_idx = i;
			break;
		}
	}

	if (sock_idx < 0) return;

	r = recv(fd, buf, sizeof(buf), 0);

	if (r < 20)
		return;

	/* RFC Compliance: get real RADIUS length from header */
	u16 rad_len;
	memcpy(&rad_len, buf + 2, 2);
	rad_len = ntohs(rad_len);

	if (rad_len < 20 || rad_len > (u16)r) {
		LOG("ERROR: invalid RESP RADIUS length %u (UDP r=%zd)\n", rad_len, r);
		return;
	}

	u8 resp_id = buf[1];
	struct slot *s = slot_find(c, sock_idx, resp_id);

	if (!s) {
		LOG("RESP: id=%u NOT FOUND (pool=%d)\n", resp_id, sock_idx);
		return;
	}

	if (c->secret) {
		LOG("RESP: id=%u -> orig_id=%u (RESTORED SIG) returning to source\n", resp_id, s->orig_id);
		/* Restore original ID */
		buf[1] = s->orig_id;
		/* Recalculate authenticator using ACTUAL RADIUS length */
		recalc_auth(buf, (size_t)rad_len, s->auth, c->secret);
	} else {
		LOG("RESP: id=%u (TRANSPARENT) returning to source\n", resp_id);
	}

	send_to_nas(c, s, buf, (size_t)r);
	s->used = 0;
}

static int parse_ip_port(const char *s, struct sockaddr_in *sa)
{
	char tmp[128];
	char *p;

	memset(sa, 0, sizeof(*sa));

	sa->sin_family = AF_INET;

	strncpy(tmp, s, sizeof(tmp) - 1);

	p = strrchr(tmp, ':');

	if (!p)
		return -1;

	*p++ = 0;

	sa->sin_port = htons((uint16_t)atoi(p));

	if (inet_pton(AF_INET, tmp, &sa->sin_addr) != 1)
		return -1;

	return 0;
}

#define MAX_LISTENERS 4
#define DEFAULT_SRV_POOLS 4 /* Each pool socket provides 256 unique RADIUS IDs for inflight requests */

int main(int argc, char **argv)
{
	struct ctx c;
	int opt;
	struct listen_config listeners[MAX_LISTENERS];
	int listen_cnt = 0;

	memset(&c, 0, sizeof(c));

	c.srv_cnt = DEFAULT_SRV_POOLS;
	c.ring_sz = c.srv_cnt * 256;

	while ((opt = getopt(argc, argv, "s:n:d")) != -1) {
		switch (opt) {
		case 's':
			c.secret = optarg;
			break;
		case 'n':
			c.srv_cnt = atoi(optarg);
			if (c.srv_cnt > 8) c.srv_cnt = 8;
			break;
		case 'd':
			debug_enabled = 1;
			break;
		default:
			goto usage;
		}
	}

	if (argc - optind < 2 || (argc - optind) % 2 != 0) {
usage:
		printf("usage: %s [-s secret] [-n pools] [-d] listen_ip:port radius_ip:port [listen_ip:port radius_ip:port ...]\n", argv[0]);
		return 1;
	}

	c.ep = epoll_create1(0);
	if (c.ep < 0) {
		perror("epoll_create");
		return 1;
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	for (int i = optind; i < argc; i += 2) {
		if (listen_cnt >= MAX_LISTENERS) break;

		struct sockaddr_in listen_addr;
		if (parse_ip_port(argv[i], &listen_addr)) return 1;
		if (parse_ip_port(argv[i+1], &listeners[listen_cnt].srv_addr)) return 1;

		int fd = mk_udp_pktinfo(&listen_addr);
		if (fd < 0) {
			perror("listen socket");
			return 1;
		}
		listeners[listen_cnt].fd = fd;

		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.fd = fd;
		if (epoll_ctl(c.ep, EPOLL_CTL_ADD, fd, &ev) < 0) {
			perror("epoll_ctl listen");
			return 1;
		}

		LOG("INIT: listen_port=%s, radius_server=%s\n", argv[i], argv[i+1]);
		listen_cnt++;
	}

	LOG("START: secret=%s, pools=%d\n", c.secret ? "****" : "none", c.srv_cnt);

	c.ring = calloc((size_t)c.ring_sz, sizeof(struct slot));
	c.srv_sock = calloc((size_t)c.srv_cnt, sizeof(int));

	for (int i = 0; i < c.srv_cnt; i++) {
		struct sockaddr_in b;
		memset(&b, 0, sizeof(b));
		b.sin_family = AF_INET;
		b.sin_port = 0;
		b.sin_addr.s_addr = INADDR_ANY;

		int fd = mk_udp_pktinfo(&b);
		if (fd < 0) {
			perror("srv socket");
			return 1;
		}

		c.srv_sock[i] = fd;

		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.fd = fd;
		epoll_ctl(c.ep, EPOLL_CTL_ADD, fd, &ev);
	}

	struct epoll_event events[MAX_EVENTS];

	while (running) {
		int n = epoll_wait(c.ep, events, MAX_EVENTS, 100);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		for (int i = 0; i < n; i++) {
			int fd = events[i].data.fd;
			struct listen_config *l_ptr = NULL;

			for (int j = 0; j < listen_cnt; j++) {
				if (fd == listeners[j].fd) {
					l_ptr = &listeners[j];
					break;
				}
			}

			if (l_ptr) {
				handle_req(&c, l_ptr);
			} else {
				handle_resp(&c, fd);
			}
		}
	}

	LOG("EXIT: cleaning up...\n");
	for (int i = 0; i < listen_cnt; i++) close(listeners[i].fd);
	for (int i = 0; i < c.srv_cnt; i++) close(c.srv_sock[i]);
	close(c.ep);
	free(c.ring);
	free(c.srv_sock);

	return 0;
}