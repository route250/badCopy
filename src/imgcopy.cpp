/*
 * To change this license header, choose License Headers in  Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in  the editor.
 */

/*
 * File:   main.c
 * Author: maeda
 *
 * Created on 2021年8月12日, 3:52
 */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/fs.h>

#define CLOCKS_PER_MSEC (CLOCKS_PER_SEC/1000);
//#define BLEN 10
#define BS 512
//#define BUF_SIZE BLEN*BS
#define MAX_STR_LEN 2048

typedef long block_t;

#define toBlock(x) x<=0 ? 0 : (x-1)*BS+1

#define dbg1_printf(fmt, ...)  printf(fmt, __VA_ARGS__)
#define err1_printf(fmt, ...)  printf(fmt, __VA_ARGS__)

#define EXIT_SUCCESS 0
#define EXIT_SAME 1
#define EXIT_SLOW 2
#define EXIT_INTER 5
#define EXIT_ARGS 10
#define EXIT_OPEN 11
#define EXIT_IO 12
#define EXIT_LOST 111
#define EXIT_ABORT 50
int myerrno;

bool isBlockDevice(char *filename) {
    struct stat buffer;
    int exist = stat(filename, &buffer);
    if (exist == 0) {
        if (S_ISBLK(buffer.st_mode)) {
            return true;
        }
    }
    return false;
}

bool isRegularFile(char *filename) {
    struct stat buffer;
    int exist = stat(filename, &buffer);
    if (exist == 0) {
        if (S_ISREG(buffer.st_mode)) {
            return true;
        }
    }
    return false;
}

off_t filesize(char *filename) {
    int fd = open(filename, O_RDONLY | O_NOATIME);
    if( fd<=0 ) {
        return -1;
    }
    struct stat buffer;
    int exist = fstat( fd, &buffer);
    off_t result = -1;
    if (exist == 0) {
        if (S_ISREG(buffer.st_mode)) {
            result = buffer.st_size;
        }
        if (S_ISBLK(buffer.st_mode)) {
            printf("dev %lu %lu\n", buffer.st_dev, buffer.st_rdev);
            long blk = 0L;
            long ssz = 0L;
            errno = 0;
            if( ioctl( fd, BLKGETSIZE, &blk)>=0 && ioctl( fd, BLKSSZGET, &ssz)>=0 ) {
                result = blk*ssz;
            } else {
                printf("ERROR:ioctl %s %s\n",filename,strerror(errno));
            }
        }
    }
    close(fd);
    return result;
}
block_t fileblocks( char *filename ) {
    off_t bytes = filesize( filename );
    if( bytes <=0 ) {
        return bytes;
    }
    return (bytes-1)/BS + 1;
}

bool exist(char *filename) {
    struct stat buffer;
    int exist = stat(filename, &buffer);
    if (exist != 0) {
        return false;
    } else {
        return true;
    }
}

bool exists_partition( dev_t devno ) {
/*
major minor  #blocks  name

8        0  250059096 sda
8        1       8192 sda1
8        2  250049863 sda2
*/
    char buf[4096];
    int major;
    int minor;
    long blocks;
    char name[4096];
    bool result=false;

    errno = 0;
    FILE *fp = fopen("/proc/partitions","r");
    if( fp == NULL || errno != 0 ) {
        printf("ERROR:can not open /proc/partitions:%s\n", strerror(errno));
    } else {
        while( fgets( buf, sizeof(buf),fp ) ) {
            int ss = sscanf( buf, "%d %d %ld %s", &major, &minor, &blocks, name);
            if( ss == 4 ) {
                dev_t x = major<<8 | minor;
                //printf( " major:%d minor:%d dev:%ld blocks:%ld name:%s\n", major, minor, x, blocks, name );
                if( x == devno ) {
                    result = true;
                    break;
                }
            }
        }
        fclose(fp);
    }

    return result;
}

bool exists_fd( int fd ) {
    struct stat buffer;
    int exist = fstat( fd, &buffer);
    bool result = false;
    if (exist == 0) {
        if (S_ISREG(buffer.st_mode)) {
            result = true;
        }
        if (S_ISBLK(buffer.st_mode)) {
            result = exists_partition( buffer.st_rdev );
        }
    }
    return result;
}

