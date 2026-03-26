#ifndef PEER_H
#define PEER_H

#include "../include/parser.h"
#include "../include/tracker.h"
#include <stddef.h>
#include <stdint.h>

// Message IDS (BEP 3)
#define MSG_CHOKE        0
#define MSG_UNCHOKE      1
#define MSG_INTERESTED   2
#define MSG_NOT_INTEREST 3
#define MSG_HAVE         4
#define MSG_BITFIELD     5
#define MSG_REQUEST      6
#define MSG_PIECE        7
#define MSG_CANCEL       8

// Block Size: 16KB is the std request size
#define BLOCK_SIZE      16384 // 0x4000
#define PEER_TIMEOUT_MS 5000  // How long to wait for a response from peer

// Bitfield Helpers

// Allocates a zeroed bitfield for num_pieces pieces
uint8_t *bitfield_create(size_t num_pieces);

// Returns 1 if the peer has the given piece, 0 otherwise
int bitfield_has_piece(const uint8_t *bitfield, size_t piece_index);

// Sets a bit in the bitfield when you download a piece
void bitfield_set_piece(uint8_t *bitfield, size_t piece_index);

// Returns number of bytes needed to store num_pieces bits
size_t bitfield_size(size_t num_pieces);

// PeerMessage :- parsed message from the peer wire protocol

typedef struct {
  uint8_t id;       // message type (MSG_* constants defined above)
  uint8_t *payload; // heap allocated, NULL  for messages with no payload
  uint32_t payload_len;
} PeerMessage;

// PeerConnection :- All states for a single peer TCP connection

typedef struct {
  // Network
  SOCKET sock;
  uint32_t ip;
  uint16_t port;

  // Handshake state
  int handshake_done;

  //  Peer's choke/interest state
  int peer_choking;    // 1 = peer is choking us (can't request)
  int peer_interested; // 1 = peer is interested in us (yay!!)

  // Our choke/interest state
  int am_choking;    // 1 = we are choking the peer
  int am_interested; // 1 = we are interested in the peer

  // Which pieces the peer has
  uint8_t *bitfield;
  size_t bitfield_bytes;
} PeerConnection;

#endif