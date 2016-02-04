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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#define FUSE_USE_VERSION 26

#include <fuse.h>

#include "lionfs.h"
#include "network.h"
#include "array.h"

/*
 * TODO It's not working!
 * Create nodeids for fakefiles after symlink have been created.
 * nodeid 1 is "/"
 * nodeids 2 v 4 v 6 v 8 ... are symlinks
 * nodeids   3 ^ 5 ^ 7 ^ 9 ... are fakefiles
 */

/* TODO
 * # Put fakefiles in a fake directory `.ff/`
 *   Example: `readlink music.mp3` -> .ff/music.mp3
 * # No nodeids for fakefiles.
 */

_filelist_t filelist = { 0, };

inline int8_t*
get_binfo(int fid)
{
	return (filelist.file[fid])->binfo;
}

inline int
set_binfo(int fid, int8_t *binfo)
{
	if(binfo == NULL)
		return -1;
	(filelist.file[fid])->binfo = binfo;
	return 0;
}

inline time_t
get_mtime(int fid)
{
	return (filelist.file[fid])->mtime;
}

inline mode_t
get_mode(int fid)
{
	return (filelist.file[fid])->mode;
}

inline long long
get_size(int fid)
{
	return (filelist.file[fid])->size;
}

inline char*
get_url(int fid)
{
	return (filelist.file[fid])->url;
}

inline char*
get_path(int fid)
{
	return (filelist.file[fid])->path;
}

int
get_fid_by_path(const char *path)
{
	int i;

	for(i = 0; i < filelist.count; i++)
	{
		if(strcmp(path, get_path(i)) == 0)
			return i;
	}
	return -ENOENT;
}

int
get_fid_by_url(const char *url)
{
	int i;

	for(i = 0; i < filelist.count; i++)
	{
		if(strcmp(url, get_url(i)) == 0)
			return i;
	}
	return -ENOENT;

}

int
string_to_fid(const char *string)
{
	int fid;
	char *endptr;

	fid = strtol(string, &endptr, 10);

	if((endptr - string) != 2)
		return -1;
	if(fid < 0 || fid > (filelist.count - 1))
		return -1;
	if(filelist.file[fid] == NULL)
		return -1;

	return fid;
}

int
remove_fid(int fid)
{
	_file_t *file;
	
	file = filelist.file[fid];

	array_object_unlink((Array) filelist.file, fid);

	filelist.count--;

	free(file->path);
	free(file->url);

	array_object_free(file);

	return 0;
}

int
add_fid(const char *path, const char *url, mode_t mode)
{
	_file_t *file;

	file = array_object_alloc(sizeof(_file_t));

	if(file == NULL)
		return -1;

	file->path = malloc(strlen(path) + 1);
	file->url = malloc(strlen(url) + 1);

	strcpy(file->path, path);
	strcpy(file->url, url);

	file->mode = mode;

	network_file_get_info(file->url, file);

	array_object_link((Array) filelist.file, file);

	filelist.count++;

	return 0;
}

int
rename_fid(int fid, const char *path)
{
	/* TODO */
}

int
lion_getattr(const char *path, struct stat *buf)
{
	int fid;

	memset(buf, 0, sizeof(struct stat));

	if(strcmp(path, "/") == 0)
	{
		buf->st_mode = S_IFDIR | 0775;
		buf->st_nlink = 2;
		return 0;
	}
#if 0
/* see TODO above */
	if(strcmp(path, "/.ff") == 0)
	{
		buf->st_mode = S_IFDIR | 0444;
		buf->st_nlink = 1;
		return 0;
	}

	if(strcmp(path, "/.ff/") > 0)
	{
		/*TODO I think ready*/
		path += 4;

		fid = get_fid_by_path(path);

		if(fid < 0)
			return -ENOENT;

		buf->st_mode = S_IFREG | 0444;
		buf->st_size = get_size(fid);
		buf->st_mtime = get_mtime(fid);
		buf->st_ino = 0;
		buf->st_nlink = 0;

		return 0;
	}
#endif
	fid = get_fid_by_path(path);

	if(fid < 0)
	{
		if((fid = string_to_fid(path + 1)) < 0)
			return -ENOENT;
		buf->st_mode = S_IFREG | 0444;
		buf->st_size = get_size(fid);
		buf->st_mtime = get_mtime(fid);
		buf->st_ino = 0;
		buf->st_nlink = 0;
		return 0;
	}

	buf->st_mode = get_mode(fid);
	buf->st_size = get_size(fid);
	buf->st_mtime = get_mtime(fid);
	buf->st_nlink = 1;

	return 0;
}

int
lion_readlink(const char *path, char *buf, size_t len)
{
	int fid;

	fid = get_fid_by_path(path);

	if(fid < 0)
		return -ENOENT;

	sprintf(buf, "%.2d", fid);

	return 0;
}

int
lion_unlink(const char *path)
{
	int fid;

	fid = get_fid_by_path(path);

	if(fid < 0)
		return -ENOENT;

	remove_fid(fid);

	return 0;
}

int
lion_symlink(const char *linkname, const char *path)
{
	int fid;

	if(get_fid_by_path(path) >= 0)
		return -EEXIST;

	if(network_file_get_valid((char*) linkname))
		return -EHOSTUNREACH;

	fid = add_fid(path, linkname, S_IFLNK | 0444);

	if(fid < 0)
		return -ENOMEM;

	return 0;
}

int
lion_rename(const char *oldpath, const char *newpath)
{
	return -ENOSYS; /*TODO*/

	int fid;

	fid = get_fid_by_path(oldpath);

	if(fid < 0)
		return -ENOENT;

	rename_fid(fid, newpath);

	return 0;
}

int
lion_read(const char *path, char *buf, size_t size, off_t off,
	struct fuse_file_info *fi)
{
	int fid;

	if((fid = string_to_fid(path + 1)) < 0)
		return -ENOENT;
#if 0
	/* TODO */
	if((fid = get_fid_by_ff(path)) < 0)
		return -ENOENT;
#endif

	if(off < get_size(fid))
	{
		if(off + size > get_size(fid))
			size = get_size(fid) - off;
	}
	else
		return 0;

	return network_file_get_data(get_url(fid), size, off, buf);
}

int
lion_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off,
	struct fuse_file_info *fi)
{
	if(strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	int i;

	for(i = 0; i < filelist.count; i++)
	{
		filler(buf, get_path(i) + 1, NULL, 0);
	}
	return 0;
}

struct fuse_operations fuseopr =
{
	.getattr = lion_getattr,
	.readlink = lion_readlink,
	.unlink = lion_unlink,
	.symlink = lion_symlink,
	.rename = lion_rename,
	.read = lion_read,
	.readdir = lion_readdir,
};

int
main(int argc, char **argv)
{
	int ret = 0;

	if((filelist.file = (_file_t**) array_new(MAX_FILES)) == NULL)
		return 1;

	if(network_open_module("http") == -1)
	{
		ret = 1;
		goto _go_free;
	}

	fuse_main(argc, argv, &fuseopr, NULL);

	// network_close_module(); TODO do not working!

_go_free:
	array_del((Array) filelist.file);

	return ret;
}