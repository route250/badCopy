/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   WriteBuffer.cpp
 * Author: maeda
 * 
 * Created on 2021年8月19日, 3:54
 */
#include <string.h>
#include <errno.h>

#include "WriteBuffer.h"

WriteBuffer::WriteBuffer( ) {
    mPos = 0;
    mLen = 0;
}

WriteBuffer::WriteBuffer( const WriteBuffer &ref ) {
    printf("ERROR:copy constructor of WriteBuffer\n");
}

WriteBuffer::~WriteBuffer( ) {
    if( mRdBuf != NULL ) {
        free( mRdBuf );
        mRdBuf = NULL;
    }
    if( mWrBuf != NULL ) {
        free( mWrBuf );
        mWrBuf = NULL;
    }
}

void WriteBuffer::setLoglevel( int aLv ) {
    mLogLevel = aLv;
}

void WriteBuffer::setSparse( bool aOn ) {
    mSparse = aOn;
}
void WriteBuffer::setBlockDevice( bool aOn ) {
    mBlockDev = aOn;
}

bool WriteBuffer::updated( ) {
    return mUpdated;
}

bool WriteBuffer::allocate( ) {
    mPos = 0;
    mLen = 0;
    if( !balloc( (void **) &mRdBuf, mBufferSize ) ) {
        return false;
    }
    if( !balloc( (void **) &mWrBuf, mBufferSize ) ) {
        return false;
    }
    return true;
}

/**
 * 出力バッファをフラッシュする
 * @param fd
 * @return
 */
