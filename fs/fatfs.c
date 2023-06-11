#include "serv.h"
#include <mmu.h>

static struct Fat32Bpb fat32bpb;
static struct FSInfo fsinfo;
struct Fat32_FS fat32fs;

int fatva_is_dirty(void* va);
int fatva_is_mapped(void* va); 
void* cluster_is_mapped(u_int clusno);
char* skip_slash(char*p);

int cluster2Sec(int clusNo) {
	return (clusNo - 2) * fat32fs.secPerClus + fat32fs.dataStartSector; 
}

int addr2cluster(void* addr) {
	return (uint32_t)(addr - DISKMAP - PDMAP) / fat32fs.bytsPerCluster + 2;
}

void* cluster2addr(int clusno) {
	uint32_t baseaddr = DISKMAP + PDMAP;
	return (void*)((clusno - 2) * fat32fs.bytsPerCluster + baseaddr);
}

int fatva_is_dirty(void* va) {
	return vpt[VPN(va)] & PTE_DIRTY;
}

int cluster_is_dirty(u_int clusno) {

	void* va = cluster2addr(clusno);
	if (fat32fs.bytsPerCluster <= BY2PG) {
		return fatva_is_mapped(va) && fatva_is_dirty(va);	
	} else {
		for (int i = 0; i < fat32fs.bytsPerCluster / BY2PG; ++i) {
			if (fatva_is_mapped(va) && fatva_is_dirty(va)) {
				return 1;
			}
			va += BY2PG;
		}
	}
	return 0;
}

int dirty_cluster(u_int clusno) {
	int r;
	void*va = cluster2addr(clusno);
	if (fat32fs.bytsPerCluster <= BY2PG) {
		if (!fatva_is_mapped(va)) {
			return -E_NOT_FOUND;
		}
		if (fatva_is_dirty(va)) {
			return 0;
		}
		return syscall_mem_map(0,va,0,va,PTE_D | PTE_DIRTY);
	} else {
		for (int i = 0; i < fat32fs.bytsPerCluster / BY2PG; ++i) {
			if (!fatva_is_mapped(va)) {
				return -E_NOT_FOUND;
			}	
			if (fatva_is_dirty(va)) {
				va += BY2PG;
				continue;
			}
			if ((r = syscall_mem_map(0,va,0,va,PTE_D | PTE_DIRTY)) != 0) {
				return r;
			}
			va += BY2PG;		
		}
	}
	return 0;
}

void write_cluster(u_int clusno) {
	if (!cluster_is_mapped(clusno)) {
		user_panic("write unmapped cluster %08x\n",clusno);
	}
	void *va = cluster2addr(clusno);
	//debugf("sec is %d,curno is %d,va is %s\n",cluster2Sec(clusno),clusno,(char*)va);
	ide_write(1,cluster2Sec(clusno),va,fat32fs.secPerClus);
}

int fatva_is_mapped(void* va) {
	return (vpd[PDX(va)] & PTE_V) && (vpt[VPN(va)] & PTE_V);
}
void* cluster_is_mapped(u_int clusno) {
	void* va = cluster2addr(clusno);
	if (fat32fs.bytsPerCluster <= BY2PG) {
		if (fatva_is_mapped(va)) {
			return va;
		} else {
			return NULL;
		} 	
	} else {
		for (int i = 0; i < fat32fs.bytsPerCluster / BY2PG; ++i) {
			if (!fatva_is_mapped(va)) {
				return NULL;
			}
			va += BY2PG;
		}
		return cluster2addr(clusno);
	}
	return NULL;
}

