#ifndef PARSER_H
#define PARSER_H
#include <stddef.h>

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

// Internal Functions
Bencode *__get_value_from_dict(Bencode *dict, const char *key);
static void __parse_files_list(Bencode *files_list, TorrentMeta *meta);

// ----APIS----
TorrentMeta *extract_torrent_metadata(Bencode *root);
void print_torrent_file(TorrentMeta *meta);
void clean_torrent_mem(TorrentMeta *meta);
#endif