int device_check(char *infile, char *cmd, block_t a, block_t b, block_t c, block_t d) {

    if (cmd == NULL || cmd[0] == 0) {
        return EXIT_SUCCESS;
    }

    fflush(stdout);
    fflush(stderr);
    int pid = fork();
    if (pid < 0) {
        return EXIT_ABORT;
    }
    if (pid == 0) {
        char p1[30];
        char p2[30];
        char p3[30];
        char p4[30];
        sprintf(p1, "%ld", a);
        sprintf(p2, "%ld", b);
        sprintf(p3, "%ld", c);
        sprintf(p4, "%ld", d);
        char *args[] = {cmd, infile, p1, p2, p3, p4, NULL};
        errno = 0;
        int ex = execvp(cmd, args);
        printf("ERROR:%s %s\n", cmd, strerror(errno));
        exit(EXIT_ABORT);
    } else {
        int status;
        pid_t result = wait(&status);
        if (result >= 0) {
            if (WIFEXITED(status)) {
                int ret = WEXITSTATUS(status);
                if (ret == 0) {
                    return 0;
                } else if (ret == EXIT_ABORT) {
                    return ret;
                } else if (ret == EXIT_LOST) {
                    return EXIT_LOST;
                } else {
                    printf("ERROR:command %s exit:%d\n", cmd, ret);
                    return EXIT_ABORT;
                }
            } else {
                printf("ERROR:wait fail:%d\n", status);
            }
        } else {
            printf("ERROR:wait error\n");
        }
        return EXIT_ABORT;
    }
}

block_t bseek(int fd, block_t bk) {
    off64_t pos = bk <= 0 ? 0 : bk * BS;
    myerrno = 0;
    errno = 0;
    off64_t nowPos = lseek64(fd, pos, SEEK_SET);
    if (errno != 0) {
        err1_printf("[ERROR](bseek) %s\n", strerror(errno));
        return nowPos < 0 ? nowPos : -1;
    }
    if (nowPos < 0) {
        err1_printf("[ERROR](bseek) return %ld\n", nowPos);
        return nowPos;
    }
    block_t res = nowPos <= 0 ? 0 : (nowPos - 1) / BS + 1;
    if (nowPos != pos || res != bk) {
        err1_printf("[ERROR](bseek) return %lds(%ld) != %lds(%ld)\n", res, nowPos, bk, pos);
        return -2;
    }
    return res;
}

block_t bread0(int fd, char *buf, block_t nblock) {
    myerrno = 0;
    size_t nbytes = nblock * BS;
    errno = 0;
    ssize_t rlen = read(fd, buf, nbytes);
    if (errno != 0) {
        err1_printf("[ERROR](bread0) %lds %ld(bytes):%s\n", nblock, nbytes, strerror(errno));
        return -1;
    }
    if (rlen < 0) {
        err1_printf("[ERROR](bread0) %lds return %ld\n", nblock, rlen);
        return -1;
    }
    if (rlen == 0) {
        //err1_printf("[ERROR](bread) return %ld EOF\n", rlen);
        return 0;
    }
    if (rlen % BS != 0) {
        err1_printf("[ERROR](bread) return %ld invalid BS %d \n", rlen, rlen % BS);
        return -3;
    }
    block_t result = rlen / BS;
    return result;
}

block_t bread(int fd, block_t pos, char *buf, block_t len) {
    block_t end = pos + len;
    if (end <= 0) {
        return -1;
    }
    block_t offset = 0;
    if (pos < 0) {
        offset = 0 - pos;
        pos = 0;
    }
    char *buf2 = buf + (offset * BS);
    block_t len2 = end - pos;
    for (char *p = buf; p < buf2;) {
        *p++ = 0;
    }
    block_t result = offset;
    block_t ox = bseek(fd, pos);
    if (ox != pos) {
        return -2;
    }
    while (result < len) {
        //printf("   bread0 len:%ld len2:%ld result:%ld\n", len, len2, result);
        block_t res = bread0(fd, buf2, len2);
        if (res <= 0) {
            break;
        }
        //printf("   bread0 len:%ld len2:%ld res:%ld result:%ld\n", len, len2, res, result);
        result += res;
        buf2 = buf2 + (res * BS);
        len2 -= res;
    }
    return result;
}