int map_cluster(u_int clusno) {
	int r;
	void* addr = cluster2addr(clusno);
	if (fat32fs.bytsPerCluster > BY2PG) {
		for (int i = 0; i < fat32fs.bytsPerCluster / BY2PG; ++i) {
			if (fatva_is_mapped(addr)) {
				addr += BY2PG;
				continue;
			}
			if ((r = syscall_mem_alloc(0,addr,PTE_D)) < 0) {
				for (int j = 0; j < i; ++j) {
					syscall_mem_unmap(0,addr);
					addr -= BY2PG;
				}
				return r;
			}	
			addr += BY2PG;
		}
		addr -= fat32fs.bytsPerCluster;
	} else {
		if (fatva_is_mapped(addr)) {
			return 0;
		}
		if ((r = syscall_mem_alloc(0,addr,PTE_D)) < 0) {
			syscall_mem_unmap(0,addr);
			return r;
		}
	}

	ide_read(1,cluster2Sec(clusno),addr,fat32fs.bytsPerCluster);
	return 0;

}

int cluster_is_free(u_int clusno) {
	u_int * fats = (u_int*)DISKMAP;
	return fats[clusno] == FAT32_FREE;
}

void free_cluster(u_int clusno) {
	u_int *fats = (u_int*)DISKMAP;
	fats[clusno] = FAT32_FREE;

}

int alloc_cluster_num(void) {
	u_int* fats = (u_int*)DISKMAP;
	for (int clusno = 2; clusno < fat32bpb.BPB_TotSec32; ++clusno) {
		if (fats[clusno] == 0x0) {
			if (fat32fs.bytsPerCluster >= BY2PG) {
				fats[clusno] = FAT32_USED; 
			} else {
				for (int i = 0; i < BY2PG / fat32fs.bytsPerCluster; ++i) {
					if (i < BY2PG / fat32fs.bytsPerCluster - 1) {
						fats[clusno + i] = clusno + i + 1;
					} else {
						fats[clusno + i] = FAT32_USED;
					}
				}	

			}
			return clusno;
		}
	}

	return -E_NO_DISK;
}

int alloc_cluster(void) {
	int r,clusno;
	if ((r = alloc_cluster_num()) < 0) {
		return r;
	}
	clusno = r;
	if ((r = map_cluster(clusno)) < 0) {
		free_cluster(clusno);
		return r;
	}
	return clusno;
}

void unmap_cluster(u_int clusno) {
	void* va;
	va = cluster_is_mapped(clusno);
	if (!cluster_is_free(clusno) && cluster_is_dirty(clusno)) {
		write_cluster(clusno);
	}
	if (fat32fs.bytsPerCluster > BY2PG) {
		for (int i = 0; i < fat32fs.bytsPerCluster / BY2PG; ++i) {
			syscall_mem_unmap(syscall_getenvid(),va);
			va += BY2PG;
		}
	} else {
		syscall_mem_unmap(syscall_getenvid(),va);
	}
	user_assert(!cluster_is_mapped(clusno));
}

