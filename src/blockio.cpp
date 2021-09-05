/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
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

void hsize( long bytes, char *dst ) {
    if( bytes < 1024L ) {
        sprintf(dst,"%ld(B)",bytes);
        return;
    }
    if( bytes < 1024L*1024L ) {
        double kirob = bytes;
        kirob = kirob / 1024.0;
        sprintf(dst,"%.0lf(K)",kirob);
        return;
    }
    if( bytes < 1024L*1024L*1024L ) {
        double megab = bytes;
        megab = megab / 1024.0 / 1024.0;
        sprintf(dst,"%.1lf(M)",megab);
        return;
    }
    if( bytes < 1024L*1024L*1024L*1024L ) {
        double megab = bytes;
        megab = megab / 1024.0 / 1024.0 / 1024.0;
        sprintf(dst,"%.2lf(G)",megab);
        return;
    }
    double terab = bytes;
    terab = terab / 1024.0 / 1024.0 / 1024.0 / 1024.0;
    sprintf(dst,"%.2lf(T)",terab);
}

/**
 * メモリ割り当て
 * (O_DIRECTに対応できるブロック境界に割り当てる)
 * @param buf
 * @param nblock
 * @return
 */
bool balloc(void **buf, block_t nblock) {
    size_t size = nblock * BS;
    errno = 0;
    int ret = posix_memalign(buf, BS, size);
    if (ret == EINVAL) {
        // サイズが境界に合わないらしい
        printf("ERROR:can not allocate: invalid size\n");
        return false;
    } else if (ret == ENOMEM) {
        // メモリが足りないらしい
        printf("ERROR:can not allocate: no memory\n");
        return false;
    } else if (ret != 0 || buf == NULL) {
        // 上記以外で何かエラーみたいだ
        printf("ERROR:can not allocate %d\n", errno);
        return false;
    }
    // ゼロクリア
    memset(*buf, 0, size);
    return true;
}

block_t bseek(int fd, block_t bk) {
    off64_t pos = bk <= 0 ? 0 : bk * BS;
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


bool blkcmp(char *buf1, char *buf2, block_t pos) {
    int s = pos *BS;
    int e = (pos+1)*BS;
    for (int i = s; i < e; i++) {
        if (buf1[i] != buf2[i]) {
            return false;
        }
    }
    return true;
}

int blkcmp2(char *buf1, int s1, char *buf2, int s2, block_t len) {
    for (int i = 0; i < len; i++) {
        if (buf1[s1 + i] != buf2[s2 + i]) {
            return i;
        }
    }
    return len;
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