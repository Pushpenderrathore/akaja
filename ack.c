#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#define NUM_THREADS 10
#define PORT 80

typedef struct {
    int thread_id;
    struct sockaddr_in addr;
} thread_args;

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;
    for (sum = 0; len > 1; len -= 2) sum += *buf++;
    if (len == 1) sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

void *send_ack(void *arg) {
    thread_args *args = (thread_args *)arg;
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sockfd < 0) {
        perror("socket");
        pthread_exit(NULL);
    }

    struct tcphdr tcp_header;
    memset(&tcp_header, 0, sizeof(tcp_header));
    tcp_header.source = htons(PORT);
    tcp_header.dest = htons(PORT);
    tcp_header.seq = htonl(1);
    tcp_header.ack_seq = htonl(1);
    tcp_header.doff = 5;
    tcp_header.ack = 1;
    tcp_header.window = htons(5840);
    tcp_header.check = checksum(&tcp_header, sizeof(tcp_header));

    if (sendto(sockfd, &tcp_header, sizeof(tcp_header), 0,
               (struct sockaddr *)&args->addr, sizeof(args->addr)) <= 0) {
        perror("sendto");
    }

    close(sockfd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <IP>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    pthread_t threads[NUM_THREADS];
    thread_args args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].addr = addr;
        if (pthread_create(&threads[i], NULL, send_ack, &args[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
