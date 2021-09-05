/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * File:   main.cpp
 * Author: maeda
 *
 * Created on 2021年8月17日, 13:25
 */

//#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <cstdlib>
#include <iostream>

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

#include "debug.h"
#include "blockio.h"
#include "BlockStatus.h"
#include "WriteBuffer.h"

#define MAX_STR_LEN 2048

#define EXIT_SUCCESS 0
#define EXIT_SAME 1
#define EXIT_SLOW 2
#define EXIT_INTER 5
#define EXIT_ARGS 10
#define EXIT_OPEN 11
#define EXIT_IO 12
#define EXIT_LOST 111
#define EXIT_ABORT 50

int log_level = 1;

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
    if (fd <= 0) {
        return -1;
    }
    struct stat buffer;
    int exist = fstat(fd, &buffer);
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
            if (ioctl(fd, BLKGETSIZE, &blk) >= 0 && ioctl(fd, BLKSSZGET, &ssz) >= 0) {
                result = blk*ssz;
            } else {
                printf("ERROR:ioctl %s %s\n", filename, strerror(errno));
            }
        }
    }
    close(fd);
    return result;
}

block_t fileblocks(char *filename) {
    off_t bytes = filesize(filename);
    if (bytes <= 0) {
        return bytes;
    }
    return (bytes - 1) / BS + 1;
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

bool exists_partition(dev_t devno) {
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
    bool result = false;

    errno = 0;
    FILE *fp = fopen("/proc/partitions", "r");
    if (fp == NULL || errno != 0) {
        printf("ERROR:can not open /proc/partitions:%s\n", strerror(errno));
    } else {
        while (fgets(buf, sizeof (buf), fp)) {
            int ss = sscanf(buf, "%d %d %ld %s", &major, &minor, &blocks, name);
            if (ss == 4) {
                dev_t x = major << 8 | minor;
                //printf( " major:%d minor:%d dev:%ld blocks:%ld name:%s\n", major, minor, x, blocks, name );
                if (x == devno) {
                    result = true;
                    break;
                }
            }
        }
        fclose(fp);
    }

    return result;
}

bool exists_fd(int fd) {
    struct stat buffer;
    int exist = fstat(fd, &buffer);
    bool result = false;
    if (exist == 0) {
        if (S_ISREG(buffer.st_mode)) {
            result = true;
        }
        if (S_ISBLK(buffer.st_mode)) {
            result = exists_partition(buffer.st_rdev);
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

class Check {
private:
    // 最初に読込み成功した位置
    block_t in_check_pos;
    // 最初に読込み成功したブロックの内容
    char *in_check_buf;
    // チェック用バッファ
    char *in_check_buf2;
public:

    bool allocate() {
        in_check_pos = -1;
        if (!balloc((void **) &in_check_buf, 1)) {
            exit(EXIT_OPEN);
        }
        if (!balloc((void **) &in_check_buf2, 1)) {
            exit(EXIT_OPEN);
        }
        return true;
    }

    bool haveData() {
        return in_check_pos >= 0;
    }

    void set(block_t pos, char *buf) {
        // ---------------------------
        //  最初に成功した位置と内容を記録
        // ---------------------------
        if (in_check_pos < 0) {
            in_check_pos = pos;
            for (int i = 0; i < BS; i++) {
                in_check_buf[i] = buf[i];
            }
        }
    }

    /**
     * 入力ファイルが行きているか確認する
     * @param fd
     * @return
     */
    bool in_check(int fd) {
        // 一度も成功していなければfalse
        if (in_check_pos < 0) {
            return true;
        }
        // 過去に成功した位置へseek
        off64_t pos = in_check_pos <= 0 ? 0 : in_check_pos * BS;
        errno = 0;
        off64_t nowPos = lseek64(fd, pos, SEEK_SET);
        if (nowPos != pos) {
            printf("ERROR:(in_check) can not seek %s\n", strerror(errno));
            return false;
        }
        errno = 0;
        // 読み込めるか確認してみる
        ssize_t res = read(fd, (void *) in_check_buf2, BS);
        if (errno != 0) {
            printf("ERROR:(in_check) can not read ret:%ld %s\n", res, strerror(errno));
            return false;
        }
        if (res != BS) {
            printf("ERROR:(in_check) can not read ret:%ld\n", res);
            return false;
        }
        // 読み込めた内容を比較してみる
        for (int i = 0; i < BS; i++) {
            if (in_check_buf[i] != in_check_buf2[i]) {
                printf("ERROR:(in_check) data miss match\n");
                return false;
            }
        }
        return true;
    }

} Check;


bool in_block_device = false;
block_t IN_LENGTH = 0;
int exitcode = 0;
block_t TotalBlocks = 0;
block_t ProgressBlocks = 0;
block_t LogBlock = 0;
time_t log_time = 0;
long clock_count = 0;


// 割り込み受付フラグ
volatile sig_atomic_t e_flag = 0;

/**
 * シグナルハンドラ
 *
 * @param sig
 */
void abrt_handler(int sig) {
    e_flag = 1;
}

/**
 * マルチセクタ単位での開始終了位置を計算する
 * 
 * @param bstart
 * @param bend
 * @param ms
 * @param offset
 * @param bno
 * @param mstart
 * @param mend
 */
bool to_multi_sector( block_t bstart, block_t bend, block_t ms, block_t offset, block_t bno, block_t &mstart, block_t &mend ) {
    if( bno<bstart || bend<=bno ) {
        return false;
    }
    block_t s = (bno+offset)/ms;
    mstart = s*ms - offset;
    mend = mstart + ms;
    if( mstart <bstart ) {
        mstart = bstart;
    }
    if( mend>bend ) {
        mend = bend;
    }
    return true;
}

void imgcopy( ImgStatus &zStatus, WriteBuffer &zWB, char *infile, int in_fd, int out_fd, char *in_buf,
        block_t cp_start, block_t cp_end, block_t cp_step, block_t multi_sector_size, block_t multi_sector_offset,
        IOStatus zTargetStatus, char *checkcmd  ) {
    
    char work[1025];
    char work2[1025];
    // -------------------------------------------------------------------------
    //  コピー処理
    // -------------------------------------------------------------------------
    long zTotalReadBlocks = 0;
    long zLogReadBlocks = 0;

    time_t start_time = time(NULL);
    time_t now = start_time;
    //int clock_count = 0;
    //time_t log_time = 0;
    time_t save_time = start_time;
    bool check = true;
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
        if( check ) {
            check = false;
//            if (!Check.in_check(in_fd)) {
//                printf("ERROR: lost %s\n", infile);
//                exitcode = EXIT_LOST;
//                break;
//            }
//            int chk = device_check(infile, checkcmd, cp_start, cp_end, ms_start, ms_end);
//            if (chk != 0) {
//                exitcode = chk;
//                break;
//            }
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

            if ( seg_start < seg_end) {
                check = true;
                //clock_count=0;
                // 読込み
                block_t ms_len = seg_end - ms_start;
                block_t in_readed_bk = bread(in_fd, ms_start, in_buf, ms_len);
                if (ms_len != in_readed_bk) {
                    // ---------------------------
                    //  読込みエラー
                    // ---------------------------
                    printf("ERROR:in  bread pos:%lds - %lds - %lds len:%ld readed:%lds %s\n", ms_start, seg_start, seg_end - 1, ms_len, in_readed_bk, strerror(errno));
                    for( block_t err_bk = seg_start; err_bk <seg_end; err_bk += 1 ) {
                        IOStatus st = zStatus.getStatus( err_bk );
                        IOStatus newst = st;
                        switch( st ) {
                            case IOError2: newst = IOError3; break;
                            case IOError1: newst = IOError2; break;
                            case Diff2: newst = Diff3; break;
                            case Diff1: newst = Diff2; break;
                            case Skip: newst = IOError1; break;
                            case Cp2: newst = Cp3; break;
                            case Cp1: newst = Cp2; break;
                            case Unknown: newst = IOError1; break;
                        }
                        if( st != newst ) {
                            zStatus.setStatus( err_bk, newst );
                        }
                    }
                    if( zTargetStatus == Unknown ) {
                        block_t skip_end = ms_start + multi_sector_size*2000;
                        for( block_t err_bk = seg_end; err_bk < skip_end && err_bk<cp_end; err_bk+=1 ) {
                            IOStatus st = zStatus.getStatus( err_bk );
                            if( st == Unknown ) {
                                zStatus.setStatus( err_bk, Skip );
                            } else {
                                break;
                            }
                        }
                    }
                    exitcode = EXIT_IO;
                    if (Check.haveData() < 0 || !Check.in_check(in_fd)) {
                        printf("ERROR: lost %s\n", infile);
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
                if (!Check.haveData() && ms_len > 0) {
                    Check.set(ms_start, in_buf);
                }
                // ---------------------------
                //  出力バッファへ追加
                // ---------------------------
                block_t off = seg_start - ms_start;
                block_t len = seg_end - seg_start;
                if (zWB.append( zStatus, out_fd, seg_start, in_buf, off, len) != 0) {
                    exitcode = EXIT_IO;
                    break;
                }
                zTotalReadBlocks += len;
            }
            seg_start = seg_end;

            if( clock_count == 0 ) {
                now = time(NULL);
                int sec1 = (now - save_time);
                if( sec1 > 2 ) {
                    zStatus.flush();
                    save_time = now;
                }
                // ---------------------------
                //  処理経過表示
                // ---------------------------
                int sec2 = (now - log_time);
                if (sec2 > 10 || seg_start >= cp_end) {
                    double lap_sec = (double)(now - log_time);
                    double lap_bytes = ( zTotalReadBlocks - zLogReadBlocks ) * BS;
                    log_time = now;
                    zLogReadBlocks = zTotalReadBlocks;
                    double zLap_mib_per_sec = (lap_bytes/1024.0/1024.0) / lap_sec;
                    double sec = (double) (now - start_time);
                    double mib = (double) (seg_start - cp_start) * BS / 1024.0 / 1024.0;
                    double mib_per_sec = mib / sec;
                    block_t yyy = seg_start - cp_start;
                    block_t cp_len = cp_end - cp_start;
                    int progress_rate = (yyy * 100) / cp_len;
                    hsize( (seg_end - cp_start)*BS, work );
                    hsize( zTotalReadBlocks*BS, work2 );
                    printf("%ld/%ld(sec) %d%% %8s %8s %.3f(MiB/Sec)\n", seg_start, cp_end, progress_rate, work, work2, zLap_mib_per_sec);
                }
                fflush(stdout);
                fflush(stderr);
            }
            clock_count = (clock_count+1) % 300;
            // ---------------------------
            //  割り込み停止
            // ---------------------------
            if (e_flag != 0) {
                printf("[INTERRUPTED]\n");
                log_level = 2;
                exitcode = EXIT_INTER;
            }
        }
        ms_start = ms_end;
    }
}
void imgcopyII( ImgStatus &zStatus, WriteBuffer &zWB, char *infile, int in_fd, int out_fd, char *in_buf,
        block_t cp_start, block_t cp_end, block_t cp_step, block_t multi_sector_size, block_t multi_sector_offset,
        bool reverse, char *checkcmd  ) {
    
    char work[1025];
    char work2[1025];
    // -------------------------------------------------------------------------
    //  コピー処理
    // -------------------------------------------------------------------------
    time_t start_time = time(NULL);
    time_t now = start_time;
    //int clock_count = 0;
    time_t save_time = start_time;
    int check = 0;
    // ---------------------------
    //  マルチセクタ単位のループ
    // ---------------------------
    block_t ms_start,ms_end;
    block_t next;
    if( !reverse ) {
        next = cp_start;
    } else {
        next = cp_end -1;
    }
    exitcode = EXIT_SUCCESS;
    while( to_multi_sector( cp_start, cp_end, multi_sector_size, multi_sector_offset, next, ms_start, ms_end ) ) {
        if( exitcode != EXIT_SUCCESS ) {
            break;
        }
        if (ms_end > cp_end) {
            ms_end = cp_end;
        }
        if (ms_end <= cp_start) {
            ms_start = ms_end;
            continue;
        }
        if( (check++) == 0 ) {
            if (!Check.in_check(in_fd)) {
                printf("ERROR: lost %s\n", infile);
                exitcode = EXIT_LOST;
                break;
            }
            int chk = device_check(infile, checkcmd, cp_start, cp_end, ms_start, ms_end);
            if (chk != 0) {
                exitcode = chk;
                break;
            }
        }
        //printf("[DBG] multi sector %ld-%ld\n",ms_start,ms_end);
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

            if ( seg_start < seg_end) {
                check = 0;
                //clock_count=0;
                // 読込み
                block_t ms_len = seg_end - ms_start;
                block_t in_readed_bk = bread(in_fd, ms_start, in_buf, ms_len);
                if (ms_len != in_readed_bk) {
                    // ---------------------------
                    //  読込みエラー
                    // ---------------------------
                    block_t err_top = ms_start;
                    if( in_readed_bk>0 ) {
                        err_top = err_top + in_readed_bk;
                    }
                    printf("ERROR:in  bread pos:%lds - %lds - %lds len:%ld readed:%lds %s\n", ms_start, seg_start, seg_end - 1, ms_len, in_readed_bk, strerror(errno));
                    for( block_t err_bk = err_top; err_bk <seg_end; err_bk += 1 ) {
                        IOStatus st = zStatus.getStatus( err_bk );
                        IOStatus newst = st;
                        switch( st ) {
                            case Unknown:
                            case Skip:
                                newst = IOError1;
                                break;
                            case IOError1:
                                newst = IOError2;
                                break;
                            case IOError2:
                                newst = IOError3;
                                break;
                            case Cp1:
                                newst = IOError1;
                                break;
                            case Cp2:
                                newst = IOError2;
                                break;
                            case Cp3:
                                newst = IOError3;
                                break;
                        }
                        if( st != newst ) {
                            zStatus.setStatus( err_bk, newst );
                        }
                    }
                    if( !reverse ) {
                        for( block_t err_bk = seg_end; err_bk < cp_end; err_bk+=1 ) {
                            IOStatus st = zStatus.getStatus( err_bk );
                            if( st == IOError1 ) {
                                zStatus.setStatus( err_bk, IOError2 );
                            } else if( st == IOError2 ) {
                                zStatus.setStatus( err_bk, IOError3 );
                            }
                        }
                    }
                    if( !reverse ) {
                        block_t skip_end = err_top + multi_sector_size*2000;
                        for( block_t err_bk = seg_end; err_bk < skip_end && err_bk<cp_end; err_bk+=1 ) {
                            IOStatus st = zStatus.getStatus( err_bk );
                            if( st == Unknown ) {
                                zStatus.setStatus( err_bk, Skip );
                            } else {
                                break;
                            }
                        }
                    }
                    exitcode = EXIT_IO;
                    if (Check.haveData() < 0 || !Check.in_check(in_fd)) {
                        printf("ERROR: lost %s\n", infile);
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
                if (!Check.haveData() && ms_len > 0) {
                    Check.set(ms_start, in_buf);
                }
                // ---------------------------
                //  出力バッファへ追加
                // ---------------------------
                block_t off = seg_start - ms_start;
                block_t len = seg_end - seg_start;
                if (zWB.append( zStatus, out_fd, seg_start, in_buf, off, len) != 0) {
                    exitcode = EXIT_IO;
                    break;
                }
                ProgressBlocks += len;
            }
            seg_start = seg_end;
            bool last = false;
            if( !reverse ) {
                last = cp_end<=seg_start;
            } else {
                last = ms_end<=seg_start && ms_start <= cp_start;
            }
            if( last ) {
                zWB.flush(zStatus,out_fd);
            }

            if( clock_count == 0 || last ) {
                now = time(NULL);
                int sec1 = (now - save_time);
                if( sec1 > 2 || last ) {
                    zStatus.flush();
                    save_time = now;
                }
                // ---------------------------
                //  処理経過表示
                // ---------------------------
                int sec2 = (now - log_time);
                if (sec2 > 10 || last ) {
                    double lap_sec = (double)(now - log_time);
                    double lap_bytes = ( ProgressBlocks - LogBlock ) * BS;
                    log_time = now;
                    LogBlock = ProgressBlocks;
                    double zLap_mib_per_sec = (lap_bytes/1024.0/1024.0) / lap_sec;
                    block_t yyy = seg_start - cp_start;
                    int progress_rate = (ProgressBlocks * 100) / TotalBlocks;
                    hsize( ProgressBlocks*BS, work );
                    hsize( TotalBlocks*BS, work2 );
                    printf("%lds %ld/%lds %d%% %8s %8s %.3f(MiB/Sec)\n", ms_start, ProgressBlocks, TotalBlocks, progress_rate, work, work2, zLap_mib_per_sec);
                }
                fflush(stdout);
                fflush(stderr);
            }
            clock_count = (clock_count+1) % 300;
            // ---------------------------
            //  割り込み停止
            // ---------------------------
            if (e_flag != 0) {
                printf("[INTERRUPTED]\n");
                log_level = 2;
                exitcode = EXIT_INTER;
            }
        }
        if( !reverse ) {
            next = ms_end;
        } else {
            next = ms_start -1;
        }
    }
}

typedef enum t_copy_mode {NoAction,A,B,C,D,TrimBegin,TrimEnd,Split} CopyMode;
const char *CopyModeStr[] = { "NoAction","A","B","C","D","TrimBegin","TrimEnd","Split"};

void imgcopyIII( ImgStatus &zStatus, WriteBuffer &zWB, char *infile, int in_fd, int out_fd, char *in_buf,
        block_t cp_start, block_t cp_end, block_t cp_step, block_t multi_sector_size, block_t multi_sector_offset,
        bool reverse, char *checkcmd  ) {
    
    char work[1025];
    char work2[1025];
    // -------------------------------------------------------------------------
    //  モード判定
    // -------------------------------------------------------------------------
    CopyMode zMode = NoAction;
    IOStatus st0 = zStatus.getStatus(cp_start);
    if( st0 == Unknown || st0 == Skip ) {
        zMode = A;
        if( (cp_end-cp_start)<=multi_sector_size*2 ) {
            cp_step = 1;
        }
    } else if( st0 == Cp1 ) {
        zMode = B;
        if( (cp_end-cp_start)<=multi_sector_size*2 ) {
            cp_step = 1;
        }
    } else if( st0 == Success2 ) {
        zMode = C;
        cp_step = 1;
        multi_sector_size = 1;
        multi_sector_offset = 0;
    } else if( st0 == IOError1) {
        // エラーの前がSuccessなら、先頭から削る法が取れる
        block_t bno = cp_start;
        IOStatus zBeforeSt = bno>0 ? zStatus.getStatus(bno-1) : Bad;
        for( ; cp_start-multi_sector_size < bno; bno=bno-1 ) {
            zBeforeSt = bno>0 ? zStatus.getStatus(bno-1) : Bad;
            if( zBeforeSt != Success && zBeforeSt != Success2 && zBeforeSt != Success3 && zBeforeSt != Cp1 ) {
                break;
            }
        }
        if( bno == cp_start-multi_sector_size ) {
            // 先頭から削れるはず
            zMode = TrimBegin;
            cp_step = 1;
            multi_sector_offset = multi_sector_size - 1;
        } else {
            // 先頭が無理なら、後方から削る方法が取れるか？
            IOStatus zAfterSt = (cp_end)<IN_LENGTH ? zStatus.getStatus(cp_end ) : Success;
            if( zAfterSt == Success ||zAfterSt == Success2||zAfterSt == Success3 || zAfterSt == Cp1) {
                //　後方から削れるはず
                zMode =TrimEnd;
                cp_step = 1;
                multi_sector_size = 1;
                multi_sector_offset = 0;
                reverse = true;
            //} else if( zBeforeSt == Cp2 || zBeforeSt == Cp3 || zBeforeSt == IOError2 || zAfterSt==Cp2 || zAfterSt==Cp3 || zAfterSt == IOError2 ) {
                // 保留
            } else {
                // 前方も後方も無理っぽい
                block_t sz = cp_end -cp_start;
                if( sz>=multi_sector_size ) {
                    // ms以上なら真ん中を試してみる
                    zMode = Split;
                    cp_start = (cp_start+cp_end)/2;
                    cp_end = cp_start + 1;
                    cp_step = 1;
                    multi_sector_offset = 0;
                } else {
                    zMode = Split;
                    cp_start = cp_end-1;
                    cp_step = 1;
                    multi_sector_offset = 0;
                }
            }
        }
        
    }
    if( zMode == NoAction ) {
        ProgressBlocks = ProgressBlocks + (cp_end-cp_start);
        //printf("[NoAction]%lds - %lds %s\n", cp_start, cp_end, toString(st0) );
        return;
    }
    if( log_level>0 ) {
        printf("[Action]%lds - %lds %s %s\n", cp_start, cp_end, toString(st0), CopyModeStr[zMode] );
    }
    fflush(stdout);
    fflush(stderr);

    // -------------------------------------------------------------------------
    //  コピー処理
    // -------------------------------------------------------------------------
    time_t start_time = time(NULL);
    time_t now = start_time;
    //int clock_count = 0;
    time_t save_time = start_time;
    int check = 0;
    // ---------------------------
    //  マルチセクタ単位のループ
    // ---------------------------
    // ループ開始位置
    block_t ms_start, ms_end;
    if( !reverse ) {
        ms_start = cp_start - multi_sector_offset;
    } else {
        ms_end = cp_start - multi_sector_offset;
        while( ms_end < cp_end ) {
            ms_end += multi_sector_size;
        }
    }
    exitcode = EXIT_SUCCESS;
    while (exitcode == EXIT_SUCCESS ) {
        // ループ処理
        if( !reverse ) {
            if( ms_start >=cp_end ) {
                //printf("[DBG]brk %ld\n",ms_start);
                break;
            }
            ms_end = ms_start + multi_sector_size;
            if (cp_end < ms_end) {
                ms_end = cp_end;
            }
            if (ms_end <= cp_start) {
                ms_start = ms_end;
                continue;
            }
        } else {
            if( ms_end <= cp_start ) {
                //printf("[DBG]brk %ld\n",ms_end);
                break;
            }
            ms_start = ms_end - multi_sector_size;
            if( cp_end < ms_end ) {
                ms_end = cp_end;
            }
        }
                
        if( (check++) == 0 ) {
            if (!Check.in_check(in_fd)) {
                printf("ERROR: lost %s\n", infile);
                exitcode = EXIT_LOST;
                break;
            }
            int chk = device_check(infile, checkcmd, cp_start, cp_end, ms_start, ms_end);
            if (chk != 0) {
                exitcode = chk;
                break;
            }
        }
        // ---------------------------
        //  ブロック単位でのループ
        // ---------------------------
        block_t seg_start = ms_start;
        while (seg_start < ms_end) {
            block_t seg_end = seg_start + cp_step;
            if (seg_end <= cp_start) {
                seg_start = seg_end;
                continue;
            }
            if (seg_end > ms_end) {
                seg_end = ms_end;
            }
            if (seg_start < cp_start) {
                seg_start = cp_start;
            }
            //printf("[DBG] cp %ld-%ld  ms %3ld-(%3ld-%3ld)-%3ld\n", cp_start,cp_end, ms_start,seg_start,seg_end,ms_end);
            if ( seg_start < seg_end) {
                check = 0;
                //clock_count=0;
                // 読込み
                block_t ms_len = seg_end - ms_start;
                block_t in_readed_bk = bread(in_fd, ms_start, in_buf, ms_len);
                if (ms_len != in_readed_bk) {
                    // ---------------------------
                    //  読込みエラー
                    // ---------------------------
                    block_t err_top = ms_start;
                    if( in_readed_bk>0 ) {
                        err_top = err_top + in_readed_bk;
                    }
                    printf("ERROR:in  bread pos:%lds - %lds - %lds len:%ld readed:%lds %s\n", ms_start, seg_start, seg_end - 1, ms_len, in_readed_bk, strerror(errno));
                    if( zMode == TrimBegin ) {
                        zStatus.setStatus(seg_start, Bad );
                    } else if( zMode == TrimEnd ) {
                        zStatus.setStatus(seg_start, Bad );
                    } else if( zMode == Split ) {
                        zStatus.setStatus(seg_start, IOError2 );
                    } else if( zMode == A ) {
                        for( block_t err_bk = err_top; err_bk <seg_end; err_bk += 1 ) {
                            zStatus.setStatus( err_bk, IOError1 );
                        }
                        if( !reverse ) {
                            block_t skip_end = err_top + multi_sector_size*2000;
                            for( block_t err_bk = seg_end; err_bk < skip_end && err_bk<cp_end; err_bk+=1 ) {
                                IOStatus st = zStatus.getStatus( err_bk );
                                if( st == Unknown ) {
                                    zStatus.setStatus( err_bk, Skip );
                                } else {
                                    break;
                                }
                            }
                        }
                    } else if( zMode == B ) {
                        for( block_t err_bk = err_top; err_bk <seg_end; err_bk += 1 ) {
                            zStatus.setStatus( err_bk, Cp2 );
                        }
                    } else if( zMode == C ) {
                        for( block_t err_bk = err_top; err_bk <seg_end; err_bk += 1 ) {
                            zStatus.setStatus( err_bk, Success3 );
                        }
                    }
                    exitcode = EXIT_IO;
                    if (Check.haveData() < 0 || !Check.in_check(in_fd)) {
                        printf("ERROR: lost %s\n", infile);
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
                if (!Check.haveData() && ms_len > 0) {
                    Check.set(ms_start, in_buf);
                }
                // ---------------------------
                //  出力バッファへ追加
                // ---------------------------
                block_t off = seg_start - ms_start;
                block_t len = seg_end - seg_start;
                if (zWB.append( zStatus, out_fd, seg_start, in_buf, off, len) != 0) {
                    exitcode = EXIT_IO;
                    break;
                }
                ProgressBlocks += len;
            }
            seg_start = seg_end;
            bool last = false;
            if( !reverse ) {
                last = cp_end<=seg_start;
            } else {
                last = ms_end<=seg_start && ms_start <= cp_start;
            }
            if( last ) {
                zWB.flush(zStatus,out_fd);
            }
            if( clock_count == 0 || last ) {
                now = time(NULL);
                int sec1 = (now - save_time);
                if( sec1 > 2 || last ) {
                    zStatus.flush();
                    save_time = now;
                }
                // ---------------------------
                //  処理経過表示
                // ---------------------------
                int sec2 = (now - log_time);
                if (sec2 > 10 || last ) {
                    double lap_sec = (double)(now - log_time);
                    double lap_bytes = ( ProgressBlocks - LogBlock ) * BS;
                    log_time = now;
                    LogBlock = ProgressBlocks;
                    double zLap_mib_per_sec = (lap_bytes/1024.0/1024.0) / lap_sec;
                    block_t yyy = seg_start - cp_start;
                    int progress_rate = (ProgressBlocks * 100) / TotalBlocks;
                    hsize( ProgressBlocks*BS, work );
                    hsize( TotalBlocks*BS, work2 );
                    printf("%lds %ld/%lds %d%% %8s %8s %.3f(MiB/Sec)\n", ms_start, ProgressBlocks, TotalBlocks, progress_rate, work, work2, zLap_mib_per_sec);
                }
                fflush(stdout);
                fflush(stderr);
            }
            clock_count = (clock_count+1) % 50;
            // ---------------------------
            //  割り込み停止
            // ---------------------------
            if (e_flag != 0) {
                printf("[INTERRUPTED]\n");
                log_level = 2;
                exitcode = EXIT_INTER;
            }
        }
        //printf("[DBG] cp %ld-%ld ms %3ld-%3ld-%3ld-%3ld\n", cp_start,cp_end,ms_start,0L,0L,ms_end);
        
        //　ループ処理（次の値)
        if( !reverse ) {
            ms_start = ms_end;
        } else {
            ms_end = ms_start;
        }
    }
}  

void imgcopyFw2( ImgStatus &zStatus, WriteBuffer &zWB, char *infile, int in_fd, int out_fd, char *in_buf,
        block_t cp_start, block_t cp_end, block_t cp_step, block_t multi_sector_size, block_t multi_sector_offset,
        IOStatus zTargetStatus, char *checkcmd  ) {
    
    size_t bytes = (cp_end-cp_start)*BS;
    if( bytes <= 20 * 1024 * 1024 ) {
        cp_step = 1;
        multi_sector_size = 1;
        multi_sector_offset = 0;
    }
    imgcopy( zStatus, zWB, infile, in_fd, out_fd, in_buf, cp_start, cp_end, cp_step, multi_sector_size, multi_sector_offset, zTargetStatus, checkcmd  );
}
void imgcopyFw1( ImgStatus &zStatus, WriteBuffer &zWB, char *infile, int in_fd, int out_fd, char *in_buf,
        block_t cp_start, block_t cp_end, block_t cp_step, block_t multi_sector_size, block_t multi_sector_offset,
        IOStatus zTargetStatus, char *checkcmd  ) {
    
    cp_step = 1;
    multi_sector_size = 1;
    multi_sector_offset = 0;
    imgcopy( zStatus, zWB, infile, in_fd, out_fd, in_buf, cp_start, cp_end, cp_step, multi_sector_size, multi_sector_offset, zTargetStatus, checkcmd  );
}
void imgcopyRev( ImgStatus &zStatus, WriteBuffer &zWB, char *infile, int in_fd, int out_fd, char *in_buf,
        block_t cp_start, block_t cp_end, block_t cp_step, block_t multi_sector_size, block_t multi_sector_offset,
        IOStatus zTargetStatus, char *checkcmd  ) {
    imgcopy( zStatus, zWB, infile, in_fd, out_fd, in_buf, cp_start, cp_end, 1, 1, 0, zTargetStatus, checkcmd  );
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
int main(int argc, char** argv) {

    // -------------------------------------------------------------------------
    // シグナルトラップ設定
    // -------------------------------------------------------------------------
    e_flag = 0;
    if (signal(SIGINT, abrt_handler) == SIG_ERR) {
        exit(EXIT_IO);
    }
    if (signal(SIGTERM, abrt_handler) == SIG_ERR) {
        exit(EXIT_IO);
    }
    if (signal(SIGQUIT, abrt_handler) == SIG_ERR) {
        exit(EXIT_IO);
    }
    if (signal(SIGTSTP, abrt_handler) == SIG_ERR) {
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
    // 出力バッファ
    bool out_block_device = false;
    bool out_sparse = true;
    // debug用 逆フラグ
    bool un_reverse = false;

    {
        int err = 0;
        bool set_start = false, set_end = false, set_step = false, set_ms_size = false, set_offset = false;
        int opt;
        while ((opt = getopt(argc, argv, "b:e:m:d:s:i:c:o:qvzu")) != -1) {
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
                case 'u':
                    un_reverse = true;
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
            IN_LENGTH = fileblocks(infile);
            if (log_level > 0) {
                printf("in:%s(block) %lds\n", infile,IN_LENGTH);
            }
        } else if (isRegularFile(infile)) {
            IN_LENGTH = fileblocks(infile);
            if (log_level > 0) {
                printf("in:%s(regular) %lds\n", infile,IN_LENGTH);
            }
        } else {
            printf("ERROR: not regular file and block device: %s\n", infile);
            err++;
        }

        if (outfile == NULL || outfile[0] == 0) {
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
        if (!set_end) {
            set_end = true;
            cp_end = IN_LENGTH;
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
            printf("options: start=%ld end=%ld offfset=%ld width=%ld step=%ld\n", cp_start, cp_end, multi_sector_offset, multi_sector_size, cp_step);
        }
        if (err > 0) {
            help();
            exit(EXIT_ARGS);
        }
    }

    // -------------------------------------------------------------------------
    // IOバッファ割当て
    // -------------------------------------------------------------------------
    char *in_buf;
    if (!balloc((void **) &in_buf, multi_sector_size)) {
        exit(EXIT_OPEN);
    }

    WriteBuffer zWB = WriteBuffer();
    zWB.setLoglevel(log_level);
    zWB.setBlockDevice(out_block_device);
    zWB.setSparse(out_sparse);
    if (!zWB.allocate()) {
        exit(EXIT_OPEN);
    }
    if (!Check.allocate()) {
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

    ImgStatus zStatus(infile,outfile);
    printf("Seg:%s\n", zStatus.getFileName() );
    zStatus.load();
    if( log_level>1 ) {
        zStatus.dump();
    }
    IOStatus zTargetStatus = zStatus.getLowestStatus(cp_start,cp_end);
    block_t tb = cp_start;
    block_t te = cp_start;
    TotalBlocks = 0;
    while( zStatus.getBlock( te, cp_end, zTargetStatus, tb, te ) >=0 ) {
        TotalBlocks = TotalBlocks + (te-tb);
    }
    printf("TargetStatus:%s total:%lds\n",toString(zTargetStatus),TotalBlocks);
    ProgressBlocks = 0;
    te = cp_start;
    while( zStatus.getBlock( te, cp_end, zTargetStatus, tb, te ) >=0 ) {
        if( exitcode != EXIT_SUCCESS || e_flag != 0 ) {
            break;
        }
        //printf("TargetStatus:%s %lds - %lds\n",toString(zTargetStatus),tb, te);
        fflush(stdout);
        fflush(stderr);

        imgcopyIII( zStatus, zWB, infile, in_fd, out_fd, in_buf,
            tb, te, cp_step, multi_sector_size, multi_sector_offset,
            un_reverse, checkcmd  );

        //        switch( zTargetStatus ) {
//            case Unknown:
//            case Skip:
//                imgcopyII( zStatus, zWB, infile, in_fd, out_fd, in_buf,
//                    tb, te, cp_step, multi_sector_size, multi_sector_offset,
//                    un_reverse, checkcmd  );
//                break;
//            case Cp1:
//                imgcopyII( zStatus, zWB, infile, in_fd, out_fd, in_buf,
//                    tb, te, cp_step, multi_sector_size, multi_sector_offset,
//                    false, checkcmd  );
//                break;
//            case Cp2:
//            case IOError1:
//                imgcopyII( zStatus, zWB, infile, in_fd, out_fd, in_buf,
//                    tb, te, 1, multi_sector_size, multi_sector_offset,
//                    false, checkcmd  );
//                break;
//            case Cp3:
//            case IOError2:
//                imgcopyII( zStatus, zWB, infile, in_fd, out_fd, in_buf,
//                    tb, te, 1, 1, 1,
//                    true, checkcmd  );
//                break;
//        }
        
    }
    
    // ---------------------------
    //  出力バッファを掃き出す
    // ---------------------------
    zWB.flush(zStatus, out_fd);
    if( log_level>1 ) {
        zStatus.dump();
    }
    zStatus.flush();

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
        if (zWB.updated()) {
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

