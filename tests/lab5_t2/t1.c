#include <lib.h>

int main() {
	int r;
	char buf[512];
	char buf1[512] = "I love buaa and os very much !";
	int fd2 = open("/root2/file1",O_RDWR);
	user_assert(fd2 >= 0);

	read(fd2,buf,512);
	debugf("buf is \n ;\n%s;\n",buf);
	close(fd2);
	memset(buf,0,512);
	fd2 = open("/root2/file1",O_RDWR);
	if (write(fd2,buf1,512) < 0) {
		user_panic("ccccccccccc");
	}
	buf[0] = '1';
	if (read(fd2,buf,512) < 0) {
		user_panic("ccccccccccc");
	}
	user_assert(buf[0] == 0);
	close(fd2);
	fd2 = open("/root2/file2",O_CREAT | O_RDWR);
	if (fd2 >= 0) {
		if (write(fd2,buf1,512) < 0) {
			user_panic("cant write to the new file!\n");
		}
		close(fd2);
	}
	fd2 = open("/root2/file2",O_RDWR);
	buf[0] = '1';
	if (read(fd2,buf,512) < 0) {
		user_panic("cant read from the new file!\n");
	}
	debugf("new file context is ;%s;\n",buf);
	close(fd2);
	return 0;
}
