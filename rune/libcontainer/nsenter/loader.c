#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

/* Defined in nsexec.c. */

#define PANIC   "panic"
#define FATAL   "fatal"
#define ERROR   "error"
#define WARNING "warning"
#define INFO    "info"
#define DEBUG   "debug"

void write_log_with_info(const char *level, const char *function, int line, const char *format, ...);

#define write_log(level, fmt, ...) \
	write_log_with_info((level), __FUNCTION__, __LINE__, (fmt), ##__VA_ARGS__)

struct pal_attr_t {
	const char *args;
	const char *log_level;
};

struct pal_stdio_fds {
	int stdin, stdout, stderr;
};


int (*fptr_pal_get_version)(void);
int (*fptr_pal_init)(const struct pal_attr_t *attr);
int (*fptr_pal_exec)(const char *path, const char * const argv[],
			const struct pal_stdio_fds *stdio, int *exit_code);
int (*fptr_pal_kill)(int sig, int pid);
int (*fptr_pal_destroy)(void);

int is_enclave(void)
{
	const char *env;
	env = getenv("_LIBCONTAINER_PAL_PATH");
	if (env == NULL || *env == '\0')
		return 0;
	return 1;
}

int load_enclave_runtime(void)
{
	const char *file;
	const char *rootfs;
	void *dl;

	file = getenv("_LIBCONTAINER_PAL_PATH");
	if (file == NULL || *file == '\0') {
		write_log(DEBUG, "invalid environment _LIBCONTAINER_PAL_PATH");
		return -EINVAL;
	}
	write_log(DEBUG, "_LIBCONTAINER_PAL_PATH = %s", file);

	/* dlopen */
	rootfs = getenv("_LIBCONTAINER_PAL_ROOTFS");
	if (rootfs && *rootfs != '\0') {
		char sofile[BUFSIZ];
		char ldpath[BUFSIZ];
		const char *env_ldpath;

		if (*file != '/') {
			write_log(DEBUG, "_LIBCONTAINER_PAL_PATH must be a absolute path");
			return -ENOSPC;
		}
		snprintf(sofile, sizeof(sofile), "%s/%s", rootfs, file);
		snprintf(ldpath, sizeof(ldpath), "%s/lib64", rootfs);

		env_ldpath = getenv("LD_LIBRARY_PATH");
		if (env_ldpath && *env_ldpath != '\0') {
			char *saved_ldpath = strdup(env_ldpath);
			if (saved_ldpath == NULL)
				return -ENOMEM;
			setenv("LD_LIBRARY_PATH", ldpath, 1);
			dl = dlopen(sofile, RTLD_NOW);
			setenv("LD_LIBRARY_PATH", saved_ldpath, 1);
			free(saved_ldpath);
		} else {
			setenv("LD_LIBRARY_PATH", ldpath, 1);
			dl = dlopen(sofile, RTLD_NOW);
			unsetenv("LD_LIBRARY_PATH");
		}
	} else {
		dl = dlopen(file, RTLD_NOW);
	}

	if (dl == NULL) {
		write_log(DEBUG, "dlopen(): %s", dlerror());
		return -ENOEXEC;
	}

#define DLSYM(fn)								\
	do {									\
		fptr_pal_ ## fn = dlsym(dl, "pal_" #fn);				\
		write_log(DEBUG, "dlsym(%s) = %p", "pal_" #fn, fptr_pal_ ## fn);	\
	} while (0)

	DLSYM(get_version);
	DLSYM(init);
	DLSYM(exec);
	DLSYM(kill);
	DLSYM(destroy);
#undef DLSYM

	return 0;
}