block_t bwrite0(int fd, char *buf, block_t nblock) {
    myerrno = 0;
    errno = 0;
    size_t nbytes = nblock * BS;
    ssize_t rlen = write(fd, buf, nbytes);
    if (errno != 0) {
        err1_printf("[ERROR](bwrite) %s\n", strerror(errno));
        return -1;
    }
    if (rlen <= 0) {
        err1_printf("[ERROR](bwrite) return %ld\n", rlen);
        return -1;
    }
    if (rlen % BS != 0) {
        err1_printf("[ERROR](bwrite) return %ld invalid BS %d \n", rlen, rlen % BS);
        return -3;
    }
    return rlen / BS;
}

block_t bwrite(int fd, block_t pos, char *buf, block_t off, block_t len) {
    block_t ox = bseek(fd, pos);
    if (ox != pos) {
        return -2;
    }
    char *buf2 = buf + (off * BS);
    block_t res = bwrite0(fd, buf2, len);
    return res;
}

block_t btrim(int fd, block_t pos, block_t len) {
    errno = 0;
    int mode = FALLOC_FL_ZERO_RANGE;
    mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
    int res = fallocate(fd, mode, pos*BS, len * BS);
    if (errno != 0) {
        err1_printf("[ERROR](btrim) return %d %s\n", res, strerror(errno));
        return -1;
    }
    if (res != 0) {
        err1_printf("[ERROR](btrim) return %ld\n", res);
        return -1;
    }
    return len;
}

int blkcmp(char *buf1, int s1, char *buf2, int s2, block_t len) {
    for (int i = 0; i < len; i++) {
        if (buf1[s1 + i] != buf2[s2 + i]) {
            return i;
        }
    }
    return len;
}
int log_level = 1;

// 最初に読込み成功した位置
block_t in_check_pos;
// 最初に読込み成功したブロックの内容
char *in_check_buf;
// チェック用バッファ
char *in_check_buf2;

/**
 * 入力ファイルが行きているか確認する
 * @param fd
 * @return 
 */
bool in_check( int fd ) {
    // 一度も成功していなければfalse
    if( in_check_pos<0 ) {
        return false;
    }
    // 過去に成功した位置へseek
    off64_t pos = in_check_pos <= 0 ? 0 : in_check_pos * BS;
    errno = 0;
    off64_t nowPos = lseek64(fd, pos, SEEK_SET);
    if (nowPos != pos) {
        printf("ERROR:(in_check) can not seek %s\n",strerror(errno));
        return false;
    }
    errno = 0;
    // 読み込めるか確認してみる
    ssize_t res = read(fd, (void *)in_check_buf2, BS);
    if( errno != 0 ) {
        printf("ERROR:(in_check) can not read ret:%ld %s\n",res, strerror(errno));
        return false;
    }
    if( res != BS ) {
        printf("ERROR:(in_check) can not read ret:%ld\n",res);
        return false;
    }
    // 読み込めた内容を比較してみる
    for( int i=0; i<BS; i++ ) {
        if( in_check_buf[i] != in_check_buf2[i] ) {
            printf("ERROR:(in_check) data miss match\n");
            return false;
        }
    }
    return true;
}

bool is_zero(char *buf, block_t offset) {
    int p = offset * BS;
    int end = p + BS;
    for (; p < end; p++) {
        if (buf[p] != 0) {
            return false;
        }
    }
    return true;
}

bool in_block_device = false;
// 出力バッファ
bool out_block_device = false;
bool out_sparse = true;

// 出力バッファ最大(1MiB)
block_t out_max = (1024*1024)/BS;
// 出力バッファ先頭のファイル内の位置
block_t out_pos;
// 出力バッファの長さ
block_t out_len;
// 出力バッファ（書き込む内容)
char *out_wbuf;
// 更新チェック用
char *out_rbuf;

// 出力が更新されたかフラグ
bool out_update = false;

// 割り込み受付フラグ
volatile sig_atomic_t e_flag = 0;

/**
 * 出力バッファをフラッシュする
 * @param fd
 * @return 
 */
