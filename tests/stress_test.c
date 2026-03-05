#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NUM_REQUESTS 100000
#define NUM_CLIENTS 10

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s proxy_ip proxy_port\n", argv[0]);
        return 1;
    }

    const char *proxy_ip = argv[1];
    int proxy_port = atoi(argv[2]);
    int num_requests = NUM_REQUESTS;
    if (argc > 3) {
        num_requests = atoi(argv[3]);
    }

    int socks[NUM_CLIENTS];
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)proxy_port);
    inet_pton(AF_INET, proxy_ip, &target.sin_addr);

    for (int i = 0; i < NUM_CLIENTS; i++) {
        socks[i] = socket(AF_INET, SOCK_DGRAM, 0);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(socks[i], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    unsigned char pkt[20];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1; /* Access-Request */
    pkt[3] = 20; /* Length LSB (for 20 bytes) */

    int sent = 0;
    int received = 0;

    printf("Starting stress test: %d requests through %d clients\n",
           num_requests, NUM_CLIENTS);
 
    for (int i = 0; i < num_requests; i++) {
        int s_idx = i % NUM_CLIENTS;
        pkt[1] = (unsigned char)(i & 0xFF);
        memcpy(pkt + 4, &i, sizeof(i));
 
        ssize_t sr = sendto(socks[s_idx], pkt, sizeof(pkt), 0,
                            (struct sockaddr *)&target, sizeof(target));
        if (sr > 0)
            sent++;
 
        unsigned char resp[20];
        ssize_t rr = recv(socks[s_idx], resp, sizeof(resp), 0);
        if (rr > 0)
            received++;
 
        if (i % 10000 == 0 && i > 0)
            printf("Progress: %d/%d\n", i, num_requests);
    }

    printf("Sent: %d, Received: %d\n", sent, received);

    for (int i = 0; i < NUM_CLIENTS; i++)
        close(socks[i]);

    if (sent == received) {
        printf("Stress test PASSED\n");
        return 0;
    }

    printf("Stress test FAILED: packet loss detected (sent %d != received %d)\n",
           sent, received);
    return 1;
}
