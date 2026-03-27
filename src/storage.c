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