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