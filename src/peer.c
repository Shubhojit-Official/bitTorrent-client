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
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  uint8_t a = (ip >> 24) & 0xFF, b = (ip >> 16) & 0xFF, c = (ip >> 8) & 0xFF,
          d = ip & 0xFF;

  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET)
    return NULL;

  // Set socket to non-blocking mode
  u_long mode = 1;
  ioctlsocket(sock, FIONBIO, &mode);

  // connect() returns immediately on non-blocking socket
  connect(sock, (struct sockaddr *)&addr, sizeof(addr));

  // Use select() to wait up to 1 second for the connection
  fd_set write_fds;
  FD_ZERO(&write_fds);
  FD_SET(sock, &write_fds);

  struct timeval timeout;
  timeout.tv_sec = 1; // 1 second connect timeout
  timeout.tv_usec = 0;

  int result = select(0, NULL, &write_fds, NULL, &timeout);

  if (result <= 0) {
    // 0 = timeout, -1 = error
    fprintf(stderr, "peer: connect to %u.%u.%u.%u:%u timed out\n", a, b, c, d,
            port);
    closesocket(sock);
    return NULL;
  }

  // Check if the connection actually succeeded
  int err = 0;
  int errlen = sizeof(err);
  getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen);
  if (err != 0) {
    fprintf(stderr, "peer: connect to %u.%u.%u.%u:%u failed: %d\n", a, b, c, d,
            port, err);
    closesocket(sock);
    return NULL;
  }

  // Switch back to blocking mode for normal send/recv
  mode = 0;
  ioctlsocket(sock, FIONBIO, &mode);

  // Set send/recv timeout for data transfer
  DWORD rw_timeout = PEER_TIMEOUT_MS;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rw_timeout,
             sizeof(rw_timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&rw_timeout,
             sizeof(rw_timeout));

  printf("peer: connected to %u.%u.%u.%u:%u\n", a, b, c, d, port);

  PeerConnection *conn = (PeerConnection *)calloc(1, sizeof(PeerConnection));
  if (!conn) {
    closesocket(sock);
    return NULL;
  }

  conn->sock = sock;
  conn->ip = ip;
  conn->port = port;
  conn->peer_choking = 1;
  conn->am_choking = 1;
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

// -----------Peer Send Message-----------
//  Wire format:
//   [0..3] length prefix = 1 + payload_len  (big-endian uint32)
//   [4]    message_id
//   [5..]  payload (may be empty)
int peer_send_message(PeerConnection *conn, uint8_t msg_id,
                      const uint8_t *payload, uint32_t payload_len)
{
  uint32_t total_len = 1 + payload_len; // 1 for the msg_id byte

  // Build the 4-byte big-endian length prefix
  uint8_t header[5];
  header[0] = (total_len >> 24) & 0xFF;
  header[1] = (total_len >> 16) & 0xFF;
  header[2] = (total_len >> 8) & 0xFF;
  header[3] = (total_len) & 0xFF;
  header[4] = msg_id;

  if (send_all(conn->sock, header, 5) != 0)
    return -1;

  if (payload && payload_len > 0) {
    if (send_all(conn->sock, payload, (int)payload_len) != 0)
      return -1;
  }

  return 0;
}

//-----------peer_recv_message-----------
//  Reads the 4-byte length prefix, then the rest of the message.
//  Returns NULL on keepalive (length == 0). caller should just
//  call again. Also returns NULL on error.