int out_flush(int fd) {
    if (out_len <= 0) {
        return 0;
    }
    memset(out_rbuf, 0, out_max * BS);
    bool same = true;
    if (fd > 0) {
        block_t read_res = bread(fd, out_pos, out_rbuf, out_len);
        if ( read_res < 0 ) {
            printf("out bread:%lds %s\n", out_pos, strerror(errno));
            return -5;
        }
        if (log_level > 1) {
            printf("out bread:%lds - %lds\n", out_pos, out_pos + out_len - 1);
        }

        same = out_len==read_res && blkcmp(out_wbuf, 0, out_rbuf, 0, out_len) == out_len;
        if (!same) {
            out_update = true;
        }
    }

    block_t start = 0;
    block_t end = out_len;
    for (; start < out_len;) {
        block_t pos = out_pos + start;
        end = start + 1;
        bool is_zero_block = is_zero(out_wbuf, start);
        for (; end < out_len; end++) {
            if (is_zero_block != is_zero(out_wbuf, end)) {
                break;
            }
        }
        block_t len = end - start;
        if (!same || is_zero_block) {
            if (is_zero_block) {
                if (out_sparse && len >= 4) {
                    block_t wlen = btrim(fd, pos, len);
                    if (wlen != len) {
                        printf("out zero   trim:%lds %s\n", pos, strerror(errno));
                        return -4;
                    }
                    if (log_level > 1) {
                        printf("out zero   trim:%lds - %lds\n", pos, pos + len - 1);
                    }
                } else {
                    block_t wlen = bwrite(fd, pos, out_wbuf, start, len);
                    if (wlen != len) {
                        printf("out zero bwrite:%lds %s\n", pos, strerror(errno));
                        return -4;
                    }
                    if (log_level > 1) {
                        printf("out zero bwrite:%lds - %lds\n", pos, pos + len - 1);
                    }
                }
            } else {
                block_t wlen = bwrite(fd, pos, out_wbuf, start, len);
                if (wlen != len) {
                    printf("out bwrite:%lds %s\n", pos, strerror(errno));
                    return -4;
                }
                if (log_level > 1) {
                    printf("out data bwrite:%lds - %lds\n", pos, pos + len - 1);
                }
            }
        } else {
            if (log_level > 1) {
                if (is_zero_block) {
                    printf("out zero   keep:%lds - %lds\n", pos, pos + len - 1);
                } else {
                    printf("out data   keep:%lds - %lds\n", pos, pos + len - 1);
                }
            }
        }
        start = end;
    }

    memset(out_wbuf, 0, out_max * BS);
    out_pos = out_pos + out_len;
    out_len = 0;
    return 0;
}

/**
 * 出力バッファへ追加
 * @param fd
 * @param pos
 * @param buf
 * @param off
 * @param len
 * @return 
 */
int out_write(int fd, block_t pos, char *buf, block_t off, block_t len) {
    block_t cur = out_pos + out_len;
    if (cur != pos) {
        int ret = out_flush(fd);
        if (ret != 0) {
            return ret;
        }
        out_pos = pos;
        out_len = 0;
    }
    block_t src_bk = off;
    block_t src_end_bk = off + len;
    while (src_bk < src_end_bk) {
        if (out_len >= out_max) {
            int ret = out_flush(fd);
            if (ret != 0) {
                return ret;
            }
            out_pos += out_len;
            out_len = 0;
        }
        // cp
        for (int i = out_len * BS, j = src_bk * BS; i < (out_len + 1) * BS;) {
            out_wbuf[i++] = buf[j++];
        }
        out_len++;
        src_bk++;
    }
    return 0;
}

/**
 * メモリ割り当て
 * (O_DIRECTに対応できるブロック境界に割り当てる)
 * @param buf
 * @param nblock
 * @return 
 */
bool balloc( void **buf, block_t nblock ) {
    size_t size = nblock * BS;
    errno = 0;
    int ret = posix_memalign( buf, BS, size );
    if( ret == EINVAL ) {
        // サイズが境界に合わないらしい
        printf("ERROR:can not allocate: invalid size\n");
        return false;
    } else if( ret == ENOMEM ) {
        // メモリが足りないらしい
        printf("ERROR:can not allocate: no memory\n");
        return false;
    } else if( ret != 0 || buf == NULL ) {
        // 上記以外で何かエラーみたいだ
        printf("ERROR:can not allocate %d\n", errno);
        return false;
    }
    // ゼロクリア
    memset( *buf, 0, size);
    return true;
}

