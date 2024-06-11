/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __REDIS_RIO_H
#define __REDIS_RIO_H

#include <stdio.h>
#include <stdint.h>
#include "sds.h"

/*
 * RIO API 接口和状态
 */
struct _rio
{

    /* Backend functions.
     * Since this functions do not tolerate short writes or reads the return
     * value is simplified to: zero on error, non zero on complete success. */
    // API
    // 这几个方法在哪里实现的 ？？？
    // 在下面 rioWrite  几个方法中，方法签名是相同的，也就是实现了这个这些方法。
    // rioWrite rioRead rioTell 是通用的方法，包含了buffer 和 file的共同实现。
    // 并且file的实现：rioFileRead rioFileWrite  rioFileTell
    // buffer的实现：rioBufferRead  rioBufferWrite  rioBufferTell
    size_t (*read)(struct _rio *, void *buf, size_t len);
    size_t (*write)(struct _rio *, const void *buf, size_t len);
    off_t (*tell)(struct _rio *);

    /* The update_cksum method if not NULL is used to compute the checksum of
     * all the data that was read or written so far. The method should be
     * designed so that can be called with the current checksum, and the buf
     * and len fields pointing to the new block of data to add to the checksum
     * computation. */
    // 校验和计算函数，每次有写入/读取新数据时都要计算一次
    void (*update_cksum)(struct _rio *, const void *buf, size_t len);

    /* The current checksum */
    // 当前校验和
    uint64_t cksum;

    /* number of bytes read or written */
    size_t processed_bytes;

    /* maximum single read or write chunk size */
    size_t max_processing_chunk;

    /* Backend-specific vars. */
    union
    {

        struct
        {
            // 缓存指针
            // sds 可以当成字节队列。 （Simple Dynamic String，简单动态字符串）
            sds ptr;
            // 偏移量
            off_t pos;
        } buffer;

        struct
        {
            // 被打开文件的指针
            FILE *fp;
            // 最近一次 fsync() 以来，写入的字节量
            off_t buffered; /* Bytes written since last fsync. */
            // 写入多少字节之后，才会自动执行一次 fsync()
            off_t autosync; /* fsync after 'autosync' bytes written. */
        } file;
    } io;
};

typedef struct _rio rio;

/* The following functions are our interface with the stream. They'll call the
 * actual implementation of read / write / tell, and will update the checksum
 * if needed. */

/*
 * 将 buf 中的 len 字节写入到 r 中。
 *
 * 写入成功返回实际写入的字节数，写入失败返回 -1 。
 */
static inline size_t rioWrite(rio *r, const void *buf, size_t len)
{
    while (len)
    {
        // 如果写入的len大于最大允许的㝍读的大小的话，就按照最大允许的读写尺寸进行读写操作。
        size_t bytes_to_write = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        // 检查函数指针 r->update_cksum 是否为NULL。
        if (r->update_cksum)
            r->update_cksum(r, buf, bytes_to_write);
        // write中的方法是 rio中定义的。
        // C 中的结构体和union中一般是没有方法的，因为他是面向过程的。
        // 这里通过rio的封装即定义，完成了面向对象的方法调用。【在结构体中定义方法。】
        if (r->write(r, buf, bytes_to_write) == 0)
            return 0;
        // 向后更改buf的指向。下次循环从新的位置开始写。
        buf = (char *)buf + bytes_to_write;
        // 写完就退出循环。
        len -= bytes_to_write;
        r->processed_bytes += bytes_to_write;
    }
    return 1;
}

/*
 * 从 r 中读取 len 字节，并将内容保存到 buf 中。
 *
 * 读取成功返回 1 ，失败返回 0 。
 */
static inline size_t rioRead(rio *r, void *buf, size_t len)
{
    while (len)
    {
        size_t bytes_to_read = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        if (r->read(r, buf, bytes_to_read) == 0)
            return 0;
        // 存在校验函数，就进行sum校验。
        if (r->update_cksum)
            r->update_cksum(r, buf, bytes_to_read);
        // 更改下次开始读的位置。
        buf = (char *)buf + bytes_to_read;
        // 读完就退出循环。
        len -= bytes_to_read;
        r->processed_bytes += bytes_to_read;
    }
    return 1;
}

/*
 * 返回 r 的当前偏移量。
 */
static inline off_t rioTell(rio *r)
{
    return r->tell(r);
}

void rioInitWithFile(rio *r, FILE *fp);
void rioInitWithBuffer(rio *r, sds s);
//
size_t rioWriteBulkCount(rio *r, char prefix, int count);
size_t rioWriteBulkString(rio *r, const char *buf, size_t len);
size_t rioWriteBulkLongLong(rio *r, long long l);
size_t rioWriteBulkDouble(rio *r, double d);

void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len);
void rioSetAutoSync(rio *r, off_t bytes);

#endif
