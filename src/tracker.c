#include "../include/tracker.h"

// Winsock headers (Need to come first before anything else!)
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//-------------------------------------------
// BEP 15 CONSTANTS
//-------------------------------------------

#define CONNECT_MAGIC   0x41727101980ULL // magic number required by spec
#define ACTION_CONNECT  0
#define ACTION_ANNOUNCE 1
#define ACTION_ERROR    3
#define EVENT_NONE      0
#define EVENT_COMPLETED 1
#define EVENT_STARTED   2
#define EVENT_STOPPED   3

// Retry policy per BEP 15: timeout = 15 * 2^n seconds, up to 8 retries.
// capping at 4 tries for now (15s, 30s, 60s, 120s)
#define MAX_RETRIES     4
#define BASE_TIMEOUT_MS 15000

// Max peers we allocate buffer space for in a single response
#define MAX_PEERS 200

//-----------------------------------------------------
// Internal byte-order helpers(not using htonl/ ntohl)
// Manually writing everything to avoid struct packing
// also makes the code easy to follow against the spec
//-----------------------------------------------------

static void write_u16(uint8_t *b, uint16_t v)
{
  b[0] = (v >> 8) & 0xFF;
  b[1] = (v) & 0xFF;
}

static void write_u32(uint8_t *b, uint32_t v)
{
  b[0] = (v >> 24) & 0xFF;
  b[1] = (v >> 16) & 0xFF;
  b[2] = (v >> 8) & 0xFF;
  b[3] = (v) & 0xFF;
}

static void write_u64(uint8_t *b, uint64_t v)
{
  b[0] = (v >> 56) & 0xFF;
  b[1] = (v >> 48) & 0xFF;
  b[2] = (v >> 40) & 0xFF;
  b[3] = (v >> 32) & 0xFF;
  b[4] = (v >> 24) & 0xFF;
  b[5] = (v >> 16) & 0xFF;
  b[6] = (v >> 8) & 0xFF;
  b[7] = (v) & 0xFF;
}