int read_cluster(u_int clusno,void**blk,u_int*isnew) {
	if (clusno >= fat32bpb.BPB_TotSec32 / fat32fs.secPerClus || clusno < 2) {
		user_panic("reading non-existent cluster! %08x\n",clusno);
	}
	void * addr = cluster2addr(clusno);
	if (cluster_is_mapped(clusno)) {
		if (isnew) {
			*isnew = 0;
		}
	} else {
		if (isnew) {
			*isnew = 1;
		}
		syscall_mem_alloc(0,addr,PTE_D);
		ide_read(1,cluster2Sec(clusno),addr,fat32fs.secPerClus);
		//debugf("reading clusno is %d,sec is %d,va is %s\n",clusno,cluster2Sec(clusno),(char*)addr);
	}
	if (blk) {
		*blk = addr;
	}
	return 0;
}
/*
   static void printDir(struct Fat32_Dir* dir) {
   debugf("DIR_NAME: %s\n",(char*)dir->DIR_Name);
   debugf("DIR_ATTR: %08x\n",dir->DIR_Attr);
   debugf("DIR_NTRes: %08x\n",dir->DIR_NTRes);
   debugf("DIR_CrtTimeTenth : %08x\n",dir->DIR_CrtTimeTenth);
   debugf("DIR_CrtTime: %08x\n",dir->DIR_CrtTime);
   debugf("DIR_CrtDate: %08x\n",dir->DIR_CrtDate);
   debugf("DIR_LstAccDate: %08x\n",dir->DIR_LstAccDate);
   debugf("DIR_FstClusHI: %08x\n",dir->DIR_FstClusHI);
   debugf("DIR_WrtTime: %08x\n",dir->DIR_WrtTime);
   debugf("DIR_WrtDate: %08x\n",dir->DIR_WrtDate);
   debugf("DIR_FstClusLO: %08x\n",dir->DIR_FstClusLO);
   debugf("DIR_FileSize: %08x\n",dir->DIR_FileSize);
   }

   static void printBpb(struct Fat32Bpb* bpb) {
   debugf("jmpBoot is %02x%02x%02x\n",bpb->BS_jmpBoot[0],bpb->BS_jmpBoot[1],bpb->BS_jmpBoot[2]);
   debugf("OEMName is '%s'\n",bpb->BS_OEMName);
   debugf("BytePerSec is %08x\n",bpb->BPB_BytsPerSec);
   debugf("SecPerClus is %08x\n",bpb->BPB_SecPerClus);
   debugf("RscdSecClus : %08x\n",bpb->BPB_RsvdSecCnt);
   debugf("NumFATs is %08x\n",bpb->BPB_NumFATs);
   debugf("BPB_RootEntCnt is %08x\n",bpb->BPB_RootEntCnt);
   debugf("BPB_Media is %08x\n",bpb->BPB_Media);
   debugf("BPB_SecPerTrk is %08x\n",bpb->BPB_SecPerTrk);
   debugf("numHeads is %08x\n",bpb->BPB_NumHeads);
   debugf("totSec32 IS %08x\n",bpb->BPB_TotSec32);
   debugf("fatsz32 is %08x\n",bpb->BPB_FATSz32);
   debugf("rootCluster is %08x\n",bpb->BPB_RootClus);

   }

   static void printFSInfo(struct FSInfo * fsinfo) {
   debugf("LeadSig is %08x\n",fsinfo->FSI_LeadSig);
   debugf("StrucSig is %08x\n",fsinfo->FSI_StrucSig);
   debugf("Free_Count is %08x\n",fsinfo->FSI_Free_Count);
   debugf("Nxt_Free is %08x\n",fsinfo->FSI_Nxt_Free);
   debugf("TrailSig is %08x\n",fsinfo->FSI_TrailSig);
   }
   */

void addTestFile() {
	char buf[512] = "1111111111111111111111111111";
	struct Fat32_Dir* dirs = fat32fs.root + 1;
	strcpy((char*)dirs->DIR_Name,"file1");
	u_int clusno = alloc_cluster();
	dirs->DIR_FstClusHI = (uint16_t)(clusno >> 16);
	dirs->DIR_FstClusLO = (uint16_t)clusno;
	dirs->DIR_FileSize = BY2PG;
	char* addr = cluster2addr(clusno);
	strcpy(addr,buf);	
}
void reInitFat() {
	u_int* fats = (u_int*)DISKMAP;
	fats[0] = fats[1] = FAT32_USED;
//	debugf("cluster is %08x\n",fat32bpb.BPB_TotSec32);
	for (int i = 2; i < fat32bpb.BPB_TotSec32; ++i) {
		fats[i] = FAT32_FREE;
	}
}

void reInitRootDir() {
	u_int* fats = (u_int*)DISKMAP;
	reInitFat();
	struct Fat32_Dir* dir = (struct Fat32_Dir*)(DISKMAP + PDMAP);
	strcpy((char*)dir->DIR_Name,"/root2");
	dir->DIR_Attr = ATTR_DIRECTORY;
	dir->DIR_FileSize = 0;
	fats[2] = 0;
	u_int clusno = alloc_cluster();
	dir->DIR_FstClusHI = (uint16_t)(clusno >> 16);
	dir->DIR_FstClusLO = (uint16_t)(clusno & ((1 <<16) - 1));
	dir->DIR_FileSize = BY2PG;
	addTestFile();
	//dirty_cluster(2);
	ide_write(1,fat32fs.dataStartSector,dir,1);
}


