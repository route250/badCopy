/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   BlockStatus.cpp
 * Author: maeda
 * 
 * Created on 2021年8月18日, 1:00
 */
#include <cstdlib>
#include <iostream>
#include <string>

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

#include "BlockStatus.h"
    
BlockStatus::BlockStatus( block_t aStart, block_t aEnd, IOStatus aStatus ) {
    mBegin = aStart;
    mEnd = aEnd;
    mStatus = aStatus;
}

int BlockStatus::compareTo( block_t bno ) {
    if( bno < mBegin ) {
        return 1;
    } else if( mEnd <= bno ) {
        return -1;
    }
    return 0;
}

int getBaseName( char *filename, char *output ) {
    char *p = filename;
    int n = 0;
    char *top = p;
    for( ;*p != 0; p++ ) {
        if( n++>1023 ) return 0;
        char cc = *p;
        if( cc=='/' ) {
            top=p+1;
        }
    }
    if( top>=p ) {
        *output=0;
        return 0;
    }
    char *last = p;
    char *dst = output;
    n=0;
    while( top < last ) {
        *dst++ = *top++;
        n++;
    }
    *dst++=0;
    return n;
}

int getDirName( char *filename, char *output ) {
    int n = 0;
    char *p = filename;
    char *last = filename-1;
    for( ;*p != 0; p++ ) {
        if( n++>1023 ) return 0;
        char cc = *p;
        if( cc=='/' ) {
            last=p;
        }
    }
    if( last < filename ) {
        *output=0;
        return 0;
    }
    if( last == filename ) {
        last++;
    }
    char *src = filename;
    char *dst = output;
    n=0;
    while( src < last ) {
        *dst++ = *src++;
        n++;
    }
    *dst++=0;
    return n;
}
ImgStatus::ImgStatus( char *infile, char *outfile ) {
    strncpy( mInFileName, infile, 1023);
    strncpy( mOutFileName, outfile, 1023);
    char dir[1025];
    char name1[1025];
    char name2[1025];
    getDirName( outfile, dir );
    getBaseName( infile, name1 );
    getBaseName( outfile, name2 );
    for( char *p = name1; *p!=0; p++ ) {
        if( *p == '.' ) *p = '_';
    }
    for( char *p = name2; *p!=0; p++ ) {
        if( *p == '.' ) *p = '_';
    }
    
    char *dst = mFileName;
    for( char *p = dir; *p!=0; ) {
        *dst++ = *p++;
    }
    if( dst>mFileName ) {
        *dst ++ = '/';
    }
    for( char *p = name1; *p!=0; ) {
        *dst++ = *p++;
    }
    *dst ++ = '_';
    for( char *p = name2; *p!=0; ) {
        *dst++ = *p++;
    }
    *dst++ = '.';
    *dst++ = 's';
    *dst++ = 'e';
    *dst++ = 'g';
    *dst++ = 0;
    char *src = outfile;
    for( int n=0; n<1023; n++ ) {
        char cc = *src++;
        *dst++ = cc;
        if( cc==0 ) break;
    }

}
//ImgStatus::ImgStatus( const ImgStatus &ref ) {
//        printf("ERROR:copy constructor of ImgStatus\n");
//}

ImgStatus::~ImgStatus() {
}

char *ImgStatus::getFileName() {
    return mFileName;
}

bool ImgStatus::load() {
    
    errno = 0;
    FILE *fp = fopen( mFileName, "r" );
    if( fp == NULL || errno != 0) {
        return false;
    }
    char buf[4096];
    long begin0, begin, end0, end;
    char status[256];
    bool start = 0;
    int zSize = 0;
    int zMaxSize = 10;
    BlockStatus *zData = (BlockStatus*)malloc( sizeof(BlockStatus)*zMaxSize );
    while (fgets(buf, sizeof (buf), fp)) {
        for( char *p=buf; *p!=0; p++ ) {
            if( *p == '#' ) {
                *p = 0;
                break;
            }
        }
        int ss = sscanf(buf, "%ld %ld %s", &begin, &end, status);
        if( ss != 3 ) {
            continue;
        }
        if( begin==-1 ) {
            begin = INT64_MIN;
        }
        if( end == -1 ) {
            end = INT64_MAX;
        }
        IOStatus zSt = toIOStatus( status );
        if( !start ) {
            if( begin==INT64_MIN && begin<end && zSt != -1 ) {
                start = true;
                begin0 = INT64_MIN;
                end0 = INT64_MIN;
            }
        }
        if( start ) {
            if( end0 == begin && begin<end && zSt != -1 ) {
                if( zSize+1>=zMaxSize ) {
                    int newMaxSize = zMaxSize + 10;
                    BlockStatus * newData = (BlockStatus*)realloc( zData, sizeof(BlockStatus)*newMaxSize );
                    if( newData == NULL ) {
                        free(zData);
                        return false;
                    }
                    zData = newData;
                    zMaxSize = newMaxSize;
                }
                zData[zSize++] = BlockStatus( begin, end, zSt );
            }
        }
        begin0 = begin;
        end0 = end;
    }
    fclose(fp);
    if( zSize>0 ) {
        mSize = zSize;
        mMaxSize = zMaxSize;
        mData = zData;
        mUpdated = 0;
    }
}