static uint32_t read_u32(const uint8_t *b)
{
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static uint64_t read_u64(const uint8_t *b)
{
  return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
         ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
         ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
         ((uint64_t)b[6] << 8) | (uint64_t)b[7];
}
//---------------------------------------------------------------
// URL parser
// Handles "udp://hostname:port/path" -> extracts host and port.
//---------------------------------------------------------------

static int parse_udp_url(const char *url, char *host_out, size_t host_size,
                         uint16_t *port_out)
{
  if (strncmp(url, "udp://", 6) != 0) {
    fprintf(stderr, "tracker: not a UDP URL: %s\n", url);
    return -1;
  }

  const char *p = url + 6;            // points past "udp://"
  const char *colon = strchr(p, ':'); // separator btw host and port

  if (!colon) {
    fprintf(stderr, "tracker: missing port in URL: %s\n", url);
    return -1;
  }

  size_t host_len = (size_t)(colon - p);
  if (host_len == 0 || host_len >= host_size) {
    fprintf(stderr, "tracker: host length invalid \n");
    return -1;
  }

  memcpy(host_out, p, host_len);
  host_out[host_len] = '\0';

  // extract port no right after ":"

  int port_val = atoi(colon + 1);
  if (port_val <= 0 || port_val > 65535) {
    fprintf(stderr, "tracker: invalid port in URL: %s\n", url);
    return -1;
  }

  *port_out = (uint16_t)port_val;
  return 0;
}

//-----------------------------------------------------
// WINSOCK LIFECYLCE (must init once)
//-----------------------------------------------------

int tracker_init(void)
{
  WSADATA wsa;
  int err = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (err != 0) {
    fprintf(stderr, "WSAStartup failed with error: %d\n", err);
    return -1;
  }

  return 0;
}

void tracker_cleanup(void) { WSACleanup(); }

//------------------------------------------------------------
// Peer ID generation
// Azureus style: "-SG0001-" prefix + 12 random decimal digits
//------------------------------------------------------------

void generate_peer_id(uint8_t peer_id[20])
{
  srand((unsigned int)time(NULL));
  memcpy(peer_id, "-SG0001-", 8);
  for (int i = 8; i < 20; i++)
    peer_id[i] = (uint8_t)('0' + rand() % 10);
}

//------------------------
// Main Public Function
//------------------------

TrackerResponse *announce_udp(const char *url, const uint8_t info_hash[20],
                              const uint8_t peer_id[20], uint64_t left,
                              uint16_t listen_port)
{
  // Parse URL
  char host[256];
  uint16_t tracker_port;

  if (parse_udp_url(url, host, sizeof(host), &tracker_port) != 0)
    return NULL;

  // Resolve hostname
  struct addrinfo hints = {0};
  hints.ai_family = AF_INET; // IPv4 only rn
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", tracker_port);

  struct addrinfo *res = NULL;
  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    fprintf(stderr, "tracker: getaddrinfo failed for %s: %d", host,
            WSAGetLastError());
    freeaddrinfo(res);
    return NULL;
  }

  // Create UDP socket
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    fprintf(stderr, "tracker: socket() failed: %d\n", WSAGetLastError());
    freeaddrinfo(res);
    return NULL;
  }

  // Connect handshake with exponential back-off retry

  // Connect request layout (16 bytes, all big-endian):
  //   [0 .. 7]  connection_id  = CONNECT_MAGIC (8 bytes)
  //   [8 .. 11] action         = 0 (connect) (4 bytes)
  //   [12..15]  transaction_id = random (4 bytes)
  //
  // Connect response layout (16 bytes):
  //   [0 .. 3]  action         = 0 (4 bytes)
  //   [4 .. 7]  transaction_id (must match ours) (4 bytes)
  //   [8 ..15]  connection_id  (save this for the announce) (8 bytes)

  uint64_t connection_id = 0;
  int connected = 0;
  uint32_t con_txn_id = (uint32_t)rand();

  for (int attempt = 0; attempt < MAX_RETRIES && !connected; attempt++) {
    // Set receive timeout: 15s, 30s, 60s, 120s
    DWORD timeout_ms = (DWORD)(BASE_TIMEOUT_MS * (1u << attempt));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
               sizeof(timeout_ms));

    // Build and send connect request
    uint8_t con_req[16];
    write_u64(con_req, CONNECT_MAGIC);
    write_u32(con_req + 8, ACTION_CONNECT);
    write_u32(con_req + 12, con_txn_id);

    int sent = sendto(sock, (char *)con_req, 16, 0, res->ai_addr,
                      (int)res->ai_addrlen);
    if (sent != 16) {
      fprintf(stderr, "tracker: sendto (connect) failed on attempt %d: %d\n",
              attempt + 1, WSAGetLastError());
      continue;
    }

    // Wait for connect response
    uint8_t con_resp[16];
    int n = recvfrom(sock, (char *)con_resp, sizeof(con_resp), 0, NULL, NULL);
    if (n < 16) {
      fprintf(stderr, "tracker: connect response timeout/error on attempt %d\n",
              attempt + 1);
      continue;
    }

    uint32_t resp_action = read_u32(con_resp);
    uint32_t resp_txn = read_u32(con_resp + 4);

    // Check for error response from  tracker

    if (resp_action == ACTION_ERROR) {
      fprintf(stderr, "tracker: error response on connect\n");
      break;
    }

    if (resp_action != ACTION_CONNECT || resp_txn != con_txn_id) {
      fprintf(stderr,
              "tracker: unexpected connect response "
              "(action=%u, txn=%u, expected txn=%u)\n",
              resp_action, resp_txn, con_txn_id);
      continue;
    }

    connection_id = read_u64(con_resp + 8);
    connected = 1;
    printf("tracker: connected (connection_id = %llu)\n",
           (unsigned long long)connection_id);
  }
  if (!connected) {
    fprintf(stderr, "tracker: failed to connect after %d attempts\n",
            MAX_RETRIES);
    closesocket(sock);
    freeaddrinfo(res);
    return NULL;
  }

  // Send Announce request(98 bytes, all big - endian)
  //
  //    [0  .. 7 ] connection_id
  //    [8  .. 11] action         = 1 (announce)
  //    [12 .. 15] transaction_id = random (new one)
  //    [16 .. 35] info_hash      (20 bytes)
  //    [36 .. 55] peer_id        (20 bytes)
  //    [56 .. 63] downloaded     = 0 (starting fresh)
  //    [64 .. 71] left
  //    [72 .. 79] uploaded       = 0
  //    [80 .. 83] event          = 2 (started)
  //    [84 .. 87] ip             = 0 (tracker uses sender IP)
  //    [88 .. 91] key            = random (identifies you across reconnects)
  //    [92 .. 95] num_want       = -1 (default, tracker decides count)
  //    [96 .. 97] port           (your listen port)

  // Reset timeout to base for announce
  DWORD ann_timeout = BASE_TIMEOUT_MS;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ann_timeout,
             sizeof(ann_timeout));

  uint32_t ann_txn_id = (uint32_t)rand();
  uint8_t ann_req[98];
  memset(ann_req, 0, sizeof(ann_req));

  int off = 0;
  write_u64(ann_req + off, connection_id);
  off += 8;
  write_u32(ann_req + off, ACTION_ANNOUNCE);
  off += 4;
  write_u32(ann_req + off, ann_txn_id);
  off += 4;
  memcpy(ann_req + off, info_hash, 20);
  off += 20;
  memcpy(ann_req + off, peer_id, 20);
  off += 20;
  write_u64(ann_req + off, 0);
  off += 8; // downloaded
  write_u64(ann_req + off, left);
  off += 8; // left
  write_u64(ann_req + off, 0);
  off += 8; // uploaded
  write_u32(ann_req + off, EVENT_STARTED);
  off += 4;
  write_u32(ann_req + off, 0);
  off += 4; // ip (0 = auto)
  write_u32(ann_req + off, (uint32_t)rand());
  off += 4; // key
  write_u32(ann_req + off, (uint32_t)-1);
  off += 4;                              // num_want
  write_u16(ann_req + off, listen_port); // port (2 bytes)

  int sent =
      sendto(sock, (char *)ann_req, 98, 0, res->ai_addr, (int)res->ai_addrlen);

  if (sent != 98) {
    fprintf(stderr, "tracker: sendto (announce) failed: %d\n",
            WSAGetLastError());
    closesocket(sock);
    freeaddrinfo(res);
    return NULL;
  }

  //  Receive Announce response
  //
  //   [0  .. 3 ] action         = 1
  //   [4  .. 7 ] transaction_id (must match)
  //   [8  .. 11] interval
  //   [12 .. 15] leechers
  //   [16 .. 19] seeders
  //   [20 ..   ] peers: 6 bytes each (4 bytes IP + 2 bytes port)

  // 20 byte header + 6 bytes per peer
  uint8_t ann_resp[20 + 6 * MAX_PEERS];
  int n = recvfrom(sock, (char *)ann_resp, sizeof(ann_resp), 0, NULL, NULL);

  if (n < 20) {
    fprintf(stderr,
            "tracker: announce response too short (%d bytes) or timeout\n", n);
    closesocket(sock);
    freeaddrinfo(res);
    return NULL;
  }

  uint32_t resp_action = read_u32(ann_resp);
  uint32_t resp_txn = read_u32(ann_resp + 4);

  if (resp_action == ACTION_ERROR) {
    // Tracker sent a human-readable error message after the header
    int msg_len = n - 8;
    if (msg_len > 0)
      fprintf(stderr, "tracker error: %.*s\n", msg_len, (char *)(ann_resp + 8));
    else
      fprintf(stderr, "tracker: error action received\n");
    closesocket(sock);
    freeaddrinfo(res);
    return NULL;
  }

  // Parse the resone into TrackerResponse
  TrackerResponse *tr = calloc(1, sizeof(TrackerResponse));
  if (!tr) {
    closesocket(sock);
    freeaddrinfo(res);
    return NULL;
  }

  tr->interval = read_u32(ann_resp + 8);
  tr->leechers = read_u32(ann_resp + 12);
  tr->seeders = read_u32(ann_resp + 16);

  int peer_bytes = n - 20; // bytes after the fixed header
  tr->peer_count = (size_t)(peer_bytes / 6);

  tr->peers = calloc(tr->peer_count, sizeof(Peer));
  if (!tr->peers) {
    free(tr);
    closesocket(sock);
    freeaddrinfo(res);
    return NULL;
  }

  for (size_t i = 0; i < tr->peer_count; i++) {
    const uint8_t *p = ann_resp + 20 + i * 6;
    // IP: 4 bytes big-endian  ->  host byte order
    tr->peers[i].ip = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    // Port: 2 bytes big-endian  ->  host byte order
    tr->peers[i].port = ((uint16_t)p[4] << 8) | p[5];
  }

  closesocket(sock);
  freeaddrinfo(res);
  return tr;
}

