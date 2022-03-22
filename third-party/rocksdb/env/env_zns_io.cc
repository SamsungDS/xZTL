//  Copyright (c) 2019, Samsung Electronics.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
//  Written by Ivan L. Picoli <i.picoli@samsung.com>

#include <string.h>
#include <sys/time.h>
#include <util/coding.h>

#include <atomic>
#include <iostream>

#include "env/env_zns.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

namespace rocksdb {

/* ### SequentialFile method implementation ### */

Status ZNSSequentialFile::ReadOffset(uint64_t offset, size_t n, Slice* result,
                                     char* scratch, size_t* readLen) const {
    if (znsfile == NULL || offset >= znsfile->size) {
        return Status::OK();
    }


    int ret = 0;
    uint64_t node_size = zrocks_get_node_size();
    int zoneid1 = -1, zoneid2 = -1; 
    int tid = env_zns->updateMediaResource();
    if (ZNS_DEBUG_R) {
        printf("ZNSSequentialFile ReadOffset filename[%s] offset[%lu] size[%lu] filesize[%lu] tid[%lu]\n",
             filename_.c_str(), offset, n, znsfile->size, env_zns->gettid());
	}
    uint64_t zone_inner_offset = offset % node_size;
    int startzidx = offset / node_size;
    if (znsfile->nodes.empty()) {
        return Status::OK();
    }
    zoneid1 = znsfile->nodes[startzidx];
    if(zone_inner_offset + n >= node_size) {
        zoneid2 = znsfile->nodes[startzidx + 1];
    }

    if (zoneid2 == -1) {
        ret = zrocks_read(zoneid1, zone_inner_offset, scratch, n, tid);
        if (ret) {
            std::cout << __func__ << filename_ << " read error " << ret << " at zone node " 
                            << zoneid1 << std::endl;
            return Status::IOError();
        }
    } else {
        uint64_t z1readsize =  node_size - zone_inner_offset;
        ret = zrocks_read(zoneid1, zone_inner_offset, scratch, z1readsize, tid);
        if (ret) {
            std::cout << __func__ << filename_ << " read error " << ret << " at zone node " 
                            << zoneid1 << std::endl;
            return Status::IOError();
        }

        ret = zrocks_read(zoneid2, 0, scratch + z1readsize, n - z1readsize, tid);
        if (ret) {
            std::cout << __func__ << filename_ << " read error " << ret << " at zone node " 
                            << zoneid2 << std::endl;
            return Status::IOError();
        }
    }
  *readLen = n;
  *result = Slice(scratch, n);
  return Status::OK();
}

Status ZNSSequentialFile::Read(size_t n, Slice* result, char* scratch) {
  size_t readLen = 0;
  Status status = ReadOffset(read_off, n, result, scratch, &readLen);

  if (status.ok()) read_off += readLen;

  return status;
}

Status ZNSSequentialFile::PositionedRead(uint64_t offset, size_t n,
                                         Slice* result, char* scratch) {
  std::cout << "WARNING: " << __func__ << " offset: " << offset << " n: " << n
            << std::endl;

  *result = Slice(scratch, n);

  return Status::OK();
}

Status ZNSSequentialFile::Skip(uint64_t n) {
  std::cout << "WARNING: " << __func__ << " n: " << n << std::endl;
  return Status::OK();
}

Status ZNSSequentialFile::InvalidateCache(size_t offset, size_t length) {
  if (ZNS_DEBUG) {
    std::cout << __func__ << " offset: " << offset << " length: " << length
              << std::endl;
  }
  return Status::OK();
}

/* ### RandomAccessFile method implementation ### */

Status ZNSRandomAccessFile::ReadObj(uint64_t offset, size_t n, Slice* result,
                                    char* scratch) const {
  int ret;

  if (ZNS_DEBUG_R) {
    std::cout << __func__ << "name: " << filename_ << " offset: " << offset
              << " size: " << n << std::endl;
  }
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
    if (znsfile == NULL || offset >= znsfile->size) {
        return Status::OK();
    }

    int ret = 0;
    uint64_t node_size = zrocks_get_node_size();
    int zoneid1 = -1, zoneid2 = -1; 
    int tid = env_zns->updateMediaResource();

    if (ZNS_DEBUG_R) {
        printf(" ZNSRandomAccessFile ReadOffset filename[%s] offset[%lu] size[%lu] filesize[%lu] tid[%lu]\n",
             filename_.c_str(), offset, n, znsfile->size, env_zns->gettid());
    }

    uint64_t zone_inner_offset = offset % node_size;
    int startzidx = offset / node_size;
    if (znsfile->nodes.empty()) {
        return Status::OK();
    }

    zoneid1 = znsfile->nodes[startzidx];
    if (zone_inner_offset + n >= node_size) {
        zoneid2 = znsfile->nodes[startzidx + 1];
    }

    if (zoneid2 == -1) {
        ret = zrocks_read(zoneid1, zone_inner_offset, scratch, n, tid);
        if (ret) {
            std::cout << __func__ << filename_ << " read error " << ret << " at zone node " 
                            << znode_id << std::endl;
            return Status::IOError();
        }
    } else {
        uint64_t z1readsize =  node_size - zone_inner_offset;
        ret = zrocks_read(zoneid1, zone_inner_offset, scratch, z1readsize, tid);
        if (ret) {
            std::cout << __func__ << filename_ << " read error " << ret << " at zone node " 
                            << znode_id << std::endl;
            return Status::IOError();
        }
        //the seconde zone  start offset should be 0 
        ret = zrocks_read(zoneid2, 0, scratch + z1readsize, n - z1readsize, tid);
        if (ret) {
            std::cout << __func__ << filename_ << " read error " << ret << " at zone node " 
                            << znode_id << std::endl;
            return Status::IOError();
        }
    }
  *result = Slice(scratch, n);
  return Status::OK();
}

Status ZNSRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result,
                                 char* scratch) const {
#if ZNS_PREFETCH
  std::atomic_flag* flag = const_cast<std::atomic_flag*>(&prefetch_lock);

  while (flag->test_and_set(std::memory_order_acquire)) {
  }

  if ((prefetch_sz > 0) && (offset >= prefetch_off) &&
      (offset + n <= prefetch_off + prefetch_sz)) {
    memcpy(scratch, prefetch + (offset - prefetch_off), n);
    flag->clear(std::memory_order_release);
    *result = Slice(scratch, n);

    return Status::OK();
  }

  flag->clear(std::memory_order_release);
#endif

#if ZNS_OBJ_STORE
  return ReadObj(offset, n, result, scratch);
#else
  return ReadOffset(offset, n, result, scratch);
#endif
}