void ImgStatus::save() {
    
    errno = 0;
    FILE *fp = fopen( mFileName, "w" );
    if( fp == NULL || errno != 0) {
        return;
    }
    fprintf(fp, "IN:%s\n",mInFileName);
    fprintf(fp, "OUT:%s\n",mOutFileName);
    char buf[256];
    for( int i=0; i<mSize; i++ ) {
        BlockStatus zSt = mData[i];
        long begin = zSt.mBegin;
        if( begin == INT64_MIN ) {
            begin = -1;
        }
        long end = zSt.mEnd;
        if( end == INT64_MAX ) {
            end = -1;
        }
        if( begin >=0 && begin<end ) {
            hsize( (end-begin)*BS, buf );
        } else {
            buf[0]=0;
        }
        fprintf( fp, "%11d %11d %-8s #%9s\n", begin, end, toString(zSt.mStatus), buf );
    }
    fclose(fp);
    
    mUpdated = 0;
}

void ImgStatus::flush() {
    if( mUpdated!=0 ) {
        save();
    }
}

int ImgStatus::search( block_t xx ) {
    if( mSize == 0 || xx < mData[0].mBegin ) {
        return ~0;
    }
    if( mData[mSize-1].mEnd<=xx ) {
        return ~mSize;
    }
    int left=0, right=mSize, mid;
    while( left < right ) {
        mid = (left + right ) / 2;
        int x = mData[mid].compareTo(xx);
        if( x == 0 ) {
            return mid;
        } else if( 0<x ) {
            right = mid;
        } else {
            left = mid;
        }
    }
    return ~left;
}
void ImgStatus::setStatus( block_t bno, IOStatus aStatus ) {
    if( mData == NULL ) {
        // 初期化
        mMaxSize = 10;
        mData = (BlockStatus*)malloc( sizeof(BlockStatus)*mMaxSize);
        if( mData == NULL ) {
            printf("ERROR:can not allocate\n");
            return;
        }
        mSize = 0;
        if( aStatus == Unknown ) {
            mData[mSize++] = BlockStatus( INT64_MIN, INT64_MAX, Unknown );
        } else {
            mUpdated = 1;
            mData[mSize++] = BlockStatus( INT64_MIN, bno, Unknown );
            mData[mSize++] = BlockStatus( bno, bno+1, aStatus );
            mData[mSize++] = BlockStatus( bno+1, INT64_MAX, Unknown );
        }
        return;
    }
    int idx = search( bno );
    if( idx< 0 ) {
        printf("ERROR:bug in block search?\n");
    }
    BlockStatus zSt1 = mData[idx];
    if( zSt1.compareTo(bno) != 0 ) {
        printf("ERROR:bug in block search?\n");
    }
    if( zSt1.mStatus == aStatus ) {
        // 変更なし
        return;
    }
    mUpdated = 1;
    if( zSt1.mBegin==zSt1.mEnd-1 ) {
        // １ブロック領域
        int src = idx;
        if( mData[src-1].mStatus == aStatus && mData[src+1].mStatus == aStatus ) {
            src+=2;
            mData[idx-1].mEnd = mData[src].mBegin;
        } else if( mData[src-1].mStatus == aStatus ) {
            src++;
            mData[idx-1].mEnd = mData[src].mBegin;
        } else if( mData[src+1].mStatus == aStatus ) {
            src++;
            mData[src].mBegin = mData[idx-1].mEnd;
        } else {
            mData[idx].mStatus = aStatus;
            return;
        }
        while( src<mSize ) {
            mData[idx++] = mData[src++];
        }
        mSize = idx;
        return;
    }
    if( zSt1.mBegin == bno && idx>0 && mData[idx-1].mStatus==aStatus ) {
        // 一つ前と調整
        mData[idx-1].mEnd = bno + 1;
        mData[idx].mBegin = bno + 1;
        return;
    }
    if( zSt1.mEnd == bno+1 && idx<(mSize-1) && mData[idx+1].mStatus==aStatus ) {
        // 一つ後ろと調整
        mData[idx].mEnd = bno;
        mData[idx+1].mBegin = bno;
        return;
    }
    // 領域分割前にメモリ確保
    int newSize = mSize + 1;
    if( zSt1.mBegin < bno && bno < zSt1.mEnd - 1 ) {
        newSize++;
    }
    if( newSize>mMaxSize ) {
        // 足りなければ拡張
        newSize = mMaxSize + 10;
        BlockStatus *newData = (BlockStatus*)realloc( mData, sizeof(BlockStatus)*newSize );
        if( newData == NULL ) {
            printf("ERROR:can not allocate\n");
            return;
        }
        mMaxSize = newSize;
        mData = newData;
    }
    // 領域分割
    if( zSt1.mBegin == bno ) {
        // データ移動
        for( int i=mSize-1; i>=idx; i-- ) {
            mData[i+1] = mData[i];
        }
        mSize++;
        mData[idx] = BlockStatus( bno,bno+1,aStatus);
        mData[idx+1].mBegin = bno+1;
        return;
    }
    if( zSt1.mEnd == bno + 1 ) {
        // データ移動
        for( int i=mSize-1; i>idx; i-- ) {
            mData[i+1] = mData[i];
        }
        mSize++;
        mData[idx].mEnd = bno;
        mData[idx+1] = BlockStatus( bno,bno+1,aStatus);
        return;
    }
    // データ移動
    for( int i=mSize-1; i>idx; i-- ) {
        mData[i+2] = mData[i];
    }
    // 保存
    BlockStatus zSt2 = BlockStatus( bno, bno+1, aStatus );
    BlockStatus zSt3 = BlockStatus( bno+1, zSt1.mEnd, zSt1.mStatus );
    mData[idx].mEnd = bno;
    mData[idx+1] = zSt2;
    mData[idx+2] = zSt3;
    mSize += 2;
}