void initRootDir() {
	void* addr = cluster2addr(2);
	map_cluster(2);
	ide_read(1,fat32fs.dataStartSector,addr,1);	
	struct Fat32_Dir * rootdir = (struct Fat32_Dir*)addr;
	fat32fs.root = rootdir;
	if (rootdir->DIR_Name[0] == 0) {
		reInitRootDir();
	}
}

void initFs() {
	fat32fs.bytsPerSector = fat32bpb.BPB_BytsPerSec;	
	fat32fs.dataStartSector = fat32bpb.BPB_RsvdSecCnt + fat32bpb.BPB_NumFATs * fat32bpb.BPB_FATSz32;
	fat32fs.numFats = fat32bpb.BPB_NumFATs;
	fat32fs.rootCluster = fat32bpb.BPB_RootClus;
	fat32fs.rsvdSecs = fat32bpb.BPB_RsvdSecCnt;
	fat32fs.secPerClus = fat32bpb.BPB_SecPerClus;
	fat32fs.secPerFat = fat32bpb.BPB_FATSz32;
	fat32fs.bytsPerCluster = fat32fs.bytsPerSector * fat32fs.secPerClus;
}

void readAndMapFat() {
	//start from the DISKMAP and end before the second PDMAP
	void *va = (void*)DISKMAP;
	int r,secCnt = 0,blo2sec = (BY2PG / fat32fs.bytsPerSector);
	secCnt = fat32fs.dataStartSector - fat32fs.rsvdSecs;
	for (int i = 0; i < secCnt / blo2sec; ++i) {
		if ((r = syscall_mem_alloc(syscall_getenvid(),va,PTE_D | PTE_LIBRARY)) < 0) {
			user_panic("read fat failed : %d\n",r);		
		}
		ide_read(1,fat32fs.rsvdSecs + i * blo2sec,va,blo2sec);
		va += BY2PG;
	}
}
void writeBackFat() {
	//start from the DISKMAP and end before the second PDMAP
	void *va = (void*)DISKMAP;
	int r,secCnt = 0,blo2sec = (BY2PG / fat32fs.bytsPerSector);
	secCnt = fat32fs.dataStartSector - fat32fs.rsvdSecs;
	for (int i = 0; i < secCnt / blo2sec; ++i) {

		ide_write(1,fat32fs.rsvdSecs + i * blo2sec,va,blo2sec);
		if ((r = syscall_mem_unmap(syscall_getenvid(),va)) < 0) {
			user_panic("read fat failed : %d\n",r);		
		}
		va += BY2PG;
	}
}

void readInfo() {
	ide_read(1,0,&fat32bpb,1);
	ide_read(1,fat32bpb.BPB_FSInfo,&fsinfo,1);
	initFs();
	readAndMapFat();
	initRootDir();
}

char *skip_rootName(char* path) {
	return path + ROOTDIR_NAMELEN;
}

int fatfile_cluster_walk(struct Fat32_Dir* f, u_int fileclusno,uint32_t **pclusno, u_int alloc) {
	u_int* fats = (u_int*)DISKMAP;
	uint32_t curClus = (f->DIR_FstClusHI << 16 ) | f->DIR_FstClusLO;
	for (int i = 0; i < fileclusno; ++i) {
		if (i < fileclusno - 1 && fats[curClus] == FAT32_USED) {
			if (!alloc) {
				return -E_NOT_FOUND;	
			} else {
				if (f->DIR_FstClusLO == 0 && f->DIR_FstClusHI == 0) {
					u_int curno = alloc_cluster();
					f->DIR_FstClusLO = (uint16_t)curno;
					f->DIR_FstClusHI = (uint16_t)(curno >> 16);
				} else {
					fats[curClus] = alloc_cluster();
				}
			}
		}	
		curClus = fats[curClus];
	}
	*pclusno = &curClus;  
	return 0;

}

