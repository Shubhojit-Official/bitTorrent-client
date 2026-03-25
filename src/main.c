#include "../include/tracker.h"
#include "../include/bencode.h"
#include "../include/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main()
{
    if (tracker_init() != 0)
        return 1;

    const char *raw = read_file("./torrents/lies_of_p.torrent");
    const char *ptr = raw;
    Bencode *parsed = parse_bencode(&ptr);
    TorrentMeta *meta = extract_torrent_metadata(parsed);

    uint8_t info_hash[20];
    get_info_hash(parsed, info_hash);

    uint8_t peer_id[20];
    generate_peer_id(peer_id);

    uint64_t left = 0;
    for (size_t i = 0; i < meta->file_count; i++)
        left += meta->files[i].length;

    TrackerResponse *resp = NULL;

    // --- Try the primary announce URL first ---
    printf("Trying primary tracker: %s\n", meta->announce);
    resp = announce_udp(meta->announce, info_hash, peer_id, left, 6881);

    if (resp && has_public_peers(resp))
    {
        printf("Got public peers from primary tracker.\n");
    }
    else
    {
        // Primary failed or returned only private IPs — try announce-list
        if (resp)
        {
            printf("Primary tracker returned only private IPs, trying announce-list...\n");
            free_tracker_response(resp);
            resp = NULL;
        }
        else
        {
            printf("Primary tracker failed, trying announce-list...\n");
        }

        // announce-list is a list of tiers, each tier is a list of URLs
        // Structure: [ [url, url], [url], [url, url] ]
        Bencode *announce_list = __get_value_from_dict(parsed, "announce-list");
        if (announce_list && announce_list->type == BE_LIST)
        {
            for (size_t i = 0; i < announce_list->len && !resp; i++)
            {
                Bencode *tier = announce_list->value.list[i];
                if (!tier || tier->type != BE_LIST)
                    continue;

                for (size_t j = 0; j < tier->len && !resp; j++)
                {
                    Bencode *url_node = tier->value.list[j];
                    if (!url_node || url_node->type != BE_STRING)
                        continue;

                    const char *url = url_node->value.string;

                    // Skip non-UDP trackers for now (HTTP support comes later)
                    if (strncmp(url, "udp://", 6) != 0)
                    {
                        printf("Skipping non-UDP tracker: %s\n", url);
                        continue;
                    }

                    printf("Trying: %s\n", url);
                    TrackerResponse *r = announce_udp(url, info_hash, peer_id, left, 6881);

                    if (r && has_public_peers(r))
                    {
                        printf("Got public peers from: %s\n", url);
                        resp = r;
                    }
                    else
                    {
                        if (r)
                            free_tracker_response(r);
                    }
                }
            }
        }
        else
        {
            printf("No announce-list found in torrent.\n");
        }
    }

    // --- Print results ---
    if (resp)
    {
        print_tracker_response(resp);
        free_tracker_response(resp);
    }
    else
    {
        printf("Could not get public peers from any tracker.\n");
    }

    free_be(parsed);
    free((void *)raw);
    clean_torrent_mem(meta);
    tracker_cleanup();
    return 0;
}