IOStatus ImgStatus::getStatus( block_t b ) {
    int idx = search( b );
    if( idx<0 ) {
        if( mSize != 0 ) {
            printf("ERROR:bug in block search?\n");
        }
        return Unknown;
    }
    return mData[idx].mStatus;
}

IOStatus ImgStatus::getLowestStatus( block_t aBegin, block_t aEnd ) {
    if( mSize==0 ) {
        return Unknown;
    }
    IOStatus zResult = Null;
    for( int i=0; i<mSize; i++ ) {
        block_t b = mData[i].mBegin;
        block_t e = mData[i].mEnd;
        if( ( b<=aBegin&&aBegin<e) || (b<=aBegin && (aEnd-1)<e) || (aBegin<=b && (e-1)<aEnd) || (b<=(aEnd-1)&&(aEnd-1)<e) ) {
            if( mData[i].mStatus<zResult ) {
                zResult = mData[i].mStatus;
            }
        }
    }
    return zResult;
}
block_t ImgStatus::getBlock( block_t aBegin, block_t aEnd, IOStatus aStatus, block_t &b, block_t &e ) {
    if( mData != NULL ) {
        for( int i=0; i<mSize; i++ ) {
            block_t x1 = mData[i].mBegin;
            if( x1>=aEnd ) {
                break;
            }
            block_t x2 = mData[i].mEnd;
            if( x2>aEnd ) {
                x2 = aEnd;
            }
            if( mData[i].mStatus == aStatus ) {
                if( x1 <= aBegin && aBegin <x2 ) {
                    b = aBegin;
                    e = aEnd<x2 ? aEnd : x2;
                    return aBegin;
                } else if( aBegin<=x1 && x1<aEnd ) {
                    b = x1;
                    e = aEnd<x2 ? aEnd : x2;
                    return x1;
                }
            }
        }
    } else {
        if( aStatus == Unknown && aBegin<aEnd) {
            b = aBegin;
            e = aEnd;
            return aBegin;
        }
    }
    b=-1;
    e=-1;
    return -1;
}

