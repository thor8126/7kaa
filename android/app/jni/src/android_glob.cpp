// Android: minimal glob() implementation, see compat/glob.h.

#include <dirent.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#include <glob.h>   // compat/glob.h

extern "C" int sevenkaa_glob(const char *pattern, int flags,
                             int (*errfunc)(const char *, int),
                             sevenkaa_glob_t *pglob)
{
	(void) errfunc;
	pglob->gl_pathc = 0;
	pglob->gl_pathv = NULL;
	pglob->gl_offs = 0;

	std::string pat(pattern);
	size_t slash = pat.rfind('/');
	std::string dir = (slash == std::string::npos) ? "." : pat.substr(0, slash ? slash : 1);
	std::string prefix = (slash == std::string::npos) ? "" : pat.substr(0, slash + 1);
	std::string name = (slash == std::string::npos) ? pat : pat.substr(slash + 1);

	std::vector<std::string> found;
	DIR *d = opendir(dir.c_str());
	if( d )
	{
		struct dirent *e;
		while( (e = readdir(d)) != NULL )
		{
			if( e->d_name[0] == '.' && name[0] != '.' )
				continue;
			if( fnmatch(name.c_str(), e->d_name, FNM_PERIOD) == 0 )
				found.push_back(prefix + e->d_name);
		}
		closedir(d);
	}
	if( found.empty() )
		return GLOB_NOMATCH;
	if( !(flags & GLOB_NOSORT) )
		std::sort(found.begin(), found.end());

	pglob->gl_pathv = (char**) calloc(found.size() + 1, sizeof(char*));
	if( !pglob->gl_pathv )
		return GLOB_NOSPACE;
	for( size_t i = 0; i < found.size(); i++ )
		pglob->gl_pathv[i] = strdup(found[i].c_str());
	pglob->gl_pathc = found.size();
	return 0;
}

extern "C" void sevenkaa_globfree(sevenkaa_glob_t *pglob)
{
	if( pglob->gl_pathv )
	{
		for( size_t i = 0; i < pglob->gl_pathc; i++ )
			free(pglob->gl_pathv[i]);
		free(pglob->gl_pathv);
	}
	pglob->gl_pathc = 0;
	pglob->gl_pathv = NULL;
}