int has_public_peers(const TrackerResponse *resp)
{
  if (!resp || resp->peer_count == 0)
    return 0;

  for (size_t i = 0; i < resp->peer_count; i++) {
    uint32_t ip = resp->peers[i].ip;
    uint8_t a = (ip >> 24) & 0xFF;
    uint8_t b = (ip >> 16) & 0xFF;

    int is_private = (a == 10) || (a == 172 && b >= 16 && b <= 31) ||
                     (a == 192 && b == 168) || (a == 127) || (a == 0);

    if (!is_private)
      return 1;
  }
  return 0;
}

// -----------------
//  Debug print
// -----------------

void print_tracker_response(const TrackerResponse *resp)
{
  if (!resp) {
    printf("TrackerResponse: NULL\n");
    return;
  }
  printf("Interval : %u seconds\n", resp->interval);
  printf("Seeders  : %u\n", resp->seeders);
  printf("Leechers : %u\n", resp->leechers);
  printf("Peers    : %zu\n", resp->peer_count);

  for (size_t i = 0; i < resp->peer_count; i++) {
    uint32_t ip = resp->peers[i].ip;
    printf("  [%3zu] %u.%u.%u.%u:%u\n", i, (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
           (ip >> 8) & 0xFF, (ip) & 0xFF, resp->peers[i].port);
  }
}

//------------------
//  Cleanup
//------------------

void free_tracker_response(TrackerResponse *resp)
{
  if (!resp)
    return;
  free(resp->peers);
  free(resp);
}