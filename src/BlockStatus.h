/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   BlockStatus.h
 * Author: maeda
 *
 * Created on 2021年8月18日, 1:00
 */

#ifndef BLOCKSTATUS_H
#define BLOCKSTATUS_H

#include <map>
#include <vector>

#include "blockio.h"

typedef enum tIOStatus {
    Unknown, Skip, Cp1, Success2, Diff1, IOError1, Cp2, Diff2,IOError2,Cp3,Diff3,IOError3,Cp4,Diff4,IOError4,Cp5,Diff5,IOError5, Success, Success3, Bad, Null
} IOStatus;
const char *toString( IOStatus aStatus );
IOStatus toIOStatus( char * aStatus );

class BlockStatus {
public:
    BlockStatus(block_t aStart, block_t aEnd, IOStatus aStatus );
    int compareTo( block_t bno );
    block_t mBegin;
    block_t mEnd;
    IOStatus mStatus;
private:
};

class ImgStatus {
public:
    ImgStatus( char *aFileName );
    ImgStatus( char *infile, char *outfile );
    ImgStatus( const ImgStatus &) = delete;
    virtual ~ImgStatus();
    char *getFileName();
    bool load();
    void save();
    void flush();
    void setStatus( block_t bno, IOStatus status );
    IOStatus getStatus( block_t b );
    IOStatus getLowestStatus( block_t begin, block_t end);
    block_t getBlock( block_t bno, block_t end, IOStatus aStatus, block_t &b, block_t &e );
    bool dump();
private:
    char mFileName[1025];
    char mInFileName[1025];
    char mOutFileName[1025];
    BlockStatus *mData = NULL;
    int mSize = 0;
    int mMaxSize = 0;
    int mUpdated = 0;
    int search( block_t no );
};

#endif /* BLOCKSTATUS_H */