int fatfile_map_cluster(struct Fat32_Dir* f, u_int fileclusno,u_int* clusno,u_int alloc) {
	int r;
	uint32_t *ptr;
	if ((r = fatfile_cluster_walk(f,fileclusno,&ptr,alloc)) < 0) {
		return r;
	}
	if (*ptr == 0) {
		if (alloc == 0) {
			return -E_NOT_FOUND;
		}
		if ((r = alloc_cluster()) < 0) {
			return r;
		}
		if (f->DIR_FstClusLO == 0 && f->DIR_FstClusHI == 0) {
			f->DIR_FstClusHI = (uint16_t)(r >> 16);
			f->DIR_FstClusLO = (uint16_t)r;	
		} 
		*ptr = r;
	}	
	*clusno = *ptr;
	return 0;
}


int fatread_block(u_int clusno,u_int inbno,void**blk,u_int*isnew) {
	if (clusno >= fat32bpb.BPB_TotSec32 / fat32fs.secPerClus || clusno < 2) {
		user_panic("reading non-existent cluster! %08x\n",clusno);
	}
	if (cluster_is_mapped(clusno)) {
		if (isnew) {
			*isnew = 0;
		}
	} else {
		if (isnew) {
			*isnew = 1;
		}
		map_cluster(clusno);
	}
	void *addr = cluster2addr(clusno);
	*blk = addr + BY2PG * inbno;
	dirty_cluster(clusno);
	return 0;
}


int fatfile_get_block(struct Fat32_Dir* f, u_int filebno,void**blk) {
	int r;
	u_int fileclusno,inbno,clusno,isnew;
	if (fat32fs.bytsPerCluster < BY2PG) {
		fileclusno = filebno * (BY2PG / fat32fs.bytsPerCluster);
		inbno = 0;
	} else {
		fileclusno = filebno / (fat32fs.bytsPerCluster / BY2PG);
		inbno = filebno % (fat32fs.bytsPerSector / BY2PG);
	}
	if ((r = fatfile_map_cluster(f,fileclusno,&clusno,1)) < 0) {
		return r;
	}

	if ((r = fatread_block(clusno,inbno,blk,&isnew)) < 0) {
		return r;
	}
	return 0;
}

int fatfile_get_cluster(struct Fat32_Dir* f, u_int fileclusno, void**blk) {
	int r;
	u_int clusno,isnew;
	if ((r = fatfile_map_cluster(f,fileclusno,&clusno,1)) < 0) {
		return r;
	}
	if ((r = read_cluster(clusno,blk,&isnew)) < 0) {
		return r;
	}
	return 0;
}

int fatdir_lookup(struct Fat32_Dir* dir, char* name, struct Fat32_Dir** file) {
	u_int nclus = dir->DIR_FileSize / fat32fs.bytsPerCluster;
	for (int i = 0; i < nclus; ++i) {
		void* blk;
		try(fatfile_get_cluster(dir,i,&blk));
		struct Fat32_Dir* files = (struct Fat32_Dir*)blk;
		for (struct Fat32_Dir* f = files; f < files + (fat32fs.bytsPerCluster / sizeof(struct Fat32_Dir)); ++f) {
			if (strcmp(name,(char*)f->DIR_Name) == 0) {
				*file = f;
				f->dir = dir;
				dirty_cluster(addr2cluster(blk));
				return 0;
			}
		}
	}
	return -E_NOT_FOUND;
}

int fatdir_alloc_file(struct Fat32_Dir* dir, struct Fat32_Dir** file) {
	int r;
	u_int nclus,i,j;
	void* blk;
	struct Fat32_Dir* f;
	nclus = dir->DIR_FileSize / fat32fs.bytsPerCluster;
	for (i = 0; i < nclus; ++i) {
		if ((r = fatfile_get_cluster(dir,i,&blk)) < 0) {
			return r;		
		}
		f = (struct Fat32_Dir*)blk;
		for (j = 0; j < fat32fs.bytsPerCluster / sizeof(struct Fat32_Dir); ++j) {
			if (f[j].DIR_Name[0] == '\0') {
				*file = &f[j];
				return 0;
			}
		}
	}
	if (fat32fs.bytsPerCluster >= BY2PG) {
		dir->DIR_FileSize += fat32fs.bytsPerSector;
	} else {
		dir->DIR_FileSize += BY2PG;
	}

	if ((r = fatfile_get_cluster(dir,i,&blk)) < 0) {
		return r;
	}
	debugf("sssssssssssssssssssssssssssssss\n");
	f = (struct Fat32_Dir*)blk;
	*file = &f[0];
	return 0;


}