int WriteBuffer::flush( ImgStatus &aStatus, int fd ) {
    if( mLen <= 0 ) {
        return 0;
    }
    memset( mRdBuf, 0, mBufferSize * BS );
    //bool same = true;
    block_t read_res = -1;
    if( fd > 0 ) {
        read_res = bread( fd, mPos, mRdBuf, mLen );
        if( read_res < 0 ) {
            printf( "out bread:%lds %s\n", mPos, strerror( errno ) );
            return -5;
        }
        if( mLogLevel > 1 ) {
            printf( "out bread:%lds - %lds\n", mPos, mPos + mLen - 1 );
        }

//        same = ( mLen == read_res ) && blkcmp( mWrBuf, 0, mRdBuf, 0, mLen ) == mLen;
//        if( same ) {
//            for( block_t b = mPos, e = mPos + mLen; b < e; b += 1 ) {
//                aStatus.setStatus( b, Success );
//            }
//        } else {
//            mUpdated = true;
//        }
    } else {
        read_res = mLen;
        memcpy( mRdBuf, mWrBuf, mLen*BS );
    }

    block_t start = 0;
    block_t end = mLen;
    for(; start < mLen; ) {
        block_t pos = mPos + start;
        end = start + 1;
        bool is_zero_block = is_zero( mWrBuf, start );
        bool is_same = start<read_res && blkcmp( mWrBuf, mRdBuf,start);
        for(; end < mLen; end++ ) {
            bool is_zero_block2 = is_zero( mWrBuf, end );
            bool is_same2 = end<read_res && blkcmp( mWrBuf, mRdBuf,end);
            if( is_zero_block != is_zero_block2 || is_same != is_same2 ) {
                break;
            }
        }
        if( !is_same ) {
            mUpdated = true;
        }
        block_t len = end - start;
        if( !is_same || is_zero_block ) {
            if( is_zero_block ) {
                if( mSparse && len >= 4 ) {
                    block_t wlen = btrim( fd, pos, len );
                    if( wlen != len ) {
                        printf( "out zero   trim:%lds %s\n", pos, strerror( errno ) );
                        return -4;
                    }
                    if( mLogLevel > 1 ) {
                        printf( "out zero   trim:%lds - %lds\n", pos, pos + len - 1 );
                    }
                } else {
                    block_t wlen = bwrite( fd, pos, mWrBuf, start, len );
                    if( wlen != len ) {
                        printf( "out zero bwrite:%lds %s\n", pos, strerror( errno ) );
                        return -4;
                    }
                    if( mLogLevel > 1 ) {
                        printf( "out zero bwrite:%lds - %lds\n", pos, pos + len - 1 );
                    }
                }
            } else {
                block_t wlen = bwrite( fd, pos, mWrBuf, start, len );
                if( wlen != len ) {
                    printf( "out bwrite:%lds %s\n", pos, strerror( errno ) );
                    return -4;
                }
                if( mLogLevel > 1 ) {
                    printf( "out data bwrite:%lds - %lds\n", pos, pos + len - 1 );
                }
            }
        } else {
            if( mLogLevel > 1 ) {
                if( is_zero_block ) {
                    printf( "out zero   keep:%lds - %lds\n", pos, pos + len - 1 );
                } else {
                    printf( "out data   keep:%lds - %lds\n", pos, pos + len - 1 );
                }
            }
        }
        if( !is_same ) {
            for( block_t b = pos, e = pos + len; b < e; b += 1 ) {
                IOStatus st = aStatus.getStatus(b);
                IOStatus newst = st;
                switch( st ) {
                    case Unknown:
                    case Skip:
                        newst = Cp1;
                        break;
                    case Success2:
                        newst = Success3;
                        break;
                    case IOError1:
                        newst = Cp1;
                        break;
                    case IOError2:
                        newst = Cp3;
                        break;
                    case Cp1:
                        newst = Diff1;
                        break;
                    case Cp2:
                        newst = Diff2;
                        break;
                    case Cp3:
                        newst = Diff3;
                        break;
                }
                if( newst != st ) {
                    aStatus.setStatus( b, newst );
                } else {
                    printf("[DBG] diff ignore %s\n", toString(st));
                }
            }
        } else {
            for( block_t b = pos, e = pos + len; b < e; b += 1 ) {
                IOStatus st = aStatus.getStatus(b);
                IOStatus newst = st;
                switch( st ) {
                    case Unknown:
                    case Skip:
                    case Success2:
                        newst = Success;
                        break;
                    case IOError1:
                        newst = Success3;
                        break;
                    case IOError2:
                        newst = Success;
                        break;
                    case Cp1:
                        newst = Success;
                        break;
                    case Cp2:
                        newst = Success2;
                        break;
                    case Cp3:
                        newst = Success;
                        break;
                }
                if( newst != st ) {
                    aStatus.setStatus( b, newst );
                } else {
                    printf("[DBG] same ignore %s\n", toString(st));
                }
            }
        }
        start = end;
    }

    memset( mWrBuf, 0, mBufferSize * BS );
    mPos = mPos + mLen;
    mLen = 0;
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
int WriteBuffer::append( ImgStatus &aStatus, int fd, block_t aPos, char *buf, block_t aOff, block_t aLen ) {
    if( (aPos+aLen) == mPos && (mLen+aLen)<mBufferSize ) {
        // バッファの先頭に追加してサイズが足りる時
        for( int i=(mLen*BS)-1; i>=0; i-- ) {
            mWrBuf[i+aLen*BS] = mWrBuf[i];
        }
        for( int i=0; i<aLen*BS; i++ ) {
            mWrBuf[i] = buf[i+aOff*BS];
        }
        mPos = mPos - aLen;
        mLen = mLen + aLen;
        return 0;
    }
    block_t cur_end = mPos + mLen;
    if( cur_end != aPos ) {
        int ret = flush( aStatus, fd );
        if( ret != 0 ) {
            return ret;
        }
        mPos = aPos;
        mLen = 0;
    }
    block_t src_bk = aOff;
    block_t src_end_bk = aOff + aLen;
    while( src_bk < src_end_bk ) {
        if( mLen >= mBufferSize ) {
            int ret = flush( aStatus, fd );
            if( ret != 0 ) {
                return ret;
            }
            mPos += mLen;
            mLen = 0;
        }
        // cp
        for( int i = mLen * BS, j = src_bk * BS; i < ( mLen + 1 ) * BS; ) {
            mWrBuf[i++] = buf[j++];
        }
        mLen++;
        src_bk++;
    }
    return 0;
}

