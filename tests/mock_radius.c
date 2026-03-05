#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s port\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    printf("Mock RADIUS server listening on port %d\n", port);

    unsigned char buf[4096];
    struct sockaddr_in src;
    socklen_t slen;

    while (1) {
        slen = sizeof(src);
        ssize_t r = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &slen);
        if (r < 0) break;

        sendto(fd, buf, (size_t)r, 0, (struct sockaddr *)&src, slen);
    }

    close(fd);
    return 0;
}
