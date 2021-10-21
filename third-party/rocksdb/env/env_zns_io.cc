//  Copyright (c) 2019, Samsung Electronics.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
//  Written by Ivan L. Picoli <i.picoli@samsung.com>

#include <util/coding.h>
#include <string.h>
#include <sys/time.h>

#include <iostream>
#include <atomic>

#include "env/env_zns.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

namespace rocksdb {


/* ### SequentialFile method implementation ### */

Status ZNSSequentialFile::ReadOffset(uint64_t offset, size_t n, Slice* result,
                                    char* scratch, size_t* readLen) const {
    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
         env_zns->filesMutex.Unlock();
        return Status::OK();
    }

    int ret;
    size_t piece_off = 0, off, left, piecesize;
    unsigned i;
	uint64_t size;
    struct zrocks_map *map;

    if (ZNS_DEBUG_R)
	std::cout << __func__ << " name: " << filename_ << " offset: "
					    << offset << " size: " << n << std::endl;

    if (offset + n > env_zns->files[filename_]->size) {
        n = env_zns->files[filename_]->size - offset;
    }
    uint64_t disk_offset =  env_zns->files[filename_]->size - env_zns->files[filename_]->misalignsize;
    size_t bubffer_read_size = 0;
    size_t disk_read = n;
    size_t readsize = ZROCKS_MAX_READ_SZ - ZNS_ALIGMENT;

    // read from the buffer
    if (offset >=  disk_offset) {
        size_t boff = offset - disk_offset;
        bubffer_read_size = (boff + n) > env_zns->files[filename_]->misalignsize ?
                                             (env_zns->files[filename_]->misalignsize - boff) : n;
         memcpy(scratch, env_zns->files[filename_]->misalign + boff, bubffer_read_size);
         *result = Slice(scratch, bubffer_read_size);
         *readLen = bubffer_read_size;
         env_zns->filesMutex.Unlock();
         return Status::OK();
    }


    *readLen = n;
    // read from disk and buffer
    if (offset < disk_offset && (n  + offset) > disk_offset) {
        disk_read = disk_offset - offset;
        bubffer_read_size = n - disk_read;
    }
    /* Find the first piece of mapping */
    off = 0;
    for (i = 0; i < env_zns->files[filename_]->map.size(); i++) {
        map = &env_zns->files[filename_]->map.at(i);

        size = map->g.nsec * ZNS_ALIGMENT * 1UL;
        if (off + size > offset) {
        piece_off = size - ( off + size - offset);
        break;
    }

    off += size;
    }

    if (i == env_zns->files[filename_]->map.size()) {
        env_zns->filesMutex.Unlock();
        return Status::IOError();
    }

    /* Create one read per piece */
    left = disk_read;
    unsigned int mapsize = env_zns->files[filename_]->map.size();
    std::cout << "read offset file name " << filename_ << "map size " << mapsize << std::endl;
    while (left && i < mapsize) {

    map = &env_zns->files[filename_]->map.at(i);

    size = (size_t)map->g.nsec * ZNS_ALIGMENT;
    size = (size - piece_off > left) ? left : size - piece_off;
    off  = map->g.offset * ZNS_ALIGMENT;

    if (ZNS_DEBUG_R) std::cout << __func__ << "  map " << i << " piece(0x" <<
        std::hex << map->g.offset << "/" << std::dec << map->g.nsec <<
        ") read(" << off << ":" << piece_off << "/" << size << ")" <<
        " left " << left << std::endl;

    off += piece_off;

       piecesize = size;
       while (piecesize) {
            if (piecesize > ZROCKS_MAX_READ_SZ) {
                 ret = zrocks_read(off, scratch + (disk_read - left), readsize);
                 off += readsize;
                 left -= readsize;
                 piecesize = piecesize - readsize;
                if (ret) {
                    env_zns->filesMutex.Unlock();
                    return Status::IOError();
                }
            } else {
                 ret = zrocks_read(off, scratch + (disk_read - left), piecesize);
                 off +=piecesize;
                 left -= piecesize;
                 piecesize = 0;
                if (ret) {
                    env_zns->filesMutex.Unlock();
                    return Status::IOError();
                }
                break;
            }
       }

        piece_off = 0;
        i++;
    }


