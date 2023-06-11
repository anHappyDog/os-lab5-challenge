#include <lib.h>

int main() {

	debugf("hello,fat!\n");
	int r;
	char buf[512];
	int fd1 = open("/root1/motd",O_RDONLY);
	if ((r = read(fd1,buf,100)) < 0) {
		user_panic("read failed!,%d\n",r);
	}
	debugf(";%s;\n",buf);
	close(fd1);
	fd1 = open("/root1/file1",O_RDWR | O_CREAT);
	if ((r = write(fd1,buf,100)) < 0) {
		user_panic("write failed!,%d\n",r);		
	}

	close(fd1);
	
	fd1 = open("/root1/file1",O_RDONLY);
	
//	debugf("fd1is %d\n",fd1);
	buf[0] = 0;
	if ((r = read(fd1,buf,100)) < 0) {
		user_panic("cccccccccc\n");
	}
	debugf(";;;;;\n%s\n;;;;;;;;;\n");
	close(fd1);
	remove("/root1/file1");
	fd1 = open("/root1/file1",O_RDONLY);
	user_assert(fd1 < 0 );
	fd1 = open("/root1/dir1",O_MKDIR | O_RDWR);
	close(fd1);
	fd1 = open("/root1/dir1/file1",O_CREAT | O_RDWR);
	write(fd1,buf,100);
	close(fd1);
	return 0;
}
