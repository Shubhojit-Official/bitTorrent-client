#include "../include/bencode.h"
#include "../include/parser.h"
#include "string.h"
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

/*
    @param dict: ptr to a bencoded dictionary
    @param key: const char* str for a key
    @return value of the associated key (could be a dict as well)
*/
Bencode *__get_value_from_dict(Bencode *dict, const char *key)
{
    for (size_t i = 0; i < dict->len; i += 2)
    {
        Bencode *k = dict->value.dict[i];
        if (k->type == BE_STRING && strcmp(k->value.string, key) == 0)
        {
            return dict->value.dict[i + 1]; // value can also be a dict that's why....
        }
    }

    return NULL;
}

// parser.c
static void __parse_files_list(Bencode *files_list, TorrentMeta *meta)
{
    if (!files_list || files_list->type != BE_LIST)
    {
        fprintf(stderr, "Error: files_list is missing or not a list\n");
        meta->files = NULL;
        meta->file_count = 0;
        return;
    }

    size_t n = files_list->len;
    meta->files = calloc(n, sizeof(TorrentFile));
    if (!meta->files)
    {
        perror("calloc failed for meta->files");
        meta->file_count = 0;
        return;
    }

    size_t out = 0; // number of valid files we actually record

    for (size_t i = 0; i < n; ++i)
    {
        Bencode *file_dict = files_list->value.list[i];

        if (!file_dict || file_dict->type != BE_DICT)
        {
            fprintf(stderr, "Warning: file entry %zu is not a dict — skipping\n", i);
            continue;
        }

        // length
        Bencode *length_b = __get_value_from_dict(file_dict, "length");
        if (!length_b || length_b->type != BE_INTEGER)
        {
            fprintf(stderr, "Warning: file entry %zu missing/invalid length — skipping\n", i);
            continue;
        }

        // path (list of strings)
        Bencode *path_b = __get_value_from_dict(file_dict, "path");
        if (!path_b || path_b->type != BE_LIST || path_b->len == 0)
        {
            fprintf(stderr, "Warning: file entry %zu missing/invalid path — skipping\n", i);
            continue;
        }

        // Validate all path components first
        size_t path_len = path_b->len;
        int bad = 0;
        for (size_t j = 0; j < path_len; ++j)
        {
            Bencode *pc = path_b->value.list[j];
            if (!pc || pc->type != BE_STRING)
            {
                fprintf(stderr, "Warning: file entry %zu has non-string path component at %zu — skipping\n", i, j);
                bad = 1;
                break;
            }
        }
        if (bad)
            continue;

        // Allocate and copy path components
        char **components = (char **)malloc(path_len * sizeof(char *));
        if (!components)
        {
            perror("malloc failed for path components");
            // We cannot partially fill meta; just skip this file.
            continue;
        }

        size_t copied = 0;
        for (size_t j = 0; j < path_len; ++j)
        {
            const char *src = path_b->value.list[j]->value.string;
            components[j] = strdup(src);
            if (!components[j])
            {
                perror("strdup failed for path component");
                // Roll back the ones we already duplicated
                for (size_t k = 0; k < j; ++k)
                    free(components[k]);
                free(components);
                components = NULL;
                break;
            }
            copied++;
        }
        if (!components)
        {
            // Skip this file due to allocation failure
            continue;
        }

        // Populate the output file entry
        meta->files[out].length = length_b->value.integer;
        meta->files[out].path_len = path_len;
        meta->files[out].path_components = components;
        out++;
    }

    // If some entries were skipped, shrink the array
    if (out == 0)
    {
        // No valid files — free the array and leave empty
        free(meta->files);
        meta->files = NULL;
        meta->file_count = 0;
    }
    else
    {
        if (out < n)
        {
            TorrentFile *shrunk = realloc(meta->files, out * sizeof(TorrentFile));
            if (shrunk)
                meta->files = shrunk; // if realloc fails, keep original (still valid)
        }
        meta->file_count = out;
    }
}