int fatwalk_path(char* path,struct Fat32_Dir** pdir,struct Fat32_Dir** pfile, char* lastelem) {
	int r;
	char *p;
	char name[MAXNAMELEN];
	struct Fat32_Dir* dir,*file;
	path = skip_slash(skip_rootName(path));
	file = fat32fs.root;	
	dir = 0;
	name[0] = 0;
	if (pdir) {
		*pdir = 0;
	}
	while (*path != '\0') {
		dir = file;
		p = path;
		while (*path != '/' && *path != '\0') {
			++path;
		}
		if (path - p >= MAXNAMELEN) {
			return -E_BAD_PATH;
		}
		memcpy(name,p,path - p);
		name[path - p] = '\0';
		path = skip_slash(path);
		if (dir->DIR_Attr != ATTR_DIRECTORY) {
			return -E_NOT_FOUND;
		}
		if ((r = fatdir_lookup(dir,name,&file)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir) {
					*pdir = dir;
				} if (lastelem) {
					strcpy(lastelem,name);
				}
				*pfile = 0;
			}
			return r;
		}
	}
	if (pdir) {
		*pdir = dir;
	}
	*pfile = file;
	return 0;
}

int fatfile_dirty(struct Fat32_Dir* f, u_int offset) {
	int r;
	u_int clusno;
	if ((r = fatfile_map_cluster(f,(offset / fat32fs.bytsPerCluster),&clusno,0)) < 0) {
		return r;
	}
	return dirty_cluster(clusno);

}

int fatfile_open(char*path, struct Fat32_Dir** file) {
	return fatwalk_path(path,0,file,0);
}


int fatfile_create(char* path,struct Fat32_Dir** file) {
	char name[MAXFATNAMELEN];
	int r;
	struct Fat32_Dir* dir,*f;
	if ((r = fatwalk_path(path,&dir,&f,name)) == 0) {
		return -E_FILE_EXISTS;
	}
	if (r != -E_NOT_FOUND || dir == 0) {
		return r;
	}
	if (fatdir_alloc_file(dir,&f) < 0) {
		return r;
	}
	strcpy((char*)f->DIR_Name,name);
	*file = f;
	return 0;

}


void fatfile_truncate(struct Fat32_Dir* f, u_int newsize) {
	u_int* fats = (u_int*)DISKMAP;
	u_int old_nclus,new_nclus,clusno,t;
	old_nclus = f->DIR_FileSize / fat32fs.bytsPerCluster + 2;
	if (f->DIR_FileSize % fat32fs.bytsPerCluster != 0) {
		old_nclus += 1;
	}
	new_nclus = newsize / fat32fs.bytsPerCluster + 2;
	if (newsize % fat32fs.bytsPerCluster != 0) {
		new_nclus += 1;
	}
	if (newsize == 0) {
		new_nclus = 0;
	}
	clusno = ((uint32_t)f->DIR_FstClusHI << 16) | (uint32_t)(f->DIR_FstClusLO);
	if (new_nclus <= old_nclus) {
		for (int i = 0; i < new_nclus; ++i) {
			clusno = fats[clusno];
		}
		t = fats[clusno];
		fats[clusno] = FAT32_USED;
		clusno = t;
		for (new_nclus; new_nclus <= old_nclus; ++new_nclus) {
			t = fats[clusno];
			fats[clusno] = FAT32_FREE;
			clusno = t;			
		}
	}
	f->DIR_FileSize = newsize;
}