IOStatus toIOStatus( char *aStatus ) {
    if( aStatus == NULL || *aStatus==0 ) {
        return Null;
    }
    if( strncmp( "Unknown", aStatus, 256 )==0 ) {
        return Unknown;
    }
    if( strncmp( "Cp1", aStatus, 256 )==0 ) {
        return Cp1;
    }
    if( strncmp( "Cp2", aStatus, 256 )==0 ) {
        return Cp2;
    }
    if( strncmp( "Cp3", aStatus, 256 )==0 ) {
        return Cp3;
    }
    if( strncmp( "IOError1", aStatus, 256 )==0 ) {
        return IOError1;
    }
    if( strncmp( "IOError2", aStatus, 256 )==0 ) {
        return IOError2;
    }
    if( strncmp( "IOError3", aStatus, 256 )==0 ) {
        return IOError3;
    }
    if( strncmp( "Diff1", aStatus, 256 )==0 ) {
        return Diff1;
    }
    if( strncmp( "Diff2", aStatus, 256 )==0 ) {
        return Diff2;
    }
    if( strncmp( "Diff3", aStatus, 256 )==0 ) {
        return Diff3;
    }
    if( strncmp( "Skip", aStatus, 256 )==0 ) {
        return Skip;
    }
    if( strncmp( "OK", aStatus, 256 )==0 ) {
        return Success;
    }
    if( strncmp( "Ok", aStatus, 256 )==0 ) {
        return Success2;
    }
    if( strncmp( "OkX", aStatus, 256 )==0 ) {
        return Success3;
    }
    if( strncmp( "BAD", aStatus, 256 )==0 ) {
        return Bad;
    }
    return Null;
}
const char *toString( IOStatus aStatus ) {
    switch( aStatus) {
        case Null:
            return "Null";
        case Unknown:
            return "Unknown";
        case Cp1:
            return "Cp1";
        case Cp2:
            return "Cp2";
        case Cp3:
            return "Cp3";
        case Cp4:
            return "Cp4";
        case Cp5:
            return "Cp5";
        case IOError1:
            return "IOError1";
        case IOError2:
            return "IOError2";
        case IOError3:
            return "IOError3";
        case IOError4:
            return "IOError4";
        case IOError5:
            return "IOError5";
        case Diff1:
            return "Diff1";
        case Diff2:
            return "Diff2";
        case Diff3:
            return "Diff3";
        case Diff4:
            return "Diff4";
        case Diff5:
            return "Diff5";
        case Skip:
            return "Skip";
        case Success:
            return "OK";
        case Success2:
            return "Ok";
        case Success3:
            return "OkX";
        case Bad:
            return "BAD";
    }
    return "-----";
}

IOStatus successNext( IOStatus aStatus ) {
    switch(aStatus) {
        case Unknown:
        case Skip:
        case Diff1:
        case IOError1:
            return Cp1;
        case Diff2:
        case IOError2:
            return Cp2;
        case Diff3:
        case IOError3:
            return Cp3;
        case Diff4:
        case IOError4:
            return Cp4;
        case Diff5:
        case IOError5:
            return Cp5;
        case Cp1:
            return Success;
        case Cp2:
            return Success2;
        case Cp3:
            return Success;
        case Cp4:
            return Success;
        case Cp5:
            return Success;
    }
    return aStatus;
}

IOStatus errorNext( IOStatus aStatus ) {
    switch(aStatus) {
        case Unknown:
        case Skip:
        case Cp1:
        case Diff1:
            return IOError1;
        case Cp2:
        case Diff2:
            return IOError2;
        case Cp3:
        case Diff3:
            return IOError3;
        case Cp4:
        case Diff4:
            return IOError4;
        case Cp5:
        case Diff5:
            return IOError5;
        case IOError1:
            return IOError2;
        case IOError2:
            return IOError3;
        case IOError3:
            return IOError4;
        case IOError4:
            return IOError5;
        case IOError5:
            return IOError1;
    }
    return aStatus;
}

bool ImgStatus::dump() {
    
    int error = 0;
    int count = 0;
    for( int i=0; i<mSize; i++ ) {
        if( i>0 ) {
            if( mData[i-1].mEnd != mData[i].mBegin) {
                error++;
            }
            if( mData[i-1].mBegin >= mData[i].mBegin) {
                error++;
            }
            if( mData[i-1].mStatus == mData[i].mStatus) {
                error++;
            }
        }
        if( mData[i].mBegin >= mData[i].mEnd) {
            error++;
        }
        BlockStatus st = mData[i];
        block_t start = st.mBegin;
        if( start == INT64_MIN ) {
            start = -1;
        }
        block_t end = st.mEnd;
        if( end == INT64_MAX ) {
            end = -1;
        }
        printf("%10ld %10ld %s\n", start, end, toString( st.mStatus ) );
        count++;
    }
    if( error>0 ) {
        printf("ERROR!\n");
        return false;
    }
    if( count==0 ) {
        printf("no status data\n");
    }
    return true;
}