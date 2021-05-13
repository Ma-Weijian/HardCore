#include <ulib.h>
#include <stdio.h>
#include <string.h>
#include <dir.h>
#include <file.h>
#include <error.h>
#include <unistd.h>

#define printf(...)                     fprintf(1, __VA_ARGS__)
#define putc(c)                         printf("%c", c)

#define BUFSIZE                         4096
#define WHITESPACE                      " \t\r\n"
#define SYMBOLS                         "<|>&;"

char shcwd[BUFSIZE];
char shpwd[BUFSIZE];

int
gettoken(char **p1, char **p2) {
    char *s;
    if ((s = *p1) == NULL) {
        return 0;
    }
    while (strchr(WHITESPACE, *s) != NULL) {
        *s ++ = '\0';
    }
    if (*s == '\0') {
        return 0;
    }

    *p2 = s;
    int token = 'w';
    if (strchr(SYMBOLS, *s) != NULL) {
        token = *s, *s ++ = '\0';
    }
    else {
        bool flag = 0;
        while (*s != '\0' && (flag || strchr(WHITESPACE SYMBOLS, *s) == NULL)) {
            if (*s == '"') {
                *s = ' ', flag = !flag;
            }
            s ++;
        }
    }
    *p1 = (*s != '\0' ? s : NULL);
    return token;
}

char *
readline(const char *prompt) {
    static char buffer[BUFSIZE];
    if (prompt != NULL) {
        printf("%s", prompt);
    }
    int ret, i = 0;
    while (1) {
        char c;
        if ((ret = read(0, &c, sizeof(char))) < 0) {
            return NULL;
        }
        else if (ret == 0) {
            if (i > 0) {
                buffer[i] = '\0';
                break;
            }
            return NULL;
        }

        if (c == 3) {
            return NULL;
        }
        else if (c >= ' ' && i < BUFSIZE - 1) {
            putc(c);
            buffer[i ++] = c;
        }
        else if (c == '\b' && i > 0) {
            putc(c);
            i --;
        }
        else if (c == '\n' || c == '\r') {
            putc(c);
            buffer[i] = '\0';
            break;
        }
    }
    return buffer;
}

void
usage(void) {
    printf("usage: sh [command-file]\n");
}

// 关闭文件描述符 fd2 ，打开文件 filename
int
reopen(int fd2, const char *filename, uint32_t open_flags) {
    int ret, fd1;
    close(fd2);
    if ((ret = open(filename, open_flags)) >= 0 && ret != fd2) {
        close(fd2);
        fd1 = ret, ret = dup2(fd1, fd2);
        close(fd1);
    }
    return ret < 0 ? ret : 0;
}

int
testfile(const char *name) {
    int ret;
    if ((ret = open(name, O_RDONLY)) < 0) {
        return ret;
    }
    close(ret);
    return 0;
}