/**
 * シグナルハンドラ
 * 
 * @param sig
 */
void abrt_handler( int sig ) {
    e_flag = 1;
}


/**
 * help
 */
void help() {
    printf("\n");
    printf("    -i path : input file\n");
    printf("    -b n : start blosk (0)\n");
    printf("    -e n : end blosk (EOF)\n");
    printf("    -m n : multi sector block (16)\n");
    printf("    -d n : offset before start block (0)\n");
    printf("    -s n : read step size (same as multi sector)\n");
    printf("    -o path : output file\n");
    printf("    -z   : disable parse file\n");
    printf("    -v   : verbose\n");
    printf("    -q   : quiet\n");
}
/*
 *
 */
int xmain(int argc, char** argv) {

    // -------------------------------------------------------------------------
    // シグナルトラップ設定
    // -------------------------------------------------------------------------
    e_flag = 0;
    if( signal( SIGINT, abrt_handler ) == SIG_ERR ) {
        exit(EXIT_IO);
    }
    if( signal( SIGTERM, abrt_handler ) == SIG_ERR ) {
        exit(EXIT_IO);
    }
    if( signal( SIGQUIT, abrt_handler ) == SIG_ERR ) {
        exit(EXIT_IO);
    }
    if( signal( SIGTSTP, abrt_handler ) == SIG_ERR ) {
        exit(EXIT_IO);
    }
    // -------------------------------------------------------------------------
    // コマンドラインオプション
    // -------------------------------------------------------------------------
    block_t cp_start = 0;
    block_t cp_end = 0;
    block_t cp_step = 1;
    block_t multi_sector_size = 16;
    block_t multi_sector_offset = 0;
    char infile[MAX_STR_LEN + 1] = "";
    char outfile[MAX_STR_LEN + 1] = "";
    char checkcmd[MAX_STR_LEN] = "";
    {
        int err = 0;
        bool set_start = false, set_end = false, set_step = false, set_ms_size = false, set_offset = false;
        int opt;
        while ((opt = getopt(argc, argv, "b:e:m:d:s:i:c:o:qvz")) != -1) {
            switch (opt) {
                case 'b':
                    set_start = true;
                    cp_start = atol(optarg);
                    break;
                case 'e':
                    set_end = true;
                    cp_end = atol(optarg);
                    break;
                case 'm':
                    set_ms_size = true;
                    multi_sector_size = atol(optarg);
                    break;
                case 's':
                    set_step = true;
                    cp_step = atol(optarg);
                    break;
                case 'd':
                    set_offset = true;
                    multi_sector_offset = atol(optarg);
                    break;
                case 'i':
                    strncpy(infile, optarg, MAX_STR_LEN);
                    break;
                case 'c':
                    strncpy(checkcmd, optarg, MAX_STR_LEN);
                    break;
                case 'o':
                    strncpy(outfile, optarg, MAX_STR_LEN);
                    break;
                case 'z':
                    out_sparse = false;
                    break;
                case 'v':
                    log_level = 2;
                    break;
                case 'q':
                    log_level = 0;
                    break;
            }
        }
        for (int i = optind; i < argc; i++) {
            printf("arg = %s\n", argv[i]);
        }

        if (infile == NULL || infile[0] == 0) {
            printf("ERROR: not present input file\n");
            err++;
        } else if (isBlockDevice(infile)) {
            in_block_device = true;
            if( log_level > 0 ) {
                printf("in:%s(block)\n", infile);
            }
        } else if (isRegularFile(infile)) {
            if( log_level > 0 ) {
                printf("in:%s(regular)\n", infile);
            }
        } else {
            printf("ERROR: not regular file and block device: %s\n", infile);
            err++;
        }

        if ( outfile==NULL || outfile[0] == 0 ) {
            printf("ERROR: not present output file\n");
            err++;
        } else if (exist(outfile)) {
            if (isBlockDevice(outfile)) {
                printf("out:%s(block)\n", outfile);
                out_block_device = true;
                out_sparse = false;
            } else if (isRegularFile(outfile)) {
                printf("out:%s(regular)\n", outfile);
            } else {
                printf("ERROR: not regular file and block device: %s\n", infile);
                err++;
            }
        } else {
                printf("out:%s(new)\n", outfile);
        }

        if (cp_start < 0) {
            printf("ERROR: invalid start %ld\n", cp_start);
            err++;
        }
        if( !set_end ) {
            set_end = true;
            cp_end = fileblocks( infile );
            printf(" get end %ld\n",cp_end);
        }
        if (cp_start >= cp_end) {
            printf("ERROR: invalid end %ld\n", cp_end);
            err++;
        }
        if (!set_ms_size) {
            multi_sector_size = 16;
        }
        if (multi_sector_size <= 0) {
            printf("ERROR: invalid width %ld\n", multi_sector_size);
            err++;
        }
        if (!set_step) {
            cp_step = multi_sector_size;
        }
        if (cp_step <= 0 || cp_step > multi_sector_size) {
            printf("ERROR: invalid step %ld\n", cp_step);
            err++;
        }
        if (multi_sector_offset < 0 || multi_sector_offset >= multi_sector_size) {
            printf("ERROR: invalid offset %ld\n", multi_sector_offset);
            err++;
        }
        if (log_level > 0) {
            printf("start:%ld end:%ld offfset:%ld width:%ld step:%ld\n", cp_start, cp_end, multi_sector_offset, multi_sector_size, cp_step);
        }
        if (err > 0) {
            help();
            exit(EXIT_ARGS);
        }
    }

    // -------------------------------------------------------------------------
    // IOバッファ割当て
    // -------------------------------------------------------------------------
    char *in_buf, *out_read_buf;
    if( !balloc( (void **) &in_buf, multi_sector_size) ) {
        exit(EXIT_OPEN);
    }
    if( !balloc( (void **) &out_read_buf, multi_sector_size) ) {
        exit(EXIT_OPEN);
    }

    out_pos = 0;
    out_len = 0;
    if( !balloc( (void **) &out_rbuf, out_max ) ) {
        exit(EXIT_OPEN);
    }
    if( !balloc( (void **) &out_wbuf, out_max ) ) {
        exit(EXIT_OPEN);
    }

    in_check_pos = -1;
    if( !balloc( (void **) &in_check_buf, 1 ) ) {
        exit(EXIT_OPEN);
    }
    if( !balloc( (void **) &in_check_buf2, 1 ) ) {
        exit(EXIT_OPEN);
    }

    // -------------------------------------------------------------------------
    //  ファイルオープン
    // -------------------------------------------------------------------------
    int out_fd = -1;
    if (outfile != NULL && outfile[0] != 0) {
        if (log_level > 1) {
            printf("open write:%s\n", outfile);
        }
        errno = 0;
        out_fd = open(outfile, O_RDWR | O_LARGEFILE | O_NOATIME | O_CREAT, 0666);
        if (out_fd < 0 || errno != 0) {
            printf("ERROR:can not open %s %s\n", outfile, strerror(errno));
            exit(EXIT_OPEN);
        }
    }

    if (log_level > 1) {
        printf("open read:%s\n", infile);
    }
    errno = 0;
    int in_fd = open(infile, O_RDONLY | O_DIRECT | O_SYNC | O_LARGEFILE | O_NOATIME);
    if (in_fd < 0) {
        printf("ERROR:can not open %s: %s\n", infile, strerror(errno));
        close(out_fd);
        exit(EXIT_OPEN);
    }

    // -------------------------------------------------------------------------
    //  コピー処理
    // -------------------------------------------------------------------------
    int exitcode = EXIT_SUCCESS;

    clock_t start_time = clock();
    clock_t log_time = 0;
    // ---------------------------
    //  マルチセクタ単位のループ
    // ---------------------------
    block_t ms_start = cp_start - multi_sector_offset;
    while (exitcode == EXIT_SUCCESS && ms_start < cp_end) {
        block_t ms_end = ms_start + multi_sector_size;
        if (ms_end > cp_end) {
            ms_end = cp_end;
        }
        if (ms_end <= cp_start) {
            ms_start = ms_end;
            continue;
        }

        if( !in_check( in_fd ) ) {
            printf("ERROR: lost %s\n",infile);
            exitcode = EXIT_LOST;
            break;
        }
        int chk = device_check(infile, checkcmd, cp_start, cp_end, ms_start, ms_end);
        if (chk != 0) {
            exitcode = chk;
            break;
        }
        // ---------------------------
        //  ブロック単位でのループ
        // ---------------------------
        block_t seg_start = ms_start;
        while (seg_start < ms_end) {
            block_t seg_end = seg_start + cp_step;
            if (seg_end <= 0) {
                seg_start = seg_end;
                continue;
            }
            if (seg_end > ms_end) {
                seg_end = ms_end;
            }
            if (seg_start < cp_start) {
                seg_start = cp_start;
            }
            if (seg_start < seg_end) {
                // 読込み
                block_t ms_len = seg_end - ms_start;
                block_t in_readed_bk = bread(in_fd, ms_start, in_buf, ms_len);
                if (ms_len != in_readed_bk) {
                    // ---------------------------
                    //  読込みエラー
                    // ---------------------------
                    printf("ERROR:in  bread pos:%lds - %lds - %lds len:%ld readed:%lds %s\n", ms_start, seg_start, seg_end -1, ms_len, in_readed_bk, strerror(errno));
                    exitcode = EXIT_IO;
                    if( !in_check( in_fd ) ) {
                        printf("ERROR: lost %s\n",infile);
                        exitcode = EXIT_LOST;
                    } else {
                        int chk = device_check(infile, checkcmd, cp_start, cp_end, ms_start, ms_end);
                        if (chk != 0) {
                            exitcode = chk;
                        }
                    }
                    break;
                }
                if (log_level > 1) {
                    printf("in  bread:%lds - %lds - %lds\n", ms_start, seg_start, seg_end - 1);
                }
                // ---------------------------
                //  最初に成功した位置と内容を記録
                // ---------------------------
                if( in_check_pos<0 && ms_len>0 ) {
                    in_check_pos = ms_start;
                    for( int i=0; i<BS; i++ ) {
                        in_check_buf[i] = in_buf[i];
                    }
                }
                // ---------------------------
                //  出力バッファへ追加
                // ---------------------------
                block_t off = seg_start - ms_start;
                block_t len = seg_end - seg_start;
                if (out_write(out_fd, seg_start, in_buf, off, len) != 0) {
                    exitcode = EXIT_IO;
                    break;
                }
            }
            seg_start = seg_end;

            // ---------------------------
            //  処理経過表示
            // ---------------------------
            clock_t now = clock();
            int msec = (now - log_time)/CLOCKS_PER_MSEC;
            if (msec > 10000 || seg_start >= cp_end) {
                log_time = now;
                double sec = (double) (now - start_time) / (double) CLOCKS_PER_SEC;
                double mib = (double) (seg_start - cp_start)*BS / 1024.0 / 1024.0;
                double mib_per_sec = mib / sec;
                block_t yyy = seg_start - cp_start;
                block_t cp_len = cp_end - cp_start;
                int progress_rate = (yyy * 100) / cp_len;
                printf("%ld/%ld(sec) %d%% %.3f(MiB/Sec)\n", seg_start, cp_end, progress_rate, mib_per_sec);
            }
            // ---------------------------
            //  割り込み停止
            // ---------------------------
            if( e_flag != 0 ) {
                printf("[INTERRUPTED]\n");
                log_level = 2;
                exitcode = EXIT_INTER;
            }
        }
        ms_start = ms_end;
    }

    // ---------------------------
    //  出力バッファを掃き出す
    // ---------------------------
    out_flush(out_fd);

    // -------------------------------------------------------------------------
    //  クローズ
    // -------------------------------------------------------------------------
    if (out_fd >= 0) {
        if (log_level > 1) {
            printf("close outfile\n");
        }
        close(out_fd);
    }
    if (log_level > 1) {
        printf("close infile\n");
    }
    close(in_fd);

    // -------------------------------------------------------------------------
    //  終了コード調整
    // -------------------------------------------------------------------------
    if (exitcode == EXIT_SUCCESS) {
        if (out_update) {
            printf("Updated\n");
        } else {
            printf("No update\n");
            exitcode = EXIT_SAME;
        }
    } else {
        printf("ERROR: exit %d\n", exitcode);
    }
    exit(exitcode);

}