TorrentMeta *extract_torrent_metadata(Bencode *root)
{
    if (!root || root->type != BE_DICT)
    {
        fprintf(stderr, "Error: root is not a dictionary\n");
        return NULL;
    }

    TorrentMeta *meta = calloc(1, sizeof(TorrentMeta));
    if (!meta)
    {
        perror("calloc failed");
        return NULL;
    }

    // --- Announce URL ---
    Bencode *announce = __get_value_from_dict(root, "announce");
    if (announce && announce->type == BE_STRING)
    {
        meta->announce = strdup(announce->value.string);
    }
    else
    {
        fprintf(stderr, "Warning: missing or invalid announce URL\n");
    }

    // --- Info Dict ---
    Bencode *info = __get_value_from_dict(root, "info");
    if (!info || info->type != BE_DICT)
    {
        fprintf(stderr, "Error: missing or invalid info dictionary\n");
        free(meta);
        return NULL;
    }

    // --- Piece Length ---
    Bencode *piece_len = __get_value_from_dict(info, "piece length");
    if (piece_len && piece_len->type == BE_INTEGER)
    {
        meta->piece_length = piece_len->value.integer;
    }
    else
    {
        fprintf(stderr, "Warning: missing or invalid piece length\n");
    }

    // --- Pieces (SHA1 hashes) ---
    Bencode *pieces = __get_value_from_dict(info, "pieces");
    if (pieces && pieces->type == BE_STRING)
    {
        if (pieces->len % 20 != 0)
        {
            fprintf(stderr, "Warning: pieces length (%zu) not divisible by 20\n", pieces->len);
        }
        size_t len = pieces->len;
        meta->pieces = malloc(len);
        if (!meta->pieces)
        {
            perror("malloc failed");
            free(meta);
            return NULL;
        }
        memcpy(meta->pieces, pieces->value.string, len);
        meta->num_pieces = len / 20;
    }
    else
    {
        fprintf(stderr, "Warning: missing or invalid pieces field\n");
    }

    // --- Name ---
    Bencode *name = __get_value_from_dict(info, "name");
    if (name && name->type == BE_STRING)
    {
        meta->name = strdup(name->value.string);
    }
    else
    {
        fprintf(stderr, "Warning: missing or invalid name\n");
        meta->name = strdup("unknown"); // fallback
    }

    // --- Files (multi-file or single-file mode) ---
    Bencode *files = __get_value_from_dict(info, "files");
    if (files && files->type == BE_LIST)
    {
        __parse_files_list(files, meta);
    }
    else
    {
        // single-file mode fallback
        meta->files = calloc(1, sizeof(TorrentFile));
        if (!meta->files)
        {
            perror("calloc failed");
            free(meta->pieces);
            free(meta->announce);
            free(meta->name);
            free(meta);
            return NULL;
        }
        meta->file_count = 1;

        Bencode *length = __get_value_from_dict(info, "length");
        if (length && length->type == BE_INTEGER)
        {
            meta->files[0].length = length->value.integer;
        }
        else
        {
            fprintf(stderr, "Warning: missing or invalid file length in single-file mode\n");
            meta->files[0].length = 0;
        }

        meta->files[0].path_components = malloc(sizeof(char *));
        if (!meta->files[0].path_components)
        {
            perror("malloc failed");
            free(meta->files);
            free(meta->pieces);
            free(meta->announce);
            free(meta->name);
            free(meta);
            return NULL;
        }
        meta->files[0].path_len = 1;
        meta->files[0].path_components[0] = strdup(meta->name);
    }

    return meta;
}

void print_torrent_file(TorrentMeta *meta)
{
    if (meta)
    {
        printf("Announce URL: %s\n", meta->announce);
        printf("Piece Length: %lld\n", meta->piece_length);
        printf("Number of Pieces: %zu\n", meta->num_pieces);

        for (size_t i = 0; i < meta->num_pieces; ++i)
        {
            printf("Piece %zu hash: ", i);
            for (int j = 0; j < 20; ++j)
                printf("%02x", meta->pieces[i * 20 + j] & 0xFF);
            printf("\n");
        }

        for (size_t i = 0; i < meta->file_count; ++i)
        {
            printf("File %zu: ", i);
            for (size_t j = 0; j < meta->files[i].path_len; ++j)
            {
                printf("%s/", meta->files[i].path_components[j]);
            }
            printf(" (%lld bytes)\n", meta->files[i].length);
        }
    }
}

void clean_torrent_mem(TorrentMeta *meta)
{
    for (size_t i = 0; i < meta->file_count; ++i)
    {
        for (size_t j = 0; j < meta->files[i].path_len; ++j)
        {
            free(meta->files[i].path_components[j]);
        }
        free(meta->files[i].path_components);
    }
    free(meta->files);
    free(meta->announce);
    free(meta->pieces);
    free(meta);
}

int main()
{
    const char *raw = read_file("./torrents/lies_of_p.torrent");
    const char *ptr = raw;

    Bencode *parsed = parse_bencode(&ptr);
    TorrentMeta *meta = extract_torrent_metadata(parsed);
    print_torrent_file(meta);
    clean_torrent_mem(meta);
    return 0;
}