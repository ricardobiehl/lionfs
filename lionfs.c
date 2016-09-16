/*
 * lionfs, The Link Over Network File System
 * Copyright (C) 2016  Ricardo Biehl Pasquali <rbpoficial@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "array.h"
#include "lionfs.h"
#include "network.h"

#define file_count (array_object_get_last((Array) files) + 1)

lionfile_t **files;
pthread_rwlock_t rwlock;

static lionfile_t*
get_file_by_path (const char *path) {
	unsigned int i;

	for (i = 0; i < file_count; i++)
		if(strcmp(path, files[i]->path) == 0)
			return files[i];

	return NULL;
}

static lionfile_t*
get_file_by_ff (const char *path) {
	if (strncmp(path, "/.ff/", 5) == 0) {
		path += 4;
		return get_file_by_path(path);
	}

	return NULL;
}


// ================
// Fuse operations:
// ================

static int
lion_getattr (const char *path, struct stat *buf) {
	lionfile_t *file;

	memset(buf, 0, sizeof(struct stat));

	if (strcmp(path, "/") == 0) {
		buf->st_mode = S_IFDIR | 0775;
		buf->st_nlink = 2;
		return 0;
	}

	if (strcmp(path, "/.ff") == 0) {
		/* fakefiles directory is read-only */
		buf->st_mode = S_IFDIR | 0444;
		buf->st_nlink = 0;
		return 0;
	}

	pthread_rwlock_rdlock(&rwlock);

	if ((file = get_file_by_ff(path)) != NULL) {
		/* It's a fakefile! */

		buf->st_mode = S_IFREG | 0444;
		buf->st_mtime = file->mtime;
		buf->st_nlink = 0;
		buf->st_size = file->size;

		pthread_rwlock_unlock(&rwlock);
		return 0;
	}

	if ((file = get_file_by_path(path)) == NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOENT;
	}

	buf->st_mode = file->mode;
	buf->st_size = file->size;
	buf->st_mtime = file->mtime;
	buf->st_nlink = 1;

	pthread_rwlock_unlock(&rwlock);

	return 0;
}

static int
lion_readlink (const char *path, char *buf, size_t len) {
	lionfile_t *file;

	pthread_rwlock_rdlock(&rwlock);

	if ((file = get_file_by_path(path)) == NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOENT;
	}

	snprintf(buf, len, ".ff/%s", file->path + 1);

	pthread_rwlock_unlock(&rwlock);

	return 0;
}

static int
lion_unlink (const char *path) {
	lionfile_t *file;

	pthread_rwlock_wrlock(&rwlock);

	if ((file = get_file_by_path(path)) == NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOENT;
	}

	array_object_unlink((Array) files, file);

	pthread_rwlock_unlock(&rwlock);

	free(file->path);
	free(file->url);
	array_object_free(file);

	return 0;
}

static int
lion_symlink (const char *url, const char *path) {
	lionfile_t network_file;
	lionfile_t *file;

	/* Check if URL exists and get its info */
	if (network_file_get_valid((char*) url))
		return -EHOSTUNREACH;
	if (network_file_get_info((char*) url, &network_file))
		return -EHOSTUNREACH;

	pthread_rwlock_wrlock(&rwlock);

	if (file_count == MAX_FILES) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOMEM;
	}

	if (get_file_by_path(path) != NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -EEXIST;
	}

	file = array_object_alloc(sizeof(lionfile_t));

	if (file == NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOMEM;
	}

	file->path = malloc(strlen(path) + 1);
	file->url = malloc(strlen(url) + 1);

	strcpy(file->path, path);
	strcpy(file->url, url);

	file->mode = S_IFLNK | 0444;

	/* TODO do not use `network_file`
	 * replace it with a lionfileinfo_t structure (for example) */
	file->size = network_file.size;
	file->mtime = network_file.mtime;

	array_object_link((Array) files, file);

	pthread_rwlock_unlock(&rwlock);

	return 0;
}

static int
lion_rename (const char *oldpath, const char *newpath) {
	lionfile_t *file;
	char **path_pointer;
	size_t newsize;

	pthread_rwlock_wrlock(&rwlock);

	if (get_file_by_path(newpath) != NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -EEXIST;
	}
	if ((file = get_file_by_path(oldpath)) == NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOENT;
	}

	path_pointer = &file->path;
	newsize = strlen(newpath) + 1;

	if (newsize == 1) {
		pthread_rwlock_unlock(&rwlock);
		return -EINVAL;
	}

	*path_pointer = realloc(*path_pointer, newsize);

	memcpy(*path_pointer, newpath, newsize);

	pthread_rwlock_unlock(&rwlock);

	return 0;
}

static int
lion_read (const char *path, char *buf, size_t size, off_t off,
	struct fuse_file_info *fi) {
	lionfile_t *file;
	size_t ret = 0;

	pthread_rwlock_rdlock(&rwlock);

	if ((file = get_file_by_ff(path)) == NULL) {
		pthread_rwlock_unlock(&rwlock);
		return -ENOENT;
	}

	if (off < file->size) {
		if(off + size > file->size)
			size = file->size - off;
	} else {
		pthread_rwlock_unlock(&rwlock);
		return 0;
	}

	ret = network_file_get_data(file->url, size, off, buf);

	pthread_rwlock_unlock(&rwlock);

	return ret;
}

static int
lion_readdir (const char *path, void *buf, fuse_fill_dir_t filler, off_t off,
	struct fuse_file_info *fi) {
	int i;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	pthread_rwlock_rdlock(&rwlock);

	for (i = 0; i < file_count; i++)
		filler(buf, files[i]->path + 1, NULL, 0);

	pthread_rwlock_unlock(&rwlock);

	return 0;
}

static struct fuse_operations fuseopr = {
	.getattr = lion_getattr,
	.readlink = lion_readlink,
	.unlink = lion_unlink,
	.symlink = lion_symlink,
	.rename = lion_rename,
	.read = lion_read,
	.readdir = lion_readdir,
};

int
main (int argc, char **argv) {
	int ret = 0;

	// Init rwlock
	if(pthread_rwlock_init(&rwlock, NULL))
		return 1;

	// Create files array.
	if ((files = (lionfile_t**) array_new(MAX_FILES)) == NULL) {
		ret = 1;
		goto _go_destroy_rwlock;
	}

	// Init network
	network_init();

	// Open all network modules available.
	network_open_all_modules();

	// Main routine. It initializes FUSE and set the operations (&fuseopr).
	fuse_main(argc, argv, &fuseopr, NULL);

	// Close all network modules.
	network_close_all_modules();

	// Delete files array.
	array_del((Array) files);

_go_destroy_rwlock:
	// Destroy rwlock
	pthread_rwlock_destroy(&rwlock);

	return ret;
}
