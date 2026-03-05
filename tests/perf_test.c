#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define NUM_REQUESTS 100000

static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static int qsort_cmp(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

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

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)proxy_port);
    inet_pton(AF_INET, proxy_ip, &target.sin_addr);

    struct timeval tv_timeout;
    tv_timeout.tv_sec = 1;
    tv_timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv_timeout, sizeof(tv_timeout));

    unsigned char pkt[20];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1;
    pkt[2] = 0;
    pkt[3] = 20;

    double *latencies = malloc(sizeof(double) * (size_t)num_requests);
    int received = 0;

    printf("Starting performance test: %d requests\n", num_requests);

    double start_gen = get_time();
    double total_latency = 0;

    for (int i = 0; i < num_requests; i++) {
        pkt[1] = (unsigned char)(i & 0xFF);
        memcpy(pkt + 4, &i, sizeof(i));

        double t1 = get_time();
        sendto(fd, pkt, sizeof(pkt), 0,
               (struct sockaddr *)&target, sizeof(target));

        unsigned char resp[20];
        if (recv(fd, resp, sizeof(resp), 0) > 0) {
            double t2 = get_time();
            double lat = t2 - t1;
            latencies[received++] = lat;
            total_latency += lat;
        }
    }

    double end_gen = get_time();
    double duration = end_gen - start_gen;

    if (received > 0) {
        qsort(latencies, (size_t)received, sizeof(double), qsort_cmp);
        printf("requests: %d\n", received);
        printf("time: %.1f sec\n", duration);
        printf("throughput: %.0f req/sec\n", (double)received / duration);
        printf("latency avg: %.6f sec\n", total_latency / (double)received);
        printf("latency p99: %.6f sec\n",
               latencies[(int)((double)received * 0.99)]);
    } else {
        printf("No responses received\n");
    }

    free(latencies);
    close(fd);
    return 0;
}
