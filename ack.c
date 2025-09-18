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

// TCP pseudo header for checksum
struct pseudo_header {
    uint32_t src_addr;
    uint32_t dest_addr;
    uint8_t placeholder;
    uint8_t protocol;
    uint16_t tcp_length;
};

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

    int one = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt");
        close(sockfd);
        pthread_exit(NULL);
    }

    // Allocate buffer for IP header + TCP header
    char packet[sizeof(struct iphdr) + sizeof(struct tcphdr)];
    memset(packet, 0, sizeof(packet));

    struct iphdr *ip_header = (struct iphdr *)packet;
    struct tcphdr *tcp_header = (struct tcphdr *)(packet + sizeof(struct iphdr));

    // Fill IP header
    ip_header->ihl = 5;
    ip_header->version = 4;
    ip_header->tos = 0;
    ip_header->tot_len = htons(sizeof(struct iphdr) + sizeof(struct tcphdr));
    ip_header->id = htons(rand() % 65535);
    ip_header->frag_off = 0;
    ip_header->ttl = 64;
    ip_header->protocol = IPPROTO_TCP;
    ip_header->check = 0; // kernel will not fill this since IP_HDRINCL is set

    // You must set a valid source IP — using 127.0.0.1 for now
    inet_pton(AF_INET, "127.0.0.1", &ip_header->saddr);
    ip_header->daddr = args->addr.sin_addr.s_addr;

    ip_header->check = checksum((unsigned short *)ip_header, sizeof(struct iphdr));

    // Fill TCP header
    tcp_header->source = htons(PORT);
    tcp_header->dest = htons(PORT);
    tcp_header->seq = htonl(1);
    tcp_header->ack_seq = htonl(1);
    tcp_header->doff = 5;
    tcp_header->ack = 1;
    tcp_header->window = htons(5840);
    tcp_header->check = 0;

    // Pseudo header + TCP header for checksum
    struct pseudo_header psh;
    psh.src_addr = ip_header->saddr;
    psh.dest_addr = ip_header->daddr;
    psh.placeholder = 0;
    psh.protocol = IPPROTO_TCP;
    psh.tcp_length = htons(sizeof(struct tcphdr));

    char pseudo_packet[sizeof(struct pseudo_header) + sizeof(struct tcphdr)];
    memcpy(pseudo_packet, &psh, sizeof(struct pseudo_header));
    memcpy(pseudo_packet + sizeof(struct pseudo_header), tcp_header, sizeof(struct tcphdr));

    tcp_header->check = checksum((unsigned short *)pseudo_packet, sizeof(pseudo_packet));

    if (sendto(sockfd, packet, sizeof(packet), 0,
               (struct sockaddr *)&args->addr, sizeof(args->addr)) <= 0) {
        perror("sendto");
    }

    close(sockfd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Target IP>\n", argv[0]);
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