void fat_flush(struct Fat32_Dir* f) {
	u_int flushedClus[128],cnt = 0,flag = 0,sec = 0;
	void* fats = (void*)DISKMAP;
	u_int clusno = (uint16_t)f->DIR_FstClusLO | ((uint32_t)f->DIR_FstClusHI << 16);
	if (clusno == FAT32_FREE) {
		return;
	}
	while (1) {
		flag = 0;
		sec = ROUNDDOWN((clusno *  sizeof(u_int)),BY2PG) / BY2PG;
		//	debugf("f is %s,clusno is %08x,sec is %d\n",f->DIR_Name,clusno,sec);
		for (int i = 0; i  < cnt ; ++i) {
			if (flushedClus[i] == sec) {
				flag = 1;
				break;
			}
		}	
		if (flag == 0) {
			ide_write(1,fat32fs.rsvdSecs + sec,fats + sec * fat32fs.bytsPerSector ,1);	
			flushedClus[cnt++] = sec;
		} 
		clusno = ((u_int*)fats)[clusno];
		if (clusno > fat32bpb.BPB_TotSec32 / fat32fs.secPerClus) {
			break;
		}
	}
}

void fatfile_flush(struct Fat32_Dir* f) {
	u_int nclus;
	u_int clusno;
	u_int fileclusno;
	int r;
	nclus = f->DIR_FileSize / fat32fs.bytsPerCluster;
	if (f->DIR_FileSize % fat32fs.bytsPerCluster != 0) {
		nclus += 1;
	}
	for (fileclusno = 0; fileclusno < nclus; ++fileclusno) {
		if ((r = fatfile_map_cluster(f,fileclusno,&clusno,0)) < 0) {
			continue;
		}
		if (cluster_is_dirty(clusno)) {
			write_cluster(clusno);
		}
	}
	fat_flush(f);
}


int fatfile_set_size(struct Fat32_Dir*f, u_int newsize) {
	u_int size = fat32fs.bytsPerCluster < BY2PG ? ROUND(newsize,BY2PG): ROUND(newsize,fat32fs.bytsPerCluster);
	if (f->DIR_FileSize > size) {
		fatfile_truncate(f,size);
	}
	f->DIR_FileSize = size;
	if (f->dir) {
		fatfile_flush(f->dir);
	}
	return 0;
}


void fatfs_sync(void) {
	int i;
	for (i = 2; i < fat32bpb.BPB_TotSec32 / fat32fs.secPerClus; ++i) {
		if (cluster_is_dirty(i)) {
			write_cluster(i);

		}

	}
}

void fatfile_close(struct Fat32_Dir* f) {
	fatfile_flush(f);
	if (f->dir) {
		fatfile_flush(f->dir);
	} 
}

void syncFat() {
	u_int* fats = (u_int*)DISKMAP;
	ide_write(1,fat32fs.rsvdSecs, fats,fat32bpb.BPB_FATSz32);
}

void deleteUpdateFat(struct Fat32_Dir* f) {
	u_int * fats = (u_int*)DISKMAP;
	u_int t,cluno = ((uint32_t)f->DIR_FstClusHI << 16) | ((uint32_t)f->DIR_FstClusLO);
	while (1) {
		t =fats[cluno];
		fats[cluno] = FAT32_FREE;
		cluno = t;
		if (cluno == FAT32_USED) {
			break;
		}
	}
	syncFat();
	f->DIR_FstClusHI = 0;
	f->DIR_FstClusLO = 0;
}

int fatfile_remove(char* path) {
	int r;
	struct Fat32_Dir* f;
	if ((r = fatwalk_path(path,0,&f,0)) < 0) {
		return r;
	}
	fatfile_truncate(f,0);
	f->DIR_Name[0] = '\0';
	deleteUpdateFat(f);
//	fatfile_flush(f);
	if (f->dir) {
		fatfile_flush(f->dir);
	}
	return 0;
}


void fatfs_init() {
	readInfo();
	//testWrite();
}
