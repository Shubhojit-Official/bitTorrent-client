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