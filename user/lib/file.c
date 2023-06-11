#include <fs.h>
#include <lib.h>

#define debug 0

static int file_close(struct Fd *fd);
static int file_read(struct Fd *fd, void *buf, u_int n, u_int offset);
static int file_write(struct Fd *fd, const void *buf, u_int n, u_int offset);
static int file_stat(struct Fd *fd, struct Stat *stat);

// Dot represents choosing the member within the struct declaration
// to initialize, with no need to consider the order of members.
struct Dev devfile = {
	.dev_id = 'f',
	.dev_name = "file",
	.dev_read = file_read,
	.dev_write = file_write,
	.dev_close = file_close,
	.dev_stat = file_stat,
};
// return size, get fileid 
static int getInfoFromFileFd(struct Fd* fd,u_int*fileid) {
	struct Filefd* ffd = (struct Filefd*)fd;
	if (fileid) {
		*fileid = ffd->f_fileid; 
	}
	return ffd->f_file.f_size;
}

static int getInfoFromFatFileFd(struct Fd* fd, u_int *fileid) {
	struct FatFileFd *ffd = (struct FatFileFd*)fd;
	if (fileid) {
		*fileid = ffd->f_fileid;
	}
	return ffd->f_file.DIR_FileSize;
}

static int getInfoFromFd(struct Fd* fd,u_int *fileid) {
	if (fd->fd_disktype == NORMAL_DISK) {
		return  getInfoFromFileFd(fd,fileid);
	} else if (fd->fd_disktype == FAT32_DISK) {
		return  getInfoFromFatFileFd(fd,fileid);
	} else {
		user_panic("user panic disk type not implentment!!\n");
		return -1;
	}
}


int open(const char *path, int mode) {
	char *va;
	u_int size, fileid;
	struct Fd *fd;
	try(fd_alloc(&fd));
	try(fsipc_open(path,mode,fd));
	va = fd2data(fd);	
	size = getInfoFromFd(fd,&fileid);
	for (int i = 0; i < size; i += BY2PG) {
		try(fsipc_map(fileid,i,va + i));
	}
	return fd2num(fd);
}



// Overview:
//  Close a file descriptor
int file_close(struct Fd *fd) {
	int r;
	void *va;
	u_int size, fileid;
	u_int i;
	size = getInfoFromFd(fd,&fileid);	
	va = fd2data(fd);
	for (i = 0; i < size; i += BY2PG) {
		fsipc_dirty(fileid, i);
	}
	if ((r = fsipc_close(fileid)) < 0) {
		debugf("cannot close the file\n");
		return r;
	}
	if (size == 0) {
		return 0;
	}
	for (i = 0; i < size; i += BY2PG) {
		if ((r = syscall_mem_unmap(0, (void *)(va + i))) < 0) {
			debugf("cannont unmap the file.\n");
			return r;
		}
	}
	return 0;
}

// Overview:
//  Read 'n' bytes from 'fd' at the current seek position into 'buf'. Since files
//  are memory-mapped, this amounts to a memcpy() surrounded by a little red
//  tape to handle the file size and seek pointer.
static int file_read(struct Fd *fd, void *buf, u_int n, u_int offset) {
	u_int size =getInfoFromFd(fd,0);
	if (offset > size) {
		return 0;
	}
	if (offset + n > size) {
		n = size - offset;
	}
	memcpy(buf, (char *)fd2data(fd) + offset, n);
	return n;
}

// Overview:
//  Find the virtual address of the page that maps the file block
//  starting at 'offset'.
int read_map(int fdnum, u_int offset, void **blk) {
	int r;
	void *va;
	struct Fd *fd;

	if ((r = fd_lookup(fdnum, &fd)) < 0) {
		return r;
	}

	if (fd->fd_dev_id != devfile.dev_id) {
		return -E_INVAL;
	}

	va = fd2data(fd) + offset;

	if (offset >= MAXFILESIZE) {
		return -E_NO_DISK;
	}

	if (!(vpd[PDX(va)] & PTE_V) || !(vpt[VPN(va)] & PTE_V)) {
		return -E_NO_DISK;
	}

	*blk = (void *)va;
	return 0;
}

