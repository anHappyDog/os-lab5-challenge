#include "serv.h"
#include <fd.h>
#include <fsreq.h>
#include <lib.h>
#include <mmu.h>


struct FatOpen {
	struct Fat32_Dir *o_file; // mapped descriptor for open file
	u_int o_fileid;	     // file id
	int o_mode;	     // open mode
	struct FatFileFd *o_ff; // va of filefd page
};
struct FatOpen fatopentab[MAXOPEN] = {{0,0,1}};


void fatserve_init(void) {
	int i;
	u_int va = FILEVA;
	for (i = 0; i < MAXOPEN; ++i) {
		fatopentab[i].o_fileid = i + 1024;
		fatopentab[i].o_ff = (struct FatFileFd*)va;
		va += BY2PG;
	}
}

int fatopen_alloc(struct FatOpen **o) {
	int i,r;
	for (i = 0; i < MAXOPEN; ++i) {
		switch (pageref(fatopentab[i].o_ff)) {
			case 0:
				if ((r = syscall_mem_alloc(0,fatopentab[i].o_ff,PTE_D | PTE_LIBRARY)) < 0) {
					return r;
				}
			case 1:
				fatopentab[i].o_fileid += MAXOPEN_NUM;
				*o = &fatopentab[i];
				memset((void*)fatopentab[i].o_ff,0,BY2PG);
				return (*o)->o_fileid;
		}
	}
	return -E_MAX_OPEN;
}

int fatopen_lookup(u_int envid, u_int fileid, struct FatOpen** po) {
	struct FatOpen *o;
	o = &fatopentab[fileid % MAXOPEN_NUM];
	if (pageref(o->o_ff) == 1 || o->o_fileid != fileid) {
		return -E_INVAL;
	}
	*po = o;
	return 0;

}

void fatserve_open(u_int envid, struct Fsreq_open *rq) {
	struct Fat32_Dir* f;
	struct FatFileFd* ff;
	int r;
	struct FatOpen *o;
	if ((r = fatopen_alloc(&o)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	if ((rq->req_omode & O_CREAT) == O_CREAT) {
		if ((r = fatfile_create(rq->req_path,&f)) < 0) {
			ipc_send(envid,r,0,0);
			return;
		}	
		//f->DIR_Attr = ATTR_DIRECTORY;
	} else if ((rq->req_omode & O_MKDIR) == O_MKDIR) {
			if ((r = fatfile_create(rq->req_path,&f)) < 0) {
			ipc_send(envid,r,0,0);
			return;
		}
		f->DIR_Attr = ATTR_DIRECTORY;
	} else {
		if ((r = fatfile_open(rq->req_path,&f)) < 0) {
			ipc_send(envid,r,0,0);
			return;
		}
	}

	o->o_mode = rq->req_omode;
	o->o_file = f;
	ff = (struct FatFileFd*)o->o_ff;
	ff->f_file = *f;
	ff->f_fileid = o->o_fileid;
	ff->f_fd.fd_omode = o->o_mode;
	ff->f_fd.fd_dev_id = devfile.dev_id;
	ff->f_fd.fd_disktype = FAT32_DISK;
	ipc_send(envid,0,o->o_ff,PTE_D | PTE_LIBRARY);
}	

void fatserve_map(u_int envid, struct Fsreq_map *rq) {
	struct FatOpen* pOpen;
	u_int filebno;
	void* blk;
	int r;
	if ((r = fatopen_lookup(envid,rq->req_fileid,&pOpen)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	filebno = rq->req_offset / BY2BLK;
	if ((r = fatfile_get_block(pOpen->o_file,filebno,&blk)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	//debugf("reading is %s\n",(char*)blk);	
	ipc_send(envid,0,blk,PTE_D | PTE_LIBRARY);
}

void fatserve_set_size(u_int envid, struct Fsreq_set_size *rq) {
	struct FatOpen* pOpen;
	int r;
	if ((r = fatopen_lookup(envid,rq->req_fileid,&pOpen)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	if ((r = fatfile_set_size(pOpen->o_file,rq->req_size)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	ipc_send(envid,0,0,0);
}

void fatserve_close(u_int envid, struct Fsreq_close *rq) {
	struct FatOpen *pOpen;
	int r;
	if ((r = fatopen_lookup(envid,rq->req_fileid,&pOpen)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	fatfile_close(pOpen->o_file);
	ipc_send(envid,0,0,0);
}

void fatserve_remove(u_int envid, struct Fsreq_remove* rq) {
	int r;
	r = fatfile_remove(rq->req_path);
	ipc_send(envid,r,0,0);
}

void fatserve_dirty(u_int envid, struct Fsreq_dirty *rq) {
	struct FatOpen *pOpen;
	int r;
	if ((r = fatopen_lookup(envid,rq->req_fileid,&pOpen)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	if ((r = fatfile_dirty(pOpen->o_file,rq->req_offset)) < 0) {
		ipc_send(envid,r,0,0);
		return;
	}
	ipc_send(envid,0,0,0);
}

void fatserve_sync(u_int envid) {
	fatfs_sync();
	ipc_send(envid,0,0,0);
}



void fatserve(void) {
	u_int req,whom,perm;
	for (;;) {
		perm = 0;
		req = ipc_recv(&whom,(void*)REQVA,&perm);
		if (!(perm & PTE_V)) {
			debugf("Invalid request from %08x: no argument page\n", whom);
			continue;
		}
		switch (req) {
			case FSREQ_OPEN:
				fatserve_open(whom,(struct Fsreq_open*)REQVA);
				break;
			case FSREQ_MAP:
				fatserve_map(whom,(struct Fsreq_map*)REQVA);
				break;
			case FSREQ_SET_SIZE:
				fatserve_set_size(whom,(struct Fsreq_set_size*)REQVA);
				break;
			case FSREQ_CLOSE:
				fatserve_close(whom,(struct Fsreq_close*)REQVA);
				break;
			case FSREQ_DIRTY:
				fatserve_dirty(whom,(struct Fsreq_dirty*)REQVA);
				break;
			case FSREQ_REMOVE:
				fatserve_remove(whom,(struct Fsreq_remove*)REQVA);
				break;
			case FSREQ_SYNC:
				fatserve_sync(whom);
				break;
			default:	
				debugf("Invalid request code %d from %08x\n", whom, req);
				break;
		}
		syscall_mem_unmap(0,(void*)REQVA);
	}
}


int main() {
	//debugf("fat32_dir size is %08x\n",sizeof(struct Fat32_Dir));
	user_assert(sizeof(struct Fat32_Dir) == FATDIRBY2FILE);
	//debugf("FAT FS is running\n");
	//init();	
	
	fatserve_init();
	fatfs_init();
	fatserve();
	return 0;
}
