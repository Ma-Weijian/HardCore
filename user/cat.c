#include <ulib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stat.h>
#include <file.h>
#include <dir.h>
#include <unistd.h>

#define printf(...)                     fprintf(1, __VA_ARGS__)
#define putc(c)                         cprintf("%c", c)
#define BUFSIZE                         4096

static char buf[BUFSIZE];

char *readline(const char *prompt)
{
	static char buffer[BUFSIZE];
	if (prompt != NULL) {
		printf("%s", prompt);
	}
	int ret, i = 0;
	while (1) {
		char c;
		if ((ret = read(0, &c, sizeof(char))) < 0) {
			return NULL;
		} else if (ret == 0) {
			if (i > 0) {
				buffer[i] = '\0';
				break;
			}
			return NULL;
		}

		if (c == 3) {
			return NULL;
		} else if (c >= ' ' && i < BUFSIZE - 1) {
			putc(c);
			buffer[i++] = c;
		} else if (c == '\b' && i > 0) {
			putc(c);
			i--;
		} else if (c == '\n' || c == '\r') {
			// putc(c);
			cprintf("\n");
			buffer[i] = '\0';
			break;
		}
		// buffer[i+1] = '\0';
		// printf("%s",buffer);
	}
	return buffer;
}

int main(int argc, char *argv[])
{

    if (argc == 1) {
        // printf("This Function is not Implement!\n");
		char *buffer;
        while ((buffer = readline("")) != NULL) {
			printf("%s\n",buffer);
        }
        return 0;
    }
    else {
        for (int i = 1; i < argc; i++) {
            struct stat stat;
            int fd;
            if ((fd = open(argv[i], O_RDONLY)) < 0) {
                printf("Can't find file named \"%s\"\n", argv[i]);
                return -1;
            }
            fstat(fd, &stat);
            int resid = stat.st_size;
            while (resid > 0) {
                int len = stat.st_size < BUFSIZE ? stat.st_size : BUFSIZE;
                len = read(fd, buf, len);
                // buf[len] = '\0';
                // printf(buf);
                write(1, buf, len);
                resid -= len;
            }
            close(fd);
        }
    }
}