#include "../include/peer.h"
#include "../include/sha1.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------Internal send/recv helpers------------
// Winsock's send/recv may transfer fewer bytes than requested
// in one call there wrappers loop untill all bytes are done.

static int send_all(SOCKET sock, const uint8_t *buf, int len)
{
  int total = 0;
  while (total < len) {
    int n = send(sock, (const char *)(buf + total), len - total, 0);
    if (n == SOCKET_ERROR || n == 0) {
      fprintf(stderr, "peer: send failed: %d\n", WSAGetLastError());
      return -1;
    }
    total += n;
  }

  return 0;
}

static int recv_all(SOCKET sock, uint8_t *buf, int len)
{
  int total = 0;
  while (total < len) {
    int n = recv(sock, (char *)(buf + total), len - total, 0);
    if (n == SOCKET_ERROR || n == 0) {
      if (n == 0)
        fprintf(stderr, "peer: connection closed by peer\n");
      else
        fprintf(stderr, "peer: recv failed: %d\n", WSAGetLastError());

      return -1;
    }

    total += n;
  }
  return 0;
}

// ----------Bitfield helpers-----------

size_t bitfield_size(size_t num_pieces)
{
  // ceil (num_pieces / 8)
  return (num_pieces + 7) / 8;
}

uint8_t *bitfield_create(size_t num_pieces)
{
  size_t sz = bitfield_size(num_pieces);
  return (uint8_t *)calloc(sz, 1);
}

int bitfield_has_piece(const uint8_t *bitfield, size_t piece_index)
{
  size_t byte = piece_index / 8;
  int bit = 7 - (int)(piece_index % 8);
  return (bitfield[byte] >> bit) & 1;
}

void bitfield_set_piece(uint8_t *bitfield, size_t piece_index)
{
  size_t byte = piece_index / 8;
  int bit = 7 - (int)(piece_index % 8);
  bitfield[byte] |= (1 << bit);
}

//----------Peer Connect----------

PeerConnection *peer_connect(uint32_t ip, uint16_t port)
{
  // Host byte order IP to string for getaddrinfo
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    fprintf(stderr, "peer: socket() failed: %d\n", WSAGetLastError());
    return NULL;
  }

  // Set connect + send/recv timeout
  DWORD timeout = PEER_TIMEOUT_MS;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
             sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
             sizeof(timeout));

  // Format IP for logging
  uint8_t a = (ip >> 24) & 0xFF, b = (ip >> 16) && 0xFF, c = (ip >> 8) & 0xFF,
          d = ip & 0xFF;

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    fprintf(stderr, "peer: connect to %u.%u.%u.%u:%u failed: %d\n", a, b, c, d,
            port, WSAGetLastError());
    closesocket(sock);
    return NULL;
  }

  printf("peer: connected to %u.%u.%u.%u:%u\n", a, b, c, d, port);

  PeerConnection *conn = (PeerConnection *)calloc(1, sizeof(PeerConnection));

  if (!conn) {
    closesocket(sock);
    return NULL;
  }

  conn->sock = sock;
  conn->ip = ip;
  conn->port = port;
  conn->peer_choking = 1; // peer starts choking us by default (BEP 3)
  conn->am_choking = 1;   // we start choking them too
  return conn;
}

// ----------Peer Handshake----------
//  Handshake layout (68 bytes total):
//   [0]      pstrlen = 19
//   [1..19]  pstr    = "BitTorrent protocol"
//   [20..27] reserved (8 zero bytes)
//   [28..47] info_hash (20 bytes)
//   [48..67] peer_id   (20 bytes)

int peer_handshake(PeerConnection *conn, const uint8_t info_hash[20],
                   const uint8_t peer_id[20])
{
  uint8_t handshake[68];
  memset(handshake, 0, sizeof(handshake));

  handshake[0] = 19;                                // pstrlen
  memcpy(handshake + 1, "BitTorrent protocol", 19); // pstr
  // bytes 20-27 stay zero
  memcpy(handshake + 28, info_hash, 20); // info_hash
  memcpy(handshake + 48, peer_id, 20);   // peer_id

  // Send our handshake
  if (send_all(conn->sock, handshake, 68) != 0) {
    fprintf(stderr, "peer: failed to send handshake\n");
    return -1;
  }

  // Receive their handshake
  uint8_t resp[68];
  if (recv_all(conn->sock, resp, 68) != 0) {
    fprintf(stderr, "peer: failed to receive handshake\n");
    return -1;
  }

  // Validate pstrlen
  if (resp[0] != 19) {
    fprintf(stderr, "peer: bad pstrlen in handshake: %d\n", resp[0]);
    return -1;
  }

  // Validate protocol string
  if (memcmp(resp + 1, "BitTorrent protocol", 19) != 0) {
    fprintf(stderr, "peer: protocol string mismatch\n");
    return -1;
  }

  // Validate info_hash : must match our torrent else stuff goes brrrrr
  if (memcmp(resp + 28, info_hash, 20) != 0) {
    fprintf(stderr, "peer: info_hash mismatch — wrong torrent\n");
    return -1;
  }

  conn->handshake_done = 1;
  printf("peer:handshake successful\n");
  return 0;
}