PeerMessage *peer_recv_message(PeerConnection *conn)
{
  // Read 4-byte length prefix
  uint8_t len_buf[4];
  if (recv_all(conn->sock, len_buf, 4) != 0)
    return NULL;

  uint32_t length = ((uint32_t)len_buf[0] << 24) |
                    ((uint32_t)len_buf[1] << 16) | ((uint32_t)len_buf[2] << 8) |
                    (uint32_t)len_buf[3];

  // keepalive: length == 0, no message id or payload
  if (length == 0) {
    printf("peer: keepalive received\n");
    return NULL;
  }

  // Read message ID
  uint8_t msg_id;
  if (recv_all(conn->sock, &msg_id, 1) != 0)
    return NULL;

  uint32_t payload_len = length - 1; // length includes the ID byte

  PeerMessage *msg = (PeerMessage *)malloc(sizeof(PeerMessage));
  if (!msg)
    return NULL;

  msg->id = msg_id;
  msg->payload_len = payload_len;
  msg->payload = NULL;

  if (payload_len > 0) {
    msg->payload = (uint8_t *)malloc(payload_len);
    if (!msg->payload) {
      free(msg);
      return NULL;
    }
    if (recv_all(conn->sock, msg->payload, (int)payload_len) != 0) {
      free(msg->payload);
      free(msg);
      return NULL;
    }
  }

  return msg;
}

//-----------peer_request_block-----------
//  REQUEST payload (12 bytes, all big-endian):
//   [0..3]  piece_index
//   [4..7]  offset within piece
//   [8..11] length of block

int peer_request_block(PeerConnection *conn, uint32_t piece_index,
                       uint32_t offset, uint32_t length)
{
  uint8_t payload[12];
  payload[0] = (piece_index >> 24) & 0xFF;
  payload[1] = (piece_index >> 16) & 0xFF;
  payload[2] = (piece_index >> 8) & 0xFF;
  payload[3] = (piece_index) & 0xFF;

  payload[4] = (offset >> 24) & 0xFF;
  payload[5] = (offset >> 16) & 0xFF;
  payload[6] = (offset >> 8) & 0xFF;
  payload[7] = (offset) & 0xFF;

  payload[8] = (length >> 24) & 0xFF;
  payload[9] = (length >> 16) & 0xFF;
  payload[10] = (length >> 8) & 0xFF;
  payload[11] = (length) & 0xFF;

  return peer_send_message(conn, MSG_REQUEST, payload, 12);
}