// Overview:
//  Write 'n' bytes from 'buf' to 'fd' at the current seek position.
static int file_write(struct Fd *fd, const void *buf, u_int n, u_int offset) {
	int r;
	u_int tot,size;
	size = getInfoFromFd(fd,0);
	tot = offset + n;
	if (tot > MAXFILESIZE) {
		return -E_NO_DISK;
	}

	if (tot > size) {
		if ((r = ftruncate(fd2num(fd), tot)) < 0) {
			return r;
		}
	}
	// Write the data
	
	memcpy((char *)fd2data(fd) + offset, buf, n);
	return n;
}

static void getStatFromFd(struct Fd* fd,struct Stat*st) {
	if (fd->fd_disktype == NORMAL_DISK) {
		struct FatFileFd *f = (struct FatFileFd*)fd;
		strcpy(st->st_name,(char*)f->f_file.DIR_Name);
		st->st_size = f->f_file.DIR_FileSize;
		st->st_isdir = f->f_file.DIR_Attr == ATTR_DIRECTORY;
	} else if (fd->fd_disktype == FAT32_DISK) {
		struct Filefd *f = (struct Filefd *)fd;
		strcpy(st->st_name, f->f_file.f_name);
		st->st_size = f->f_file.f_size;
		st->st_isdir = f->f_file.f_type == FTYPE_DIR;		
	} else {
		user_panic("not implentmented disk type!\n");
	} 
}

static int file_stat(struct Fd *fd, struct Stat *st) {
	getStatFromFd(fd,st);
	return 0;
}

static void setInfoToFd(struct Fd* fd, u_int*size,u_int*fileid) {
	if (fd->fd_disktype == NORMAL_DISK) {
		struct Filefd* f = (struct Filefd*)fd;
		if (size) {
			f->f_file.f_size = *size;
		}
		if (fileid) {
			f->f_fileid = *fileid;
		}
	} else if (fd->fd_disktype == FAT32_DISK) {
		struct FatFileFd* f = (struct FatFileFd*)fd;
		if (size) {
			f->f_file.DIR_FileSize = *size;
		}
		if (fileid) {
			f->f_fileid = *fileid;
		}
	} else {
		user_panic("not implentmented disk!\n");
	}
}

// Overview:
//  Truncate or extend an open file to 'size' bytes
int ftruncate(int fdnum, u_int size) {
	int i, r;
	struct Fd *fd;
	u_int oldsize, fileid;
	if (size > MAXFILESIZE) {
		return -E_NO_DISK;
	}

	if ((r = fd_lookup(fdnum, &fd)) < 0) {
		return r;
	}

	if (fd->fd_dev_id != devfile.dev_id) {
		return -E_INVAL;
	}
	oldsize = getInfoFromFd(fd,&fileid);
	setInfoToFd(fd,&size,0);
	if ((r = fsipc_set_size(fileid, size)) < 0) {
		return r;
	}

	void *va = fd2data(fd);
	for (i = ROUND(oldsize, BY2PG); i < ROUND(size, BY2PG); i += BY2PG) {
		if ((r = fsipc_map(fileid, i, va + i)) < 0) {
			fsipc_set_size(fileid, oldsize);
			return r;
		}
	}

	// Unmap pages if truncating the file
	for (i = ROUND(size, BY2PG); i < ROUND(oldsize, BY2PG); i += BY2PG) {
		if ((r = syscall_mem_unmap(0, (void *)(va + i))) < 0) {
			user_panic("ftruncate: syscall_mem_unmap %08x: %e", va + i, r);
		}
	}


	return 0;
}

int remove(const char *path) {
	return fsipc_remove(path);
}

int sync(void) {
	return fsipc_sync();
}
