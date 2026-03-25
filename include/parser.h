#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "../include/bencode.h"
#include "../include/sha1.h"

typedef struct
{
    char **path_components;
    size_t path_len;
    long long length;
} TorrentFile;

typedef struct
{
    char *announce;
    long long piece_length;
    unsigned char *pieces;
    size_t num_pieces;

    char *name;
    TorrentFile *files;
    size_t file_count;
} TorrentMeta;

// ----APIS----
Bencode *__get_value_from_dict(Bencode *dict, const char *key);
TorrentMeta *extract_torrent_metadata(Bencode *root);
void print_torrent_file(TorrentMeta *meta);
void clean_torrent_mem(TorrentMeta *meta);
int get_info_hash(Bencode *root, uint8_t out_hash[SHA1_BLOCK_SIZE]);
char *get_info_hash_hex(Bencode *root);

#endif