/*
 * Copyright (c) 2011 Jiri Zarevucky
 * Copyright (c) 2011 Petr Koupy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libposix
 * @{
 */
/** @file Miscellaneous standard definitions.
 */

#define LIBPOSIX_INTERNAL
#define __POSIX_DEF__(x) posix_##x

#include "internal/common.h"
#include "posix/unistd.h"

#include <errno.h>

#include "posix/string.h"
#include "posix/fcntl.h"

#include "libc/task.h"
#include "libc/thread.h"
#include "libc/stats.h"
#include "libc/malloc.h"
#include "libc/vfs/vfs.h"

#include <libarch/config.h>

// FIXME: replace with a hash table
aoff64_t posix_pos[MAX_OPEN_FILES];

/* Array of environment variable strings (NAME=VALUE). */
char **posix_environ = NULL;
char *posix_optarg;

/**
 * Sleep for the specified number of seconds.
 *
 * Note that POSIX allows this call to be interrupted and then the return
 * value represents remaining seconds for the sleep. HelenOS does not offer
 * such functionality and thus always the whole sleep is taken.
 *
 * @param seconds Number of seconds to sleep.
 * @return Always 0 on HelenOS.
 */
unsigned int posix_sleep(unsigned int seconds)
{
	return thread_sleep(seconds);
}

/**
 * Get current user name.
 *
 * @return User name (static) string or NULL if not found.
 */
char *posix_getlogin(void)
{
	/* There is currently no support for user accounts in HelenOS. */
	return (char *) "user";
}

/**
 * Get current user name.
 *
 * @param name Pointer to a user supplied buffer.
 * @param namesize Length of the buffer.
 * @return Zero on success, error code otherwise.
 */
int posix_getlogin_r(char *name, size_t namesize)
{
	/* There is currently no support for user accounts in HelenOS. */
	if (namesize >= 5) {
		posix_strcpy(name, (char *) "user");
		return 0;
	} else {
		errno = ERANGE;
		return -1;
	}
}

/**
 * Test whether open file descriptor is associated with a terminal.
 *
 * @param fd Open file descriptor to test.
 * @return Boolean result of the test.
 */
int posix_isatty(int fd)
{
	// TODO
	/* Always returns false, because there is no easy way to find
	 * out under HelenOS. */
	return 0;
}

/**
 * Get the pathname of the current working directory.
 *
 * @param buf Buffer into which the pathname shall be put.
 * @param size Size of the buffer.
 * @return Buffer pointer on success, NULL on failure.
 */
char *posix_getcwd(char *buf, size_t size)
{
	if (failed(vfs_cwd_get(buf, size))) 
		return NULL;
	return buf;
}

/**
 * Change the current working directory.
 *
 * @param path New working directory.
 */
int posix_chdir(const char *path)
{
	if (failed(vfs_cwd_set(path)))
		return -1;
	return 0;
}

/**
 * Determine the page size of the current run of the process.
 *
 * @return Page size of the process.
 */
int posix_getpagesize(void)
{
	return PAGE_SIZE;
}

/**
 * Get the process ID of the calling process.
 *
 * @return Process ID.
 */
posix_pid_t posix_getpid(void)
{
	return task_get_id();
}

/**
 * Get the real user ID of the calling process.
 *
 * @return User ID.
 */
posix_uid_t posix_getuid(void)
{
	/* There is currently no support for user accounts in HelenOS. */
	return 0;
}

/**
 * Get the real group ID of the calling process.
 * 
 * @return Group ID.
 */
posix_gid_t posix_getgid(void)
{
	/* There is currently no support for user accounts in HelenOS. */
	return 0;
}

/**
 * Close a file.
 *
 * @param fildes File descriptor of the opened file.
 * @return 0 on success, -1 on error.
 */
int posix_close(int fildes)
{
	posix_pos[fildes] = 0;
	if (failed(vfs_put(fildes)))
		return -1;
	else
		return 0;
}

/**
 * Read from a file.
 *
 * @param fildes File descriptor of the opened file.
 * @param buf Buffer to which the read bytes shall be stored.
 * @param nbyte Upper limit on the number of read bytes.
 * @return Number of read bytes on success, -1 otherwise.
 */
ssize_t posix_read(int fildes, void *buf, size_t nbyte)
{
	size_t nread;
	if (failed(vfs_read(fildes, &posix_pos[fildes], buf, nbyte, &nread)))
		return -1;
	return (ssize_t) nread;
}

/**
 * Write to a file.
 *
 * @param fildes File descriptor of the opened file.
 * @param buf Buffer to write.
 * @param nbyte Size of the buffer.
 * @return Number of written bytes on success, -1 otherwise.
 */
ssize_t posix_write(int fildes, const void *buf, size_t nbyte)
{
	size_t nwr;
	if (failed(vfs_write(fildes, &posix_pos[fildes], buf, nbyte, &nwr)))
		return -1;
	return nwr;
}

/**
 * Reposition read/write file offset
 *
 * @param fildes File descriptor of the opened file.
 * @param offset New offset in the file.
 * @param whence The position from which the offset argument is specified.
 * @return Upon successful completion, returns the resulting offset
 *         as measured in bytes from the beginning of the file, -1 otherwise.
 */