#include "stat.h"
int
runcmd(char *cmd) {
    static char argv0[BUFSIZE];
    const char *argv[EXEC_MAX_ARG_NUM + 1];
    char *t;
    int argc, token, ret, p[2];
again:
    argc = 0;
    while (1) {
        switch (token = gettoken(&cmd, &t)) {
        case 'w':
            if (argc == EXEC_MAX_ARG_NUM) {
                printf("sh error: too many arguments\n");
                return -1;
            }
            argv[argc ++] = t;
            break;
        case '<':
            if (gettoken(&cmd, &t) != 'w') {
                printf("sh error: syntax error: < not followed by word\n");
                return -1;
            }
            if ((ret = reopen(0, t, O_RDONLY)) != 0) {
                return ret;
            }
            break;
        case '>':
            if (gettoken(&cmd, &t) != 'w') {
                printf("sh error: syntax error: > not followed by word\n");
                return -1;
            }
            if ((ret = reopen(1, t, O_RDWR | O_TRUNC | O_CREAT)) != 0) {
                return ret;
            }
            break;
        case '|':
          //  if ((ret = pipe(p)) != 0) {
          //      return ret;
          //  }
            if ((ret = fork()) == 0) {
                close(0);
                if ((ret = dup2(p[0], 0)) < 0) {
                    return ret;
                }
                close(p[0]), close(p[1]);
                goto again;
            }
            else {
                if (ret < 0) {
                    return ret;
                }
                close(1);
                if ((ret = dup2(p[1], 1)) < 0) {
                    return ret;
                }
                close(p[0]), close(p[1]);
                goto runit;
            }
            break;
        case 0:
            goto runit;
        case ';':
            if ((ret = fork()) == 0) {
                goto runit;
            }
            else {
                if (ret < 0) {
                    return ret;
                }
                waitpid(ret, NULL);
                goto again;
            }
            break;
        default:
            printf("sh error: bad return %d from gettoken\n", token);
            return -1;
        }
    }
runit:
    if (argc == 0) {
        return 0;
    }
    else if (strcmp(argv[0], "cd") == 0) {
        if (argc != 2) {
            return -1;
        }
        strcpy(shcwd, argv[1]);
        return 0;
    }
    if ((ret = testfile(argv[0])) != 0) {
        if (ret != -E_NOENT) {
            return ret;
        }
        snprintf(argv0, sizeof(argv0), "/%s", argv[0]);
        argv[0] = argv0;
    }
    argv[argc] = NULL;
    cprintf("\n");
    return __exec(argv[0], argv);
}

int in_backgroud(char *buffer)
{
    int i = 0;
    while (buffer[i] != '\0')
        i++;
    do
    {
        if (buffer[i] == '&')
        {
            buffer[i] = '\0';
            return 1;
        }
        if (buffer[i] == ' ')
            break;
    } while (i--);
    return 0;
}

int
main(int argc, char **argv) {
    printf("user sh is running!!!\n");
    int ret, interactive = 1;
    if (argc == 2) {
        if ((ret = reopen(0, argv[1], O_RDONLY)) != 0) {
            return ret;
        }
        interactive = 0;
    }
    else if (argc > 2) {
        usage();
        return -1;
    }
    // 以上命令是在sh启动的时候执行
    // 两个参数就是 > sh xxx 重开一个命令行执行
    // 或者直接 xxxx        在当前命令行执行
    assert(shcwd != NULL);
    if ((ret = getcwd(shpwd, sizeof(shpwd))) != 0) {
		return ret;
	}
    static char prepwd [BUFSIZE];
    snprintf(prepwd, sizeof(prepwd), "%s$ ", shpwd);
    char *buffer;
    while ((buffer = readline((interactive) ? prepwd : NULL)) != NULL) {
        int run_in_backgroud = in_backgroud(buffer);
        if(buffer[0]=='\0'){
            cprintf("\n");
            continue;
        }
        if(buffer[0]=='c' && buffer[1]=='d'){
            strcpy(shcwd, buffer+3);
            ret = chdir(shcwd);
            
            if (ret != 0) {
                printf("error: %d - %e\n", ret, ret);
                shcwd[0]='\0';
            }
            cprintf("\n");
            /* 更新shell显示前缀 */
            if ((ret = getcwd(shpwd, sizeof(shpwd))) != 0) {
		        return ret;
	        }
            snprintf(prepwd, sizeof(prepwd), "%s$ ", shpwd);
            continue;
        }
        int pid;
        // 运行完fork 0 是子进程，去执行cmd 调用 exec 执行
        if ((pid = fork()) == 0) {
            ret = runcmd(buffer);
            // 子进程的返回值，由父进程读取
            exit(ret);
        }
        assert(pid >= 0);
        // 如果该命令不是跑在后台父进程等待子进程的结束
        if (run_in_backgroud == 0)
        {
            if (waitpid(pid, &ret) == 0) {
                // if (ret == 0 && shcwd[0] != '\0') {
                //     ret = chdir(shcwd);
                // }
                if (ret != 0) {
                    printf("error: %d - %e\n", ret, ret);
                }
            }
        }
        else
            printf("proccess %d run in background\n", pid);
    }
    return 0;
}