     if (bubffer_read_size) {
         memcpy(scratch + disk_read, env_zns->files[filename_]->misalign, bubffer_read_size);
    }
    *result = Slice(scratch, n);

    env_zns->filesMutex.Unlock();
    return Status::OK();
}

Status ZNSSequentialFile::Read(size_t n, Slice* result, char* scratch) {
    size_t readLen = 0;
    Status status = ReadOffset(read_off, n, result, scratch, &readLen);

    if (status.ok())
        read_off += readLen;

    return status;
}

Status ZNSSequentialFile::PositionedRead(uint64_t offset, size_t n,
					    Slice* result, char* scratch) {
    std::cout << "WARNING: " << __func__ << " offset: " << offset
						<< " n: " << n << std::endl;

    *result = Slice(scratch, n);

    return Status::OK();
}

Status ZNSSequentialFile::Skip(uint64_t n) {
    std::cout << "WARNING: " << __func__ << " n: " << n << std::endl;
    return Status::OK();
}

Status ZNSSequentialFile::InvalidateCache(size_t offset, size_t length) {
    if (ZNS_DEBUG) std::cout << __func__ << " offset: " << offset << " length: "
							<< length << std::endl;
    return Status::OK();
}


/* ### RandomAccessFile method implementation ### */

Status ZNSRandomAccessFile::ReadObj(uint64_t offset, size_t n, Slice* result,
							char* scratch) const {
    int ret;

    if (ZNS_DEBUG_R)
	std::cout << __func__ << "name: " << filename_ << " offset: "
					    << offset << " size: " << n << std::endl;

    ret = zrocks_read_obj(ztl_id, offset, scratch, n);
    if (ret) {
	std::cout << " ZRocks (read_obj) error: " << ret << std::endl;
	return Status::IOError();
    }

    *result = Slice(scratch, n);

    return Status::OK();
}

Status ZNSRandomAccessFile::ReadOffset(uint64_t offset, size_t n, Slice* result,
                                    char* scratch) const {
    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
         env_zns->filesMutex.Unlock();
        return Status::OK();
    }

    int ret;
	uint64_t size;
    size_t piece_off = 0, off, left, piecesize;
    unsigned i;
    struct zrocks_map *map;

    if (ZNS_DEBUG_R)
	std::cout << __func__ << " name: " << filename_ << " offset: "
					    << offset << " size: " << n << std::endl;

    if (offset + n > env_zns->files[filename_]->size) {
        n = env_zns->files[filename_]->size - offset;
    }
    uint64_t disk_offset =  env_zns->files[filename_]->size - env_zns->files[filename_]->misalignsize;
    size_t bubffer_read_size = 0;
    size_t disk_read = n;
    size_t readsize = ZROCKS_MAX_READ_SZ - ZNS_ALIGMENT;

    // read from the buffer
    if (offset >=  disk_offset) {
        size_t boff = offset - disk_offset;
        bubffer_read_size = (boff + n) > env_zns->files[filename_]->misalignsize ?
                                             (env_zns->files[filename_]->misalignsize - boff) : n;
         memcpy(scratch, env_zns->files[filename_]->misalign + boff, bubffer_read_size);
         *result = Slice(scratch, bubffer_read_size);
         env_zns->filesMutex.Unlock();
         return Status::OK();
    }

    // read from disk and buffer
    if (offset < disk_offset && (n  + offset) > disk_offset) {
        disk_read = disk_offset - offset;
        bubffer_read_size = n - disk_read;
    }
    /* Find the first piece of mapping */
    off = 0;
    for (i = 0; i < env_zns->files[filename_]->map.size(); i++) {
        map = &env_zns->files[filename_]->map.at(i);

        size = map->g.nsec * ZNS_ALIGMENT * 1UL;
        if (off + size > offset) {
        piece_off = size - ( off + size - offset);
        break;
    }

    off += size;
    }

    if (i == env_zns->files[filename_]->map.size()) {
        env_zns->filesMutex.Unlock();
        return Status::IOError();
    }

    /* Create one read per piece */
    left = disk_read;
    unsigned int mapsize = env_zns->files[filename_]->map.size();
    while (left && i < mapsize) {

    map = &env_zns->files[filename_]->map.at(i);

    size = map->g.nsec * ZNS_ALIGMENT * 1UL;
    size = (size - piece_off > left) ? left : size - piece_off;
    off  = map->g.offset * ZNS_ALIGMENT;

    if (ZNS_DEBUG_R) std::cout << __func__ << "  map " << i << " piece(0x" <<
        std::hex << map->g.offset << "/" << std::dec << map->g.nsec <<
        ") read(" << off << ":" << piece_off << "/" << size << ")" <<
        " left " << left << std::endl;

    off += piece_off;

       piecesize = size;
       while (piecesize) {
            if (piecesize > ZROCKS_MAX_READ_SZ) {
                 ret = zrocks_read(off, scratch + (disk_read - left), readsize);
                 off += readsize;
                 left -= readsize;
                 piecesize = piecesize - readsize;
                if (ret) {
                    env_zns->filesMutex.Unlock();
                    return Status::IOError();
                }
            } else {
                 ret = zrocks_read(off, scratch + (disk_read - left), piecesize);
                 off +=piecesize;
                 left -= piecesize;
                 piecesize = 0;
                if (ret) {
                    env_zns->filesMutex.Unlock();
                    return Status::IOError();
                }
                break;
            }
       }

        piece_off = 0;
        i++;
    }


     if (bubffer_read_size) {
         memcpy(scratch + disk_read, env_zns->files[filename_]->misalign, bubffer_read_size);
    }
    *result = Slice(scratch, n);


    env_zns->filesMutex.Unlock();
    return Status::OK();
}

