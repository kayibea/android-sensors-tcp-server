#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"

typedef struct __attribute__((packed)) {
  float x, y, z;
} pkt_accel_t;

int main(void) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
    perror("inet_pton");
    return 1;
  }

  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    return 1;
  }

  printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

  pkt_accel_t pkt;
  while (1) {
    ssize_t n = recv(sock, &pkt, sizeof(pkt), 0);
    if (n == 0) {
      printf("Server closed connection\n");
      break;
    } else if (n < 0) {
      perror("recv");
      break;
    } else if (n < (ssize_t)sizeof(pkt)) {
      continue;
    }

    printf("Accel: x=%f y=%f z=%f\n", pkt.x, pkt.y, pkt.z);
    fflush(stdout);
  }

  close(sock);
  return 0;
}
