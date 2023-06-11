#include <lib.h>

int main() {
	char buf[512] = "I Love buaa and os!";
	char buf1[512];
	/*int fd = open("/root2/file3",O_RDWR | O_CREAT);
	if (write(fd,buf,512) < 0) {
		user_panic("ccccccccccc\n");
	}
	close(fd);*/
	int fd = open("/root2/dir1",O_MKDIR | O_RDWR);
	if (fd < 0) {
		user_panic("sadssssssssss\n");
	}
	close(fd);
	fd = open("/root2/dir1/file1",O_CREAT | O_RDWR);
	write(fd,buf,512);
	close(fd);
	fd = open("/root2/dir1/file1",O_RDWR);
	read(fd,buf1,512);
	debugf(";%s;\n",buf1);
	close(fd);
	return 0;
}
