/*
 * Server file fatcher rewrite of dlr/main.c
 * This program connects to a server, downloads a file over HTTP,
 * and saves it locally. (Safe + simplified)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define SERVER_IP   "119.18.54.95"     // Change to your server IP
#define SERVER_PORT 80          // Change to your server port
#define FILE_PATH   "downloaded.bin"
#define REQUEST     "GET /404.html HTTP/1.0\r\n\r\n"

int main() {
    int sockfd, filefd;
    struct sockaddr_in server_addr;
    char buffer[1024];
    int bytes_read;

    printf("Starting download...\n");

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    // Send HTTP GET request
    if (write(sockfd, REQUEST, strlen(REQUEST)) < 0) {
        perror("write");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Open file for writing
    filefd = open(FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (filefd < 0) {
        perror("open");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Skip HTTP header (simple method: find \r\n\r\n)
    char header_buf[4096] = {0};
    int header_len = recv(sockfd, header_buf, sizeof(header_buf), 0);
    char *body_start = strstr(header_buf, "\r\n\r\n");
    if (!body_start) {
        fprintf(stderr, "Invalid HTTP response\n");
        close(sockfd);
        close(filefd);
        exit(EXIT_FAILURE);
    }
    body_start += 4; // Skip header end

    // Write first chunk (after header)
    write(filefd, body_start, header_len - (body_start - header_buf));

    // Write rest of response
    while ((bytes_read = read(sockfd, buffer, sizeof(buffer))) > 0) {
        write(filefd, buffer, bytes_read);
    }

    printf("Download complete. Saved as %s\n", FILE_PATH);

    close(sockfd);
    close(filefd);
    return 0;
}
