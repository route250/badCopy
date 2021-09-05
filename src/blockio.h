/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   blockio.h
 * Author: maeda
 *
 * Created on 2021年8月17日, 22:06
 */

#ifndef BLOCKIO_H
#define BLOCKIO_H

#define BS 512
//#define toBlock(x) x<=0 ? 0 : (x-1)*BS+1

typedef long block_t;

bool balloc(void **buf, block_t nblock);
block_t bseek(int fd, block_t bk);
block_t bread(int fd, block_t pos, char *buf, block_t len);
block_t bwrite(int fd, block_t pos, char *buf, block_t off, block_t len);
block_t btrim(int fd, block_t pos, block_t len);
int blkcmp2(char *buf1, int s1, char *buf2, int s2, block_t len);
bool blkcmp(char *buf1, char *buf2, block_t pos);
bool is_zero(char *buf, block_t offset);
void hsize( long bytes, char *dst );

#endif /* BLOCKIO_H */

