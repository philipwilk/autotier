/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2020			 Josh Boudreau <jboudreau@45drives.com>
	Copyright (C) 2001-2007	Miklos Szeredi <miklos@szeredi.hu>
	Copyright (C) 2011			 Sebastian Pipping <sebastian@pipping.org>

	This program can be distributed under the terms of the GNU GPLv2.
	See the file COPYING.
*/

/** @file
 *
 * Adapted from example code from libfuse, handles filsystem calls
 *
 * Compile with:
 *
 *		 gcc -Wall passthrough.c `pkg-config fuse3 --cflags --libs` -lulockmgr -o passthrough_fh
 */

#ifdef USE_FUSE
#include "fuse_handler.hpp"
#include "tierEngine.hpp"
#include "tier.hpp"
#include <list>
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

//const char *backend = "/mnt/tier1/";

std::list<Tier> *tiers_ptr = NULL;

const char *find_backend_path(const char *path){
	if(tiers_ptr){
		for(std::list<Tier>::iterator titr = tiers_ptr->begin(); titr != tiers_ptr->end(); ++titr){
			if(exists(titr->dir/fs::path(path)))
				return (titr->dir/fs::path(path)).c_str();
		}
	}
	return "";
}

extern "C" {
	#define FUSE_USE_VERSION 30

	#ifdef HAVE_CONFIG_H
	#include <config.h>
	#endif

	#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
	#endif

	#ifdef linux
	/* For pread()/pwrite()/utimensat() */
	#define _XOPEN_SOURCE 700
	#endif

	#include <fuse.h>
	#include <stdlib.h>
	#include <stdio.h>
	#include <string.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <sys/stat.h>
	#include <dirent.h>
	#include <errno.h>
	#ifdef __FreeBSD__
	#include <sys/socket.h>
	#include <sys/un.h>
	#endif
	#include <sys/time.h>
	#ifdef HAVE_SETXATTR
	#include <sys/xattr.h>
	#endif

	#include "passthrough_helpers.h"

	static void *at_init(struct fuse_conn_info *conn,
					struct fuse_config *cfg)
	{
		(void) conn;
		cfg->use_ino = 1;

		/* Pick up changes from lower filesystem right away. This is
			 also necessary for better hardlink support. When the kernel
			 calls the unlink() handler, it does not know the inode of
			 the to-be-removed entry and can therefore not invalidate
			 the cache of the associated inode - resulting in an
			 incorrect st_nlink value being reported for any remaining
			 hardlinks to this inode. */
		cfg->entry_timeout = 0;
		cfg->attr_timeout = 0;
		cfg->negative_timeout = 0;

		return NULL;
	}

	static int at_getattr(const char *path, struct stat *stbuf,
					 struct fuse_file_info *fi)
	{
		(void) fi;
		int res;

		res = lstat(find_backend_path(path), stbuf);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_access(const char *path, int mask)
	{
		int res;

		res = access(find_backend_path(path), mask);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_readlink(const char *path, char *buf, size_t size)
	{
		int res;

		res = readlink(find_backend_path(path), buf, size - 1);
		if (res == -1)
			return -errno;

		buf[res] = '\0';
		return 0;
	}


	static int at_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
					 off_t offset, struct fuse_file_info *fi,
					 enum fuse_readdir_flags flags)
	{
		DIR *dp;
		struct dirent *de;

		(void) offset;
		(void) fi;
		(void) flags;

		dp = opendir(find_backend_path(path));
		if (dp == NULL)
			return -errno;

		while ((de = readdir(dp)) != NULL) {
			struct stat st;
			memset(&st, 0, sizeof(st));
			st.st_ino = de->d_ino;
			st.st_mode = de->d_type << 12;
			if (filler(buf, de->d_name, &st, 0, (fuse_fill_dir_flags)0))
				break;
		}

		closedir(dp);
		return 0;
	}

	static int at_mknod(const char *path, mode_t mode, dev_t rdev)
	{
		int res;

		res = mknod_wrapper(AT_FDCWD, find_backend_path(path), NULL, mode, rdev);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_mkdir(const char *path, mode_t mode)
	{
		int res;

		res = mkdir(find_backend_path(path), mode);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_unlink(const char *path)
	{
		int res;

		res = unlink(find_backend_path(path));
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_rmdir(const char *path)
	{
		int res;

		res = rmdir(find_backend_path(path));
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_symlink(const char *from, const char *to)
	{
		int res;

		res = symlink(find_backend_path(from), find_backend_path(to));
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_rename(const char *from, const char *to, unsigned int flags)
	{
		int res;

		if (flags)
			return -EINVAL;

		res = rename(find_backend_path(from), find_backend_path(to));
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_link(const char *from, const char *to)
	{
		int res;

		res = link(find_backend_path(from), find_backend_path(to));
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_chmod(const char *path, mode_t mode,
				 struct fuse_file_info *fi)
	{
		(void) fi;
		int res;

		res = chmod(find_backend_path(path), mode);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_chown(const char *path, uid_t uid, gid_t gid,
				 struct fuse_file_info *fi)
	{
		(void) fi;
		int res;

		res = lchown(find_backend_path(path), uid, gid);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_truncate(const char *path, off_t size,
				struct fuse_file_info *fi)
	{
		int res;

		if (fi != NULL)
			res = ftruncate(fi->fh, size);
		else
			res = truncate(find_backend_path(path), size);
		if (res == -1)
			return -errno;

		return 0;
	}

	#ifdef HAVE_UTIMENSAT
	static int at_utimens(const char *path, const struct timespec ts[2],
					 struct fuse_file_info *fi)
	{
		(void) fi;
		int res;

		/* don't use utime/utimes since they follow symlinks */
		res = utimensat(0, find_backend_path(path), ts, AT_SYMLINK_NOFOLLOW);
		if (res == -1)
			return -errno;

		return 0;
	}
	#endif

	static int at_create(const char *path, mode_t mode,
					struct fuse_file_info *fi)
	{
		int res;

		res = open(find_backend_path(path), fi->flags, mode);
		if (res == -1)
			return -errno;

		fi->fh = res;
		return 0;
	}

	static int at_open(const char *path, struct fuse_file_info *fi)
	{
		int res;

		res = open(find_backend_path(path), fi->flags);
		if (res == -1)
			return -errno;

		fi->fh = res;
		return 0;
	}

	static int at_read(const char *path, char *buf, size_t size, off_t offset,
				struct fuse_file_info *fi)
	{
		int fd;
		int res;

		if(fi == NULL)
			fd = open(find_backend_path(path), O_RDONLY);
		else
			fd = fi->fh;
		
		if (fd == -1)
			return -errno;

		res = pread(fd, buf, size, offset);
		if (res == -1)
			res = -errno;

		if(fi == NULL)
			close(fd);
		return res;
	}

	static int at_write(const char *path, const char *buf, size_t size,
				 off_t offset, struct fuse_file_info *fi)
	{
		int fd;
		int res;

		(void) fi;
		if(fi == NULL)
			fd = open(find_backend_path(path), O_WRONLY);
		else
			fd = fi->fh;
		
		if (fd == -1)
			return -errno;

		res = pwrite(fd, buf, size, offset);
		if (res == -1)
			res = -errno;

		if(fi == NULL)
			close(fd);
		return res;
	}

	static int at_statfs(const char *path, struct statvfs *stbuf)
	{
		int res;

		res = statvfs(find_backend_path(path), stbuf);
		if (res == -1)
			return -errno;

		return 0;
	}

	static int at_release(const char *path, struct fuse_file_info *fi)
	{
		(void) path;
		close(fi->fh);
		return 0;
	}

	static int at_fsync(const char *path, int isdatasync,
				 struct fuse_file_info *fi)
	{
		/* Just a stub.	 This method is optional and can safely be left
			 unimplemented */

		(void) path;
		(void) isdatasync;
		(void) fi;
		return 0;
	}

	#ifdef HAVE_POSIX_FALLOCATE
	static int at_fallocate(const char *path, int mode,
				off_t offset, off_t length, struct fuse_file_info *fi)
	{
		int fd;
		int res;

		(void) fi;

		if (mode)
			return -EOPNOTSUPP;

		if(fi == NULL)
			fd = open(find_backend_path(path), O_WRONLY);
		else
			fd = fi->fh;
		
		if (fd == -1)
			return -errno;

		res = -posix_fallocate(fd, offset, length);

		if(fi == NULL)
			close(fd);
		return res;
	}
	#endif

	#ifdef HAVE_SETXATTR
	/* xattr operations are optional and can safely be left unimplemented */
	static int at_setxattr(const char *path, const char *name, const char *value,
				size_t size, int flags)
	{
		int res = lsetxattr(find_backend_path(path), name, value, size, flags);
		if (res == -1)
			return -errno;
		return 0;
	}

	static int at_getxattr(const char *path, const char *name, char *value,
				size_t size)
	{
		int res = lgetxattr(find_backend_path(path), name, value, size);
		if (res == -1)
			return -errno;
		return res;
	}

	static int at_listxattr(const char *path, char *list, size_t size)
	{
		int res = llistxattr(find_backend_path(path), list, size);
		if (res == -1)
			return -errno;
		return res;
	}

	static int at_removexattr(const char *path, const char *name)
	{
		int res = lremovexattr(find_backend_path(path), name);
		if (res == -1)
			return -errno;
		return 0;
	}
	#endif /* HAVE_SETXATTR */

	#ifdef HAVE_COPY_FILE_RANGE
	static ssize_t at_copy_file_range(const char *path_in,
						 struct fuse_file_info *fi_in,
						 off_t offset_in, const char *path_out,
						 struct fuse_file_info *fi_out,
						 off_t offset_out, size_t len, int flags)
	{
		int fd_in, fd_out;
		ssize_t res;

		if(fi_in == NULL)
			fd_in = open(find_backend_path(path_in), O_RDONLY);
		else
			fd_in = fi_in->fh;

		if (fd_in == -1)
			return -errno;

		if(fi_out == NULL)
			fd_out = open(find_backend_path(path_out), O_WRONLY);
		else
			fd_out = fi_out->fh;

		if (fd_out == -1) {
			close(fd_in);
			return -errno;
		}

		res = copy_file_range(fd_in, &offset_in, fd_out, &offset_out, len,
						flags);
		if (res == -1)
			res = -errno;

		close(fd_in);
		close(fd_out);

		return res;
	}
	#endif

	static off_t at_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
	{
		int fd;
		off_t res;

		if (fi == NULL)
			fd = open(find_backend_path(path), O_RDONLY);
		else
			fd = fi->fh;

		if (fd == -1)
			return -errno;

		res = lseek(fd, off, whence);
		if (res == -1)
			res = -errno;

		if (fi == NULL)
			close(fd);
		return res;
	}

	static const struct fuse_operations at_oper = {
		.getattr	= at_getattr,
		.readlink	= at_readlink,
		.mknod		= at_mknod,
		.mkdir		= at_mkdir,
		.unlink		= at_unlink,
		.rmdir		= at_rmdir,
		.symlink	= at_symlink,
		.rename		= at_rename,
		.link		= at_link,
		.chmod		= at_chmod,
		.chown		= at_chown,
		.truncate	= at_truncate,
		.open		= at_open,
		.read		= at_read,
		.write		= at_write,
		.statfs		= at_statfs,
		.release	= at_release,
		.fsync		= at_fsync,
	#ifdef HAVE_SETXATTR
		.setxattr	= at_setxattr,
		.getxattr	= at_getxattr,
		.listxattr	= at_listxattr,
		.removexattr	= at_removexattr,
	#endif
		.readdir	= at_readdir,
		.init			 = at_init,
		.access		= at_access,
		.create 	= at_create,
	#ifdef HAVE_UTIMENSAT
		.utimens	= at_utimens,
	#endif
	#ifdef HAVE_POSIX_FALLOCATE
		.fallocate	= at_fallocate,
	#endif
	#ifdef HAVE_COPY_FILE_RANGE
		.copy_file_range = at_copy_file_range,
	#endif
		.lseek		= at_lseek,
	};

	int mount_autotier(int argc, char **argv) {
		return fuse_main(argc, argv, &at_oper, NULL);
	}
	
}
#endif
