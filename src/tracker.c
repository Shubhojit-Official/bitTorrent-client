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

#define CONNECT_MAGIC 0x41727101980ULL // magic number required by spec
#define ACTION_CONNECT 0
#define ACTION_ANNOUNCE 1
#define ACTION_ERROR 3
#define EVENT_NONE 0
#define EVENT_COMPLETED 1
#define EVENT_STARTED 2
#define EVENT_STOPPED 3

// Retry policy per BEP 15: timeout = 15 * 2^n seconds, up to 8 retries.
// capping at 4 tries for now (15s, 30s, 60s, 120s)
#define MAX_RETRIES 4
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

// URL parser
// Handles "udp://hostname:port/path" -> extracts host and port.

static int parse_udp_url(const char *url,
                         char *host_out,
                         size_t host_size,
                         uint16_t *port_out)
{
    if (strncmp(url, "udp://", 6) != 0)
    {
        fprintf(stderr, "tracker: not a UDP URL: %s\n", url);
        return -1;
    }

    const char *p = url + 6;            // points past "udp://"
    const char *colon = strchr(p, ":"); // separator btw host and port

    if (!colon)
    {
        fprintf(stderr, "tracker: missing port in URL: %s\n", url);
        return -1;
    }

    size_t host_len = (size_t)(colon - p);
    if (host_len == 0 || host_len >= host_size)
    {
        fprintf(stderr, "tracker: host length invalid \n");
        return -1;
    }

    memcpy(host_out, p, host_len);
    host_out[host_len] = "\0";

    // extract port no right after ":"

    int port_val = atoi(colon + 1);
    if (port_val <= 0 || port_val > 65535)
    {
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
    if (err != 0)
    {
        fprintf(stderr, "WSAStartup failed with error: %d\n", err);
        return -1;
    }

    return 0;
}

void tracker_cleanup(void)
{
    WSACleanup();
}