Status ZNSRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result,
                                char* scratch) const {

#if ZNS_PREFETCH
    std::atomic_flag* flag = const_cast<std::atomic_flag*>(&prefetch_lock);

    while (flag->test_and_set(std::memory_order_acquire))
    {}

    if ( (prefetch_sz > 0) && (offset >= prefetch_off) &&
            (offset + n <= prefetch_off + prefetch_sz) ) {

    memcpy(scratch, prefetch + (offset - prefetch_off), n);
    flag->clear(std::memory_order_release);
    *result = Slice(scratch, n);

    return Status::OK();
    }

    flag->clear(std::memory_order_release);
#endif

#if ZNS_OBJ_STORE
    return ReadObj (offset, n, result, scratch);
#else
    return ReadOffset (offset, n, result, scratch);
#endif
}

Status ZNSRandomAccessFile::Prefetch(uint64_t offset, size_t n) {
    if (ZNS_DEBUG) std::cout << __func__ << " offset: " << offset
                    << " n: " << n << std::endl;
    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
        env_zns->filesMutex.Unlock();
        return Status::OK();
    }
    env_zns->filesMutex.Unlock();
#if ZNS_PREFETCH
    Slice result;
    Status st;
    std::atomic_flag* flag = const_cast<std::atomic_flag*>(&prefetch_lock);

    while (flag->test_and_set(std::memory_order_acquire))
    {}
    if (n > ZNS_PREFETCH_BUF_SZ) {
           n = ZNS_PREFETCH_BUF_SZ;
    }

#if ZNS_OBJ_STORE
    st = ReadObj(offset, n, &result, prefetch);
#else
    st = ReadOffset(offset, n, &result, prefetch);
#endif
    if (!st.ok()) {
        prefetch_sz = 0;
        flag->clear(std::memory_order_release);
        return Status::OK();
    }

    prefetch_sz = n;
    prefetch_off = offset;

    flag->clear(std::memory_order_release);
#endif /* ZNS_PREFETCH */

    return Status::OK();
}

size_t ZNSRandomAccessFile::GetUniqueId(char* id, size_t max_size) const {
    if (ZNS_DEBUG) std::cout << __func__ << std::endl;

    if (max_size < (kMaxVarint64Length * 3)) {
        return 0;
    }

    char* rid = id;
    uint64_t base = 0;
    // uint64_t base = ((uint64_t)this) + uuididx;
    if (!env_zns->files[filename_]) {
        std::cout << "the zns random file ptr is null" << "file name is " << filename_ << std::endl;
        base = (uint64_t)this;
    } else {
        base = ((uint64_t)env_zns->files[filename_]->uuididx);
    }

    rid = EncodeVarint64(rid, base);
    rid = EncodeVarint64(rid, base);
    rid = EncodeVarint64(rid, base);
    assert(rid >= id);

    return static_cast<size_t>(rid - id);
}

