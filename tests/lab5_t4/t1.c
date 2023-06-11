#include <lib.h>

int main() {
	char buf[512] = "I Love buaa and os!";
	char buf1[512];
	int fd;
/*	fd = open("/root2/file3",O_RDWR | O_CREAT);
	if (write(fd,buf,512) < 0) {
		user_panic("ccccccccccc\n");
	}
	close(fd);*/
	fd = open("/root2/file3",O_RDONLY);
	read(fd,buf1,512);
	debugf(";%s;\n",buf1);
	close(fd);
	remove("/root2/file3");
	user_assert(open("/root2/file3",O_RDWR) < 0);
	return 0;
}