posix_off_t posix_lseek(int fildes, posix_off_t offset, int whence)
{
	vfs_stat_t st;

	switch (whence) {
	case SEEK_SET:
		posix_pos[fildes] = offset;
		break;
	case SEEK_CUR:
		posix_pos[fildes] += offset;
		break;
	case SEEK_END:
		if (failed(vfs_stat(fildes, &st)))
			return -1;
		posix_pos[fildes] = st.size + offset;
		break;
	}
	if (posix_pos[fildes] > INT64_MAX) {
		/* The native width is too large for the POSIX interface. */
		errno = ERANGE;
		return -1;
	}
	return posix_pos[fildes];
}

/**
 * Requests outstanding data to be written to the underlying storage device.
 *
 * @param fildes File descriptor of the opened file.
 * @return Zero on success, -1 otherwise.
 */
int posix_fsync(int fildes)
{
	if (failed(vfs_sync(fildes)))
		return -1;
	else
		return 0;
}

/**
 * Truncate a file to a specified length.
 *
 * @param fildes File descriptor of the opened file.
 * @param length New length of the file.
 * @return Zero on success, -1 otherwise.
 */
int posix_ftruncate(int fildes, posix_off_t length)
{
	if (failed(vfs_resize(fildes, (aoff64_t) length)))
		return -1;
	else
		return 0;
}

/**
 * Remove a directory.
 *
 * @param path Directory pathname.
 * @return Zero on success, -1 otherwise.
 */
int posix_rmdir(const char *path)
{
	if (failed(vfs_unlink_path(path)))
		return -1;
	else
		return 0;
}

/**
 * Remove a link to a file.
 * 
 * @param path File pathname.
 * @return Zero on success, -1 otherwise.
 */
int posix_unlink(const char *path)
{
	if (failed(vfs_unlink_path(path)))
		return -1;
	else
		return 0;
}

/**
 * Duplicate an open file descriptor.
 *
 * @param fildes File descriptor to be duplicated.
 * @return On success, new file descriptor for the same file, otherwise -1.
 */
int posix_dup(int fildes)
{
	return posix_fcntl(fildes, F_DUPFD, 0);
}

/**
 * Duplicate an open file descriptor.
 * 
 * @param fildes File descriptor to be duplicated.
 * @param fildes2 File descriptor to be paired with the same file description
 *     as is paired fildes.
 * @return fildes2 on success, -1 otherwise.
 */
int posix_dup2(int fildes, int fildes2)
{
	int file;
	if (failed(vfs_clone(fildes, fildes2, false, &file))) {
		return -1;
	}
	return file;
}

/**
 * Determine accessibility of a file.
 *
 * @param path File to check accessibility for.
 * @param amode Either check for existence or intended access mode.
 * @return Zero on success, -1 otherwise.
 */
int posix_access(const char *path, int amode)
{
	if (amode == F_OK || (amode & (X_OK | W_OK | R_OK))) {
		/* HelenOS doesn't support permissions, permission checks
		 * are equal to existence check.
		 *
		 * Check file existence by attempting to open it.
		 */
		int fd = posix_open(path, O_RDONLY);
		if (fd < 0)
			return -1;
		posix_close(fd);
		return 0;
	} else {
		/* Invalid amode argument. */
		errno = EINVAL;
		return -1;
	}
}

/**
 * Get configurable system variables.
 * 
 * @param name Variable name.
 * @return Variable value.
 */
long posix_sysconf(int name)
{
	long clk_tck = 0;
	size_t cpu_count = 0;
	stats_cpu_t *cpu_stats = stats_get_cpus(&cpu_count);
	if (cpu_stats && cpu_count > 0) {
		clk_tck = ((long) cpu_stats[0].frequency_mhz) * 1000000L;
	}
	if (cpu_stats) {
		free(cpu_stats);
		cpu_stats = 0;
	}

	long phys_pages = 0;
	long avphys_pages = 0;
	stats_physmem_t *mem_stats = stats_get_physmem();
	if (mem_stats) {
		phys_pages = (long) (mem_stats->total / posix_getpagesize());
		avphys_pages = (long) (mem_stats->free / posix_getpagesize());
		free(mem_stats);
		mem_stats = 0;
	}

	switch (name) {
	case _SC_PHYS_PAGES:
		return phys_pages;
	case _SC_AVPHYS_PAGES:
		return avphys_pages;
	case _SC_PAGESIZE:
		return posix_getpagesize();
	case _SC_CLK_TCK:
		return clk_tck;
	default:
		errno = EINVAL;
		return -1;
	}
}

/**
 * 
 * @param path
 * @param name
 * @return
 */
long posix_pathconf(const char *path, int name)
{
	// TODO: low priority, just a compile-time dependency of binutils
	not_implemented();
	return -1;
}

/**
 * 
 * @return
 */
posix_pid_t posix_fork(void)
{
	// TODO: low priority, just a compile-time dependency of binutils
	not_implemented();
	return -1;
}

/**
 * 
 * @param path
 * @param argv
 * @return
 */
int posix_execv(const char *path, char *const argv[])
{
	// TODO: low priority, just a compile-time dependency of binutils
	not_implemented();
	return -1;
}

/**
 * 
 * @param file
 * @param argv
 * @return
 */
int posix_execvp(const char *file, char *const argv[])
{
	// TODO: low priority, just a compile-time dependency of binutils
	not_implemented();
	return -1;
}

/**
 * 
 * @param fildes
 * @return
 */
int posix_pipe(int fildes[2])
{
	// TODO: low priority, just a compile-time dependency of binutils
	not_implemented();
	return -1;
}

unsigned int posix_alarm(unsigned int seconds)
{
	not_implemented();
	return 0;
}

/** @}
 */