Status ZNSRandomAccessFile::Prefetch(uint64_t offset, size_t n) {
  if (ZNS_DEBUG) {
    std::cout << __func__ << " offset: " << offset << " n: " << n << std::endl;
  }

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

  while (flag->test_and_set(std::memory_order_acquire)) {
  }
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
  ZNSFile* znsFilePtr = NULL;

  env_zns->filesMutex.Lock();
  znsFilePtr = env_zns->files[filename_];
  env_zns->filesMutex.Unlock();

  if (!znsFilePtr) {
    std::cout << "the zns random file ptr is null"
              << "file name is " << filename_ << std::endl;
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
  if (ZNS_DEBUG) {
    std::cout << __func__ << " offset: " << offset << " length: " << length
              << std::endl;
  }

  return Status::OK();
}

/* ### WritableFile method implementation ### */
Status ZNSWritableFile::Append(const rocksdb::Slice&, const rocksdb::DataVerificationInfo&)
{
   return Status::OK();
}

Status ZNSWritableFile::PositionedAppend(const rocksdb::Slice&, uint64_t, const rocksdb::DataVerificationInfo&)
{
   return Status::OK();
}
Status ZNSWritableFile::Append(const Slice& data) {
  if (ZNS_DEBUG_AF)
    std::cout << __func__ << " size: " << data.size() << std::endl;

  size_t size = data.size();
  size_t offset = 0;
  Status s;

  if (!cache_off) {
    printf("ZNSWritableFile:Append is failed.\n");
    return Status::IOError();
  }

  size_t cpytobuf_size = ZNS_MAX_BUF;
  if (cache_off + data.size() > wcache + ZNS_MAX_BUF) {
    if (ZNS_DEBUG_AF)
      std::cout
          << __func__
          << " Maximum buffer size is "
             "ZNS_MAX_BUF(65536*ZNS_ALIGMENT)/(1024*1024) MB  data size is "
          << data.size() << std::endl;

    size = cpytobuf_size - (cache_off - wcache);
    memcpy(cache_off, data.data(), size);
    cache_off += size;
    s = Sync();
    if (!s.ok()) {
      return Status::IOError();
    }

    offset = size;
    size = data.size() - size;

    while (size > cpytobuf_size) {
      memcpy(cache_off, data.data() + offset, cpytobuf_size);
      offset = offset + cpytobuf_size;
      size = size - cpytobuf_size;
      cache_off += cpytobuf_size;
      s = Sync();
      if (!s.ok()) {
        return Status::IOError();
      }
    }
  }

  memcpy(cache_off, data.data() + offset, size);
  cache_off += size;
  filesize_ += data.size();


  znsfile->size += data.size();

  return Status::OK();
}

Status ZNSWritableFile::PositionedAppend(const Slice& data, uint64_t offset) {
  if (ZNS_DEBUG_AF) {
    std::cout << __func__ << __func__ << " size: " << data.size()
              << " offset: " << offset << std::endl;
  }

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

  size_t cache_size = (size_t)(cache_off - wcache);
  size_t trun_size = znsfile->size - size;

  if (cache_size < trun_size) {
    // TODO
    env_zns->filesMutex.Unlock();
    return Status::OK();
  }

  cache_off -= trun_size;
  filesize_ = size;
  znsfile->size = size;

  env_zns->filesMutex.Unlock();
  return Status::OK();
}

Status ZNSWritableFile::Close() {
  if (ZNS_DEBUG) {
      std::cout << __func__ << " file: " << filename_ << std::endl;
  }

  Sync();

  std::int32_t node_id = -1;    
  for(uint32_t i = 0; i < znsfile->nodes.size(); i++) {
    node_id = znsfile->nodes[i];
    if (node_id != -1) {
      zrocks_node_finish(node_id);
    }
  }
  return Status::OK();
}

Status ZNSWritableFile::Flush() {
  if (ZNS_DEBUG_AF) {
      std::cout << __func__ << std::endl;
  }

  return Status::OK();
}

Status ZNSWritableFile::Sync() {
  size_t size;
  int ret, tid;

  if (!cache_off) return Status::OK();

  size = (size_t)(cache_off - wcache);
  if (!size) return Status::OK();

#if ZNS_OBJ_STORE
    ret = zrocks_new(ztl_id, wcache, size, level);
#else
    int32_t node_id = znsfile->znode_id;
    if(node_id != -1 && zrocks_node_is_full(node_id)) {
        node_id = -1;
    }

    tid = env_zns->updateMediaResource();
    ret = zrocks_write(wcache, size, &node_id, tid);
#endif

  if (ret) {
    std::cout << " ZRocks (write) error: " << ret << std::endl;
    return Status::IOError();
  }

  if(node_id != znsfile->znode_id) {
      znsfile->nodes.push_back(node_id);
      znsfile->znode_id = node_id;
  }
#if !ZNS_OBJ_STORE
  if (ZNS_DEBUG_W) {
    std::cout << __func__
              << " DONE. node_id: " << env_zns->files[filename_]->znode_id
              << std::endl;
  }

  // write page
  env_zns->filesMutex.Lock();
  if (env_zns->files[filename_] == NULL) {
    env_zns->filesMutex.Unlock();
    return Status::OK();
  }

  env_zns->FlushMetaData();
  env_zns->filesMutex.Unlock();
#endif

  cache_off = wcache;
  return Status::OK();
}

Status ZNSWritableFile::Fsync() {
  if (ZNS_DEBUG_AF) std::cout << __func__ << std::endl;
  return Sync();
}

Status ZNSWritableFile::InvalidateCache(size_t offset, size_t length) {
  if (ZNS_DEBUG)
    std::cout << __func__ << " offset: " << offset << " length: " << length
              << std::endl;
  return Status::OK();
}

#ifdef ROCKSDB_FALLOCATE_PRESENT
Status ZNSWritableFile::Allocate(uint64_t offset, uint64_t len) {
  if (ZNS_DEBUG)
    std::cout << __func__ << " offset: " << offset << " len: " << len
              << std::endl;
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
  env_zns->filesMutex.Lock();
  if (!env_zns->files[filename_]) {
    base = (uint64_t)this;
  } else {
    base = ((uint64_t)znsfile->uuididx);
  }
  env_zns->filesMutex.Unlock();
  rid = EncodeVarint64(rid, (uint64_t)base);
  rid = EncodeVarint64(rid, (uint64_t)base);
  rid = EncodeVarint64(rid, (uint64_t)base);
  assert(rid >= id);

  return static_cast<size_t>(rid - id);
}

}  // namespace rocksdb
