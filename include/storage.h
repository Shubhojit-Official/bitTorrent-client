#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include "../include/parser.h"
#include "../include/sha1.h"

typedef struct {

  TorrentMeta *meta; // pointer to torrent metadata

  char *output_dir; // directory to for downloaded files

  uint64_t *file_offsets; // file_offsets[i] = byte offset of file i within
                          // the flat torrent data space. heap-allocated.

  uint64_t total_length; // sum of all file lengths

  uint8_t *bitfield; // which pieces we have successfully written
  size_t bitfield_bytes;

} TorrentStorage;

// ---------------API---------------
/*
 * storage_init
 * Allocates and initialises a TorrentStorage.
 * Creates the output directory and any subdirectories needed.
 * Pre-allocates all files at their full size (sparse, no actual disk use).
 * Returns heap-allocated TorrentStorage on success, NULL on failure.
 */
TorrentStorage *storage_init(TorrentMeta *meta, const char *output_dir);

/*
 * verify_piece
 * SHA1-hashes piece_buf (piece_length bytes) and compares against
 * the expected hash stored in meta->pieces for piece_index.
 * Returns 1 if hash matches (piece is good), 0 if corrupted.
 */
int verify_piece(const TorrentMeta *meta, uint32_t piece_index,
                 const uint8_t *piece_buf, uint32_t piece_length);

/*
 * storage_write_piece
 * Writes a verified piece to the correct location(s) on disk.
 * Handles pieces that span multiple files automatically.
 * Returns 0 on success, -1 on failure.
 */
int storage_write_piece(TorrentStorage *storage, uint32_t piece_index,
                        const uint8_t *piece_buf, uint32_t piece_length);

/*
 * storage_piece_length
 * Returns the length of a specific piece in bytes.
 * All pieces are meta->piece_length bytes except the last one
 * which may be smaller.
 */
uint32_t storage_piece_length(const TorrentStorage *storage,
                              uint32_t piece_index);

/*
 * storage_is_complete
 * Returns 1 if all pieces have been successfully written, 0 otherwise.
 */
int storage_is_complete(const TorrentStorage *storage);

/*
 * storage_pieces_done
 * Returns how many pieces have been successfully written so far.
 */
size_t storage_pieces_done(const TorrentStorage *storage);

/*
 * storage_free
 * Frees all memory. Does NOT delete any downloaded files.
 */
void storage_free(TorrentStorage *storage);
#endif