Status ZNSRandomAccessFile::InvalidateCache(size_t offset, size_t length) {
    if (ZNS_DEBUG) std::cout << __func__ << " offset: " << offset
                    << " length: " << length << std::endl;
    return Status::OK();
}


/* ### WritableFile method implementation ### */

Status ZNSWritableFile::Append(const Slice& data) {
    if (ZNS_DEBUG_AF) std::cout << __func__ << " size: " << data.size() << std::endl;

    size_t size = data.size();
    size_t offset = 0;
    Status s;

    // the reasons of ZNS_MAX_BUF - ZNS_ALIGMENT: because of alignment requirement,
    // there maybe some left misalign in the buffer after last sync
    // therefore, the buffer can't fit the ZNS_MAX_BUF
    size_t  cpytobuf_size = (ZNS_MAX_BUF - ZNS_ALIGMENT);

    if (!cache_off) {
        printf("Append is error *************** \n");
        return Status::IOError();
    }

    if (cache_off + data.size() > wcache + ZNS_MAX_BUF) {
        if (ZNS_DEBUG_AF)
	        std::cout << __func__ << " Maximum buffer size is ZNS_MAX_BUF(65536*ZNS_ALIGMENT)/(1024*1024) MB  data size is "
	        << data.size() << std::endl;
       s = Sync();
       if (!s.ok()) {
            return Status::IOError();
       }

       while (size > cpytobuf_size) {
            memcpy(cache_off, data.data() + offset , cpytobuf_size);
            offset = offset + cpytobuf_size;
            size = size - cpytobuf_size;
            s = Sync();
             if (!s.ok()) {
                return Status::IOError();
             }
       }
    }

    memcpy(cache_off, data.data() + offset, size);
    cache_off += size;
    filesize_ += data.size();

    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
        env_zns->filesMutex.Unlock();
         return Status::OK();
    }

    env_zns->files[filename_]->size += data.size();
    env_zns->filesMutex.Unlock();

    return Status::OK();
}

Status ZNSWritableFile::PositionedAppend(const Slice& data, uint64_t offset) {
    if (ZNS_DEBUG_AF) std::cout << __func__ << __func__ << " size: " << data.size()
                    << " offset: " << offset << std::endl;
    if (offset != filesize_) {
    std::cout << "Write Violation: " << __func__ << " size: " << data.size()
                << " offset: " << offset << std::endl;
    }

    return Append(data);
}

Status ZNSWritableFile::Truncate(uint64_t size) {
    if (ZNS_DEBUG_AF) std::cout << __func__ << " size: " << size << std::endl;

    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
        env_zns->filesMutex.Unlock();
        return Status::OK();
    }

    size_t cache_size = (size_t) (cache_off - wcache);
    size_t trun_size = env_zns->files[filename_]->size - size;

    if (cache_size < trun_size) {
        // TODO
        env_zns->filesMutex.Unlock();
        return Status::OK();
    }

    cache_off -= trun_size;
    filesize_ = size;
    env_zns->files[filename_]->size = size;

    env_zns->filesMutex.Unlock();
    return Status::OK();
}

Status ZNSWritableFile::Close() {
    if (ZNS_DEBUG) std::cout << __func__ << std::endl;
#if !ZNS_OBJ_STORE
    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
        env_zns->filesMutex.Unlock();
        return Status::OK();
    }

    env_zns->filesMutex.Unlock();
    Sync();

#endif
    return Status::OK();
}

Status ZNSWritableFile::Flush() {
    if (ZNS_DEBUG_AF) std::cout << __func__ << std::endl;
    return Status::OK();
}

