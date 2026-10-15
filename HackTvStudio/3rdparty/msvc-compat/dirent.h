/* dirent.h - minimal POSIX directory listing for MSVC (used by hacktv's teletext.c).
 *
 * Implements opendir / readdir / closedir on top of FindFirstFileA. Only the
 * d_name member of struct dirent is provided, which is all hacktv uses.
 * This header is only on the include path for the MSVC kit (see HackTvStudio.pro). */

#ifndef HACKTVSTUDIO_MSVC_DIRENT_H
#define HACKTVSTUDIO_MSVC_DIRENT_H

#ifndef _MSC_VER
#error "This dirent.h shim is for MSVC only"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

/* teletext.c also uses POSIX gmtime_r(); MSVC has gmtime_s() with swapped arguments */
#include <time.h>
#define gmtime_r(timep, result) (gmtime_s((result), (timep)) == 0 ? (result) : NULL)

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    int first;          /* result of FindFirstFile still pending */
    int done;
    struct dirent entry;
} DIR;

static DIR *opendir(const char *path)
{
    DIR *d;
    char pattern[MAX_PATH + 4];
    size_t len;

    if (!path) { errno = EINVAL; return NULL; }
    len = strlen(path);
    if (len == 0 || len + 3 > sizeof(pattern)) { errno = ENAMETOOLONG; return NULL; }

    d = (DIR *)calloc(1, sizeof(DIR));
    if (!d) { errno = ENOMEM; return NULL; }

    strcpy(pattern, path);
    if (pattern[len - 1] != '/' && pattern[len - 1] != '\\')
        strcat(pattern, "\\");
    strcat(pattern, "*");

    d->handle = FindFirstFileA(pattern, &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        errno = ENOENT;
        return NULL;
    }
    d->first = 1;
    return d;
}

static struct dirent *readdir(DIR *d)
{
    if (!d || d->done) return NULL;
    if (d->first) {
        d->first = 0;
    } else if (!FindNextFileA(d->handle, &d->data)) {
        d->done = 1;
        return NULL;
    }
    strncpy(d->entry.d_name, d->data.cFileName, MAX_PATH - 1);
    d->entry.d_name[MAX_PATH - 1] = '\0';
    return &d->entry;
}

static int closedir(DIR *d)
{
    if (!d) return -1;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    free(d);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* HACKTVSTUDIO_MSVC_DIRENT_H */
