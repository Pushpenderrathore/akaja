#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/select.h> 
#include <sys/epoll.h>
#include <poll.h> 
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>

// Calculate checksum for ICMP packet (required for ICMP protocol)
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;
    
    // Sum all 16-bit words in the buffer
    for (sum = 0; len > 1; len -= 2) {
        sum += *buf++;
    }
    
    // Add left-over byte, if any (for odd-length buffers)
    if (len == 1) {
        sum += *(unsigned char *)buf;
    }
    
    // Fold 32-bit sum to 16 bits (carry bits)
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum; // One's complement
    
    return result;
}

// Function to send ICMP echo request packet
int send_ping(int sockfd, struct sockaddr_in *addr, int seq) {
    struct icmp icmp_pkt;
    struct timeval tv;
    
    // Initialize ICMP packet structure
    memset(&icmp_pkt, 0, sizeof(icmp_pkt));
    icmp_pkt.icmp_type = ICMP_ECHO;  // Echo request type
    icmp_pkt.icmp_code = 0;          // Code 0 for echo request
    icmp_pkt.icmp_id = getpid() & 0xFFFF; // Use process ID as identifier
    icmp_pkt.icmp_seq = seq;         // Sequence number
    
    // Get current time for timestamp (used to calculate round-trip time)
    gettimeofday(&tv, NULL);
    memcpy(icmp_pkt.icmp_data, &tv, sizeof(tv));
    
    // Calculate checksum (must be 0 before calculation)
    icmp_pkt.icmp_cksum = 0;
    icmp_pkt.icmp_cksum = checksum(&icmp_pkt, sizeof(icmp_pkt));
    
    // Send the packet using raw socket
    if (sendto(sockfd, &icmp_pkt, sizeof(icmp_pkt), 0, 
               (struct sockaddr *)addr, sizeof(*addr)) <= 0) {
        perror("sendto failed");
        return -1;
    }
    
    return 0;
}

// Function to receive ICMP echo reply
int recv_ping(int sockfd, struct sockaddr_in *addr, int seq) {
    char recvbuf[1024];              // Buffer for received packet
    struct ip *ip_hdr;               // IP header structure
    struct icmp *icmp_hdr;           // ICMP header structure
    struct timeval tv_recv, tv_sent; // Time values for RTT calculation
    socklen_t addr_len = sizeof(*addr);
    int bytes_received;
    
    // Set timeout for receiving (1 second)
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Receive packet from socket
    bytes_received = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0, 
                             (struct sockaddr *)addr, &addr_len);
    
    if (bytes_received < 0) {
        // Handle timeout or other errors
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Request timeout for icmp_seq %d\n", seq);
        } else {
            perror("recvfrom failed");
        }
        return -1;
    }
    
    // Parse IP header (located at start of received buffer)
    ip_hdr = (struct ip *)recvbuf;
    int ip_hdr_len = ip_hdr->ip_hl * 4; // Header length in bytes
    
    // Parse ICMP header (after IP header)
    icmp_hdr = (struct icmp *)(recvbuf + ip_hdr_len);
    
    // Check if it's an echo reply and matches our process ID
    if (icmp_hdr->icmp_type == ICMP_ECHOREPLY && 
        icmp_hdr->icmp_id == (getpid() & 0xFFFF)) {
        
        // Extract sent time from packet data
        memcpy(&tv_sent, icmp_hdr->icmp_data, sizeof(tv_sent));
        
        // Get current receive time
        gettimeofday(&tv_recv, NULL);
        
        // Calculate round-trip time in milliseconds
        double rtt = (tv_recv.tv_sec - tv_sent.tv_sec) * 1000.0;
        rtt += (tv_recv.tv_usec - tv_sent.tv_usec) / 1000.0;
        
        // Print ping result (similar to standard ping output)
        printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms\n",
               bytes_received - ip_hdr_len,    // ICMP payload size
               inet_ntoa(addr->sin_addr),     // Destination IP
               icmp_hdr->icmp_seq,            // Sequence number
               ip_hdr->ip_ttl,                // Time to live
               rtt);                          // Round-trip time
        
        return 0;
    }
    
    return -1; // Not our packet or wrong type
}

int main(int argc, char *argv[]) {
    int sockfd;                     // Socket file descriptor
    struct sockaddr_in addr;        // Destination address structure
    int seq = 0;                    // Sequence counter
    
    // Validate command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname/IP>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Create raw socket for ICMP protocol (requires root privileges)
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket creation failed");
        fprintf(stderr, "Note: Raw sockets usually require root privileges\n");
        exit(EXIT_FAILURE);
    }
    
    // Initialize destination address structure
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; // IPv4
    
    // Convert IP address string to network format
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", argv[1]);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("PING %s (%s):\n", argv[1], inet_ntoa(addr.sin_addr));
    
    // Main ping loop - sends and receives packets continuously
    while (1) {
        // Send ping request
        if (send_ping(sockfd, &addr, seq) == 0) {
            // Try to receive ping reply
            recv_ping(sockfd, &addr, seq);
        }
        
        seq++; // Increment sequence number
        sleep(1); // Wait 1 second between pings
    }
    
    close(sockfd); // Clean up socket (though we never reach this in infinite loop)
    return 0;
}