Status ZNSWritableFile::Sync() {
    size_t size;
    uint32_t misalignsize;
    int ret;

#if !ZNS_OBJ_STORE
    struct zrocks_map *map;
    uint16_t pieces = 0;
    int i;
#endif

    if (!cache_off)
        return Status::OK();

    size = (size_t) (cache_off - wcache);
    misalignsize = size % ZNS_ALIGMENT;
    size = size - misalignsize;
    if (misalignsize)
        memcpy(env_zns->files[filename_]->misalign, wcache + size, misalignsize);
    if (ZNS_DEBUG_W) std::cout << __func__ << " flush size: " << size << std::endl;

    if (size == 0) {
         env_zns->files[filename_]->misalignsize =  misalignsize;
         cache_off = wcache + env_zns->files[filename_]->misalignsize;
        if (misalignsize) {
            if (ZNS_DEBUG_W)
                printf("the misaligned size is %d \n", misalignsize);
            memcpy(wcache, env_zns->files[filename_]->misalign,  misalignsize);
            env_zns->filesMutex.Lock();
            env_zns->FlushMetaData();
            env_zns->filesMutex.Unlock();
        }
        return Status::OK();
    }


#if ZNS_OBJ_STORE
    ret = zrocks_new(ztl_id, wcache, size, level);
#else
    env_zns->filesMutex.Lock();
    ret = zrocks_write(wcache, size, level, &map, &pieces);
    env_zns->filesMutex.Unlock();
    if (!ret)
    	map_off = map[pieces - 1].g.offset;
    // memcpy (cache_off, map, sizeof (struct zrocks_map) * pieces);
    // Make sure the mapping piece is accessible via MANIFEST
#endif

    if (ret) {
        std::cout << " ZRocks (write) error: " << ret << std::endl;
        return Status::IOError();
    }

#if !ZNS_OBJ_STORE
    if (ZNS_DEBUG_W)
        std::cout << __func__ << " DONE. pieces: " << pieces << std::endl;
    // write page
    env_zns->filesMutex.Lock();
    if (env_zns->files[filename_] == NULL) {
        env_zns->filesMutex.Unlock();
        zrocks_free(map);
        return Status::OK();
    }

    for (i = 0; i < pieces; i++) {
        env_zns->files[filename_]->map.push_back(map[i]);
        if (ZNS_DEBUG_W) {
            std::cout << __func__ << "  map 0x" << std::hex << i << std::dec
                << " off " << map[i].g.offset << " sz " << map[i].g.nsec
                << std::endl;
        }
    }

    env_zns->FlushMetaData();
    env_zns->filesMutex.Unlock();
    zrocks_free(map);
#endif


    env_zns->files[filename_]->misalignsize =  misalignsize;
    cache_off = wcache + env_zns->files[filename_]->misalignsize;
    if (misalignsize) {
        memcpy(wcache, env_zns->files[filename_]->misalign,  misalignsize);
    }
    return Status::OK();
}

Status ZNSWritableFile::Fsync() {
    if (ZNS_DEBUG_AF) std::cout << __func__ << std::endl;
    return Sync();
}

Status ZNSWritableFile::InvalidateCache(size_t offset, size_t length) {
    if (ZNS_DEBUG) std::cout << __func__ << " offset: " << offset
				    << " length: " << length << std::endl;
    return Status::OK();
}

#ifdef ROCKSDB_FALLOCATE_PRESENT
Status ZNSWritableFile::Allocate(uint64_t offset, uint64_t len) {
    if (ZNS_DEBUG) std::cout << __func__ << " offset: " << offset
					<< " len: " << len << std::endl;
    return Status::OK();
}
#endif

Status ZNSWritableFile::RangeSync(uint64_t offset, uint64_t nbytes) {
    std::cout << "WARNING: " << __func__ << " offset: " << offset
					<< " nbytes: " << nbytes << std::endl;
    return Status::OK();
}

size_t ZNSWritableFile::GetUniqueId(char* id, size_t max_size) const {
    if (ZNS_DEBUG) std::cout << __func__ << std::endl;

    if (max_size < (kMaxVarint64Length * 3)) {
	return 0;
    }

    char* rid = id;
    uint64_t base = 0;
     if (!env_zns->files[filename_]) {
        // std::cout << "the zns random file ptr is null" << "file name is " << filename_ << std::endl;
        base =(uint64_t) this;
    } else {
        base = ((uint64_t)env_zns->files[filename_]->uuididx);
    }
    // uint64_t base = env_zns->files[filename_]->uuididx;
    rid = EncodeVarint64(rid, (uint64_t)base);
    rid = EncodeVarint64(rid, (uint64_t)base);
    rid = EncodeVarint64(rid, (uint64_t)base);
    assert(rid >= id);

    return static_cast<size_t>(rid - id);
}

} // namespace rocksdb
