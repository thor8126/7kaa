// Android: minimal glob() for the game's Directory class (src/ODIR.cpp).
//
// Bionic only has glob() from API 28 (Android 9), but the app also runs on
// older devices and emulators (e.g. Nox, Android 7). This header is found
// before the system <glob.h> and maps glob/globfree/glob_t to our own
// implementation (android_glob.cpp) under different symbol names, so it never
// clashes with libc's glob on newer Android versions.
//
// Supported: wildcards (* ? [..]) in the last path component only, which is
// all the game uses ("dir/*.SAV", "*.SCN", ...). Flags: GLOB_NOSORT.

#ifndef SEVENKAA_COMPAT_GLOB_H
#define SEVENKAA_COMPAT_GLOB_H

#include <stddef.h>

#define glob_t   sevenkaa_glob_t
#define glob     sevenkaa_glob
#define globfree sevenkaa_globfree

#ifndef GLOB_NOSORT
#define GLOB_NOSORT 0x0020
#endif
#ifndef GLOB_NOMATCH
#define GLOB_NOMATCH 3
#endif
#ifndef GLOB_NOSPACE
#define GLOB_NOSPACE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	size_t gl_pathc;
	char **gl_pathv;
	size_t gl_offs;
} sevenkaa_glob_t;

int sevenkaa_glob(const char *pattern, int flags,
                  int (*errfunc)(const char *epath, int eerrno),
                  sevenkaa_glob_t *pglob);
void sevenkaa_globfree(sevenkaa_glob_t *pglob);

#ifdef __cplusplus
}
#endif

#endif
