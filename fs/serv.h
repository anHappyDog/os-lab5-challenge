#include <fs.h>
#include <lib.h>
#include <mmu.h>

#define PTE_DIRTY 0x0002 // file system block cache is dirty

/* IDE disk number to look on for our file system */
#define DISKNO 1

#define BY2SECT 512		    /* Bytes per disk sector */
#define SECT2BLK (BY2BLK / BY2SECT) /* sectors to a block */

/* Disk block n, when in memory, is mapped into the file system
 * server's address space at DISKMAP+(n*BY2BLK). */
#define DISKMAP 0x10000000

/* Maximum disk size we can handle (1GB) */
#define DISKMAX 0x40000000
// Max number of open files in the file system at once
#define MAXOPEN 1024
#define REQVA 0x0ffff000
#define FILEVA 0x60000000

#define FAT32_USED 0xffffffff
#define FAT32_FREE 0x0

#define ROOTDIR_NAMELEN 6

/* ide.c */
void ide_read(u_int diskno, u_int secno, void *dst, u_int nsecs);
void ide_write(u_int diskno, u_int secno, void *src, u_int nsecs);

/* fs.c */
int file_open(char *path, struct File **pfile);
int file_get_block(struct File *f, u_int blockno, void **pblk);
int file_set_size(struct File *f, u_int newsize);
void file_close(struct File *f);
int file_remove(char *path);
int file_dirty(struct File *f, u_int offset);
void file_flush(struct File *);
int file_create(char *path, struct File **file);


void fs_init(void);
void fs_sync(void);
extern uint32_t *bitmap;
int map_block(u_int);
int alloc_block(void);

/* fatfs.c */
int fatfile_open(char* path, struct Fat32_Dir**pfile);
int fatfile_get_cluster(struct Fat32_Dir*f, u_int clusno,void**pblk);
int fatfile_set_size(struct Fat32_Dir* f, u_int newsize);
void fatfile_close(struct Fat32_Dir*f);
int fatfile_remove(char* path);
int fatfile_dirty(struct Fat32_Dir* f, u_int offset);
void fatfile_flush(struct Fat32_Dir*f);
int fatfile_create(char* path,struct Fat32_Dir** file);

int fatfile_get_block(struct Fat32_Dir* f, u_int filebno,void**blk);
void fatfs_init(void);
void fatfs_sync(void);
int map_cluster(u_int);
int alloc_cluster(void);