//---------------------peer_download_piece---------------------
//
//  Full flow for downloading one piece:
//   1. Send interested
//   2. Wait for unchoke
//   3. Request all blocks (piece split into 16KB chunks)
//   4. Receive PIECE messages and write into out_buf
//   5. Return 0 when all blocks received
int peer_download_piece(PeerConnection *conn, uint32_t piece_index,
                        uint32_t piece_length, uint8_t *out_buf)
{
  // Step 1: Tell peer we're interested
  if (peer_send_message(conn, MSG_INTERESTED, NULL, 0) != 0) {
    fprintf(stderr, "peer: failed to send interested\n");
    return -1;
  }

  // Step 2: Wait for unchoke, handle other messages while waiting
  printf("peer: waiting for unchoke...\n");
  while (conn->peer_choking) {
    PeerMessage *msg = peer_recv_message(conn);
    if (!msg)
      continue; // keepalive or minor recv issue — keep waiting

    switch (msg->id) {
    case MSG_UNCHOKE:
      printf("peer: unchoked!\n");
      conn->peer_choking = 0;
      break;

    case MSG_CHOKE:
      fprintf(stderr, "peer: got choked while waiting for unchoke\n");
      free_peer_message(msg);
      return -1;

    case MSG_HAVE:
      // Peer telling us they got a new piece — update bitfield
      if (msg->payload_len == 4 && conn->bitfield) {
        uint32_t idx = ((uint32_t)msg->payload[0] << 24) |
                       ((uint32_t)msg->payload[1] << 16) |
                       ((uint32_t)msg->payload[2] << 8) |
                       (uint32_t)msg->payload[3];
        bitfield_set_piece(conn->bitfield, idx);
      }
      break;

    case MSG_BITFIELD:
      // Late bitfield — copy it in
      if (conn->bitfield && msg->payload_len == conn->bitfield_bytes)
        memcpy(conn->bitfield, msg->payload, msg->payload_len);
      break;

    default:
      // Ignore anything else while waiting for unchoke
      break;
    }
    free_peer_message(msg);
  }

  // Step 3: Request all blocks of this piece
  // Calculate how many full 16KB blocks + one possible smaller last block
  uint32_t num_blocks = (piece_length + BLOCK_SIZE - 1) / BLOCK_SIZE;
  uint32_t blocks_recv = 0;

  printf("peer: requesting %u blocks for piece %u\n", num_blocks, piece_index);

  for (uint32_t b = 0; b < num_blocks; b++) {
    uint32_t offset = b * BLOCK_SIZE;
    // Last block may be smaller than BLOCK_SIZE
    uint32_t blen = (offset + BLOCK_SIZE <= piece_length)
                        ? BLOCK_SIZE
                        : (piece_length - offset);

    if (peer_request_block(conn, piece_index, offset, blen) != 0) {
      fprintf(stderr, "peer: failed to send request for block %u\n", b);
      return -1;
    }
  }

  // Step 4: Receive PIECE messages until we have all blocks
  while (blocks_recv < num_blocks) {
    PeerMessage *msg = peer_recv_message(conn);
    if (!msg)
      continue;

    switch (msg->id) {
    case MSG_PIECE: {
      // PIECE payload layout:
      //  [0..3]  piece index  (big-endian uint32)
      //  [4..7]  byte offset  (big-endian uint32)
      //  [8..]   block data
      if (msg->payload_len < 9) {
        fprintf(stderr, "peer: piece message too short\n");
        free_peer_message(msg);
        return -1;
      }

      uint32_t recv_index = ((uint32_t)msg->payload[0] << 24) |
                            ((uint32_t)msg->payload[1] << 16) |
                            ((uint32_t)msg->payload[2] << 8) |
                            (uint32_t)msg->payload[3];

      uint32_t recv_offset = ((uint32_t)msg->payload[4] << 24) |
                             ((uint32_t)msg->payload[5] << 16) |
                             ((uint32_t)msg->payload[6] << 8) |
                             (uint32_t)msg->payload[7];

      uint32_t data_len = msg->payload_len - 8;

      if (recv_index != piece_index) {
        fprintf(stderr, "peer: got piece %u but expected %u\n", recv_index,
                piece_index);
        free_peer_message(msg);
        continue;
      }

      if (recv_offset + data_len > piece_length) {
        fprintf(stderr, "peer: block overflows piece buffer\n");
        free_peer_message(msg);
        return -1;
      }

      // Write block into the correct position in out_buf
      memcpy(out_buf + recv_offset, msg->payload + 8, data_len);
      blocks_recv++;
      printf("peer: received block at offset %u (%u/%u)\n", recv_offset,
             blocks_recv, num_blocks);
      break;
    }

    case MSG_CHOKE:
      fprintf(stderr, "peer: choked mid-download\n");
      conn->peer_choking = 1;
      free_peer_message(msg);
      return -1;

    case MSG_HAVE:
      if (msg->payload_len == 4 && conn->bitfield) {
        uint32_t idx = ((uint32_t)msg->payload[0] << 24) |
                       ((uint32_t)msg->payload[1] << 16) |
                       ((uint32_t)msg->payload[2] << 8) |
                       (uint32_t)msg->payload[3];
        bitfield_set_piece(conn->bitfield, idx);
      }
      break;

    default:
      break;
    }

    free_peer_message(msg);
  }

  printf("peer: piece %u download complete\n", piece_index);
  return 0;
}

//----------Clean Up-----------

void peer_disconnect(PeerConnection *conn)
{
  if (!conn)
    return;
  closesocket(conn->sock);
  free(conn->bitfield);
  free(conn);
}

void free_peer_message(PeerMessage *msg)
{
  if (!msg)
    return;
  free(msg->payload);
  free(msg);
}