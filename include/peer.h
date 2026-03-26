#ifndef PEER_H
#define PEER_H

#include <winsock2.h>
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

// ============================================================
//  API
// ============================================================

/*
 * peer_connect
 * Opens a TCP connection to the given peer.
 * Returns a heap-allocated PeerConnection on success, NULL on failure.
 */
PeerConnection *peer_connect(uint32_t ip, uint16_t port);

/*
 * peer_handshake
 * Sends our handshake and receives + validates the peer's handshake.
 * Returns 0 on success, -1 on failure (wrong info_hash, timeout, etc.)
 */
int peer_handshake(PeerConnection *conn, const uint8_t info_hash[20],
                   const uint8_t peer_id[20]);

/*
 * peer_send_message
 * Serialises and sends a message to the peer.
 * For messages with no payload (choke, unchoke, interested, not_interested)
 * pass payload=NULL and payload_len=0.
 */
int peer_send_message(PeerConnection *conn, uint8_t msg_id,
                      const uint8_t *payload, uint32_t payload_len);

/*
 * peer_recv_message
 * Reads one complete message from the peer.
 * Returns a heap-allocated PeerMessage on success, NULL on timeout/error.
 * Caller must free msg->payload and the PeerMessage itself.
 */
PeerMessage *peer_recv_message(PeerConnection *conn);

/*
 * peer_request_block
 * Sends a REQUEST message for a specific block.
 *   piece_index : which piece
 *   offset      : byte offset within that piece (multiple of BLOCK_SIZE)
 *   length      : number of bytes to request (usually BLOCK_SIZE)
 */
int peer_request_block(PeerConnection *conn, uint32_t piece_index,
                       uint32_t offset, uint32_t length);

/*
 * peer_download_piece
 * High-level: requests all blocks of a piece and assembles them into
 * a caller-supplied buffer. Buffer must be at least piece_length bytes.
 * Returns 0 on success, -1 on failure.
 */
int peer_download_piece(PeerConnection *conn, uint32_t piece_index,
                        uint32_t piece_length, uint8_t *out_buf);

/*
 * peer_disconnect
 * Closes the socket and frees all memory for the connection.
 */
void peer_disconnect(PeerConnection *conn);

/*
 * free_peer_message
 * Frees a PeerMessage returned by peer_recv_message.
 */
void free_peer_message(PeerMessage *msg);

#endif