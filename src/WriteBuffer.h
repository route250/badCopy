/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   WriteBuffer.h
 * Author: maeda
 *
 * Created on 2021年8月19日, 3:54
 */
#include "blockio.h"
#include "BlockStatus.h"

#ifndef WRITEBUFFER_H
#define WRITEBUFFER_H

class WriteBuffer {
    private:
        int mLogLevel = 0;
    // 出力バッファ最大(1MiB)
    block_t mBufferSize = (1024 * 1024) / BS;
    // 出力バッファ先頭のファイル内の位置
    block_t mPos;
    // 出力バッファの長さ
    block_t mLen;
    // 出力バッファ（書き込む内容)
    char *mWrBuf;
    // 更新チェック用
    char *mRdBuf;
    //
    bool mSparse = true;
    bool mBlockDev = false;
    // 出力が更新されたかフラグ
    bool mUpdated = false;
public:
    WriteBuffer();
    WriteBuffer( const WriteBuffer & );
    virtual ~WriteBuffer();
    void setLoglevel( int aLv );
    void setSparse( bool on );
    void setBlockDevice( bool on );
    bool allocate();
    int flush( ImgStatus &aStatus, int fd) ;
    int append( ImgStatus &aStatus, int fd, block_t aPos, char *buf, block_t aOff, block_t aLen);
    bool updated();
};

#endif /* WRITEBUFFER_H */

