#ifndef TRACKER_H
#define TRACKER_H

#include <stdint.h>
#include <stddef.h>

// A single peer returned by the tracker
typedef struct {
  uint32_t ip;   // IPv4 address in host byte order
  uint16_t port; // port in host byter order
} Peer;

// Response structure from a tracker announce
typedef struct {
  uint32_t interval; // seconds until next re-announce (shit goes wrong must
                     // RESPECT!!!)
  uint32_t leechers; // peers currently downloading
  uint32_t seeders;  // peers with full copy
  Peer *peers;       // heap-allocated array of peers
  size_t peer_count;
} TrackerResponse;

/*
 * tracker init / tracker_cleanup
 * Must be called once at program start and end.
 * On Windows this initialises / tears down Winsock
 */
int tracker_init(void);
void tracker_cleanup(void);

/*
 * generate_peer_id
 * Fills peer_id[20] with an Azureus-style ID: "-SG0001-" + 12 random digits.
 * Call once at startup and reuse the same ID for the whole session.
 */
void generate_peer_id(uint8_t peer_id[20]);

/*
 * announce_udp
 * Performs a full UDP tracker announce (BEP 15):
 *   1. Connect handshake  (with retry/back-off)
 *   2. Announce request
 *
 * Parameters:
 *   url        - tracker announce URL, e.g.
 * "udp://tracker.opentrackr.org:1337/announce" info_hash  - 20-byte raw SHA1
 * from get_info_hash() peer_id    - 20-byte peer ID from generate_peer_id()
 *   left       - bytes still needed (total file size on first call)
 *   listen_port- TCP port you will listen on for peer connections (e.g. 6881)
 *
 * Returns a heap-allocated TrackerResponse on success, NULL on failure.
 * Caller must free with free_tracker_response().
 */

TrackerResponse *announce_udp(const char *url, const uint8_t info_hash[20],
                              const uint8_t peer_id[20], uint64_t left,
                              uint16_t listen_port);

/*
 * print_tracker_response
 * Debug helper — prints interval, counts, and all peer IP:port pairs.
 */

void print_tracker_response(const TrackerResponse *resp);

void free_tracker_response(TrackerResponse *resp);

// Returns 1 if the response contains at least one public (non-RFC1918) peer
int has_public_peers(const TrackerResponse *resp);

#endif
