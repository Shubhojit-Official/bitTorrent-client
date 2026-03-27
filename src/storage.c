#include "../include/storage.h"
#include "../include/sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Windows mkdir takes only one argument (no mode)
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

//-------------Internal helpers-------------
// Creates every directory component in a path like
// "downloads/game/data" — equivalent to mkdir -p
static void mkdir_recursive(const char *path)
{
  char tmp[512];
  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char saved = *p;
      *p = '\0';
      MKDIR(tmp); // ignore errors — dir may already exist
      *p = saved;
    }
  }

  MKDIR(tmp);
}

// Builds the full path for file i: "output_dir/path_component0/component1/..."
static void build_file_path(const TorrentStorage *storage, size_t file_index,
                            char *out, size_t out_size)
{
  TorrentFile *f = &storage->meta->files[file_index];

  // Start with output dir
  snprintf(out, out_size, "%s", storage->output_dir);

  // Append each path component
  for (size_t i = 0; i < f->path_len; i++) {
    size_t used = strlen(out);
    snprintf(out + used, out_size - used, "%s", f->path_components[i]);
  }
}

// Pre-creates a file at its full size so fseek+fwrite works anywhere
// without gaps. Uses a sparse write — just seeks to the end and writes
// one zero byte, letting the OS handle the rest.
static int preallocate_file(const char *path, uint64_t size)
{
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "storage: failed to create file: %s\n", path);
    return -1;
  }

  if (size > 0) {

    // Seek to last byte position and write a single zero
    // This creates a sparse file : the OS doesn't actually
    // allocate all the disk blocks until data is written there
    if (_fseeki64(f, (int64_t)(size - 1), SEEK_SET) != 0) {
      fclose(f);
      return -1;
    }
    uint8_t zero = 0;
    fwrite(&zero, 1, 1, f);
  }

  fclose(f);
  return 0;
}

//  Bitfield helpers (storage's own copy for completed pieces)

static int storage_bitfield_has(const TorrentStorage *s, size_t piece_index)
{
  size_t byte = piece_index / 8;
  int bit = 7 - (int)(piece_index % 8);
  return (s->bitfield[byte] >> bit) & 1;
}

static void storage_bitfield_set(TorrentStorage *s, size_t piece_index)
{
  size_t byte = piece_index / 8;
  int bit = 7 - (int)(piece_index % 8);
  s->bitfield[byte] |= (uint8_t)(1 << bit);
}
