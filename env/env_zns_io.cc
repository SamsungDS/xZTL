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

#include "env_zns.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

namespace rocksdb {

/* ### SequentialFile method implementation ### */

Status ZNSSequentialFile::ReadOffset(uint64_t offset, size_t n, Slice* result,
                                     char* scratch, size_t* readLen) const {
  struct zrocks_map* map;
  size_t             piece_off = 0, left, size;
  int                ret;
  unsigned           i;
  uint64_t           off;

  if (znsfile == NULL || offset >= znsfile->size) {
    return Status::OK();
  }

  if (ZNS_DEBUG_R)
    std::cout << __func__ << " name: " << filename_ << " offset: " << offset
              << " size: " << n << " file_size:  " << znsfile->size
              << std::endl;

  if (offset + n > znsfile->size) {
    n = znsfile->size - offset;
  }
  // env_zns->filesMutex.Lock();
  std::vector<struct zrocks_map> temlist;
  temlist.assign(znsfile->map.begin(), znsfile->map.end());
  // env_zns->filesMutex.Unlock();

  off = 0;
  for (i = 0; i < temlist.size(); i++) {
    map = &temlist.at(i);
    size = map->g.num * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    if (off + size > offset) {
      piece_off = size - (off + size - offset);
      break;
    }
    off += size;
  }

  if (i == temlist.size()) {
    if (ZNS_DEBUG_R)
      std::cout << __func__ << " name: " << filename_ << " error: No fit map! "
                << std::endl;
    return Status::IOError();
  }

  /* Create one read per piece */
  left = n;
  while (left) {
    map          = &temlist.at(i);
    size         = map->g.num * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    size         = (size - piece_off > left) ? left : size - piece_off;
    uint64_t tmp = map->g.start;
    off          = tmp * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;

    if (ZNS_DEBUG_R)
      std::cout << __func__ << " name: " << filename_ << " map: " << i
                << " node: " << map->g.node_id << " start: " << map->g.start
                << " num: " << map->g.num << " left " << left << std::endl;

    off += piece_off;
    ret = zrocks_read(map->g.node_id, off, scratch + (n - left), size);
    if (ret) {
      return Status::IOError();
    }

    left -= size;
    piece_off = 0;
    i++;
  }

  *readLen = n;
  *result  = Slice(scratch, n);
  return Status::OK();
}

Status ZNSSequentialFile::Read(size_t n, Slice* result, char* scratch) {
  size_t readLen = 0;
  Status status  = ReadOffset(read_off, n, result, scratch, &readLen);

  if (status.ok())
    read_off += readLen;

  return status;
}

Status ZNSSequentialFile::PositionedRead(uint64_t offset, size_t n,
                                         Slice* result, char* scratch) {
  if (ZNS_DEBUG)
    std::cout << "WARNING: " << __func__ << " offset: " << offset << " n: " << n
              << std::endl;
  size_t readLen = 0;
  Status status  = ReadOffset(read_off, n, result, scratch, &readLen);

  *result = Slice(scratch, readLen);

  return Status::OK();
}

Status ZNSSequentialFile::Skip(uint64_t n) {
  if (ZNS_DEBUG)
    std::cout << "WARNING: " << __func__ << " n: " << n << std::endl;
  if (read_off + n <= znsfile->size) {
    read_off += n;
  }
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
  int ret = 0;

  if (ZNS_DEBUG_R) {
    std::cout << __func__ << "name: " << filename_ << " offset: " << offset
              << " size: " << n << std::endl;
  }

  ret = zrocks_read_obj(0, offset, scratch, n);
  if (ret) {
    std::cout << " ZRocks (read_obj) error: " << ret << std::endl;
    return Status::IOError();
  }

  *result = Slice(scratch, n);

  return Status::OK();
}

Status ZNSRandomAccessFile::ReadOffset(uint64_t offset, size_t n, Slice* result,
                                       char* scratch) const {
  struct zrocks_map* map;
  size_t             piece_off = 0, left, msize;
  int                ret;
  unsigned           i;
  uint64_t           off;

  if (znsfile == NULL || offset >= znsfile->size) {
    return Status::OK();
  }

  if (ZNS_DEBUG_R)
    std::cout << __func__ << " name: " << filename_ << " offset: " << offset
              << " size: " << n << " file_size:  " << znsfile->size
              << std::endl;

  if (offset + n > znsfile->size) {
    n = znsfile->size - offset;
  }

  // env_zns->filesMutex.Lock();
  std::vector<struct zrocks_map> temlist;
  temlist.assign(znsfile->map.begin(), znsfile->map.end());
  // env_zns->filesMutex.Unlock();

  off = 0;
  for (i = 0; i < temlist.size(); i++) {
    map   = &temlist.at(i);
    msize = map->g.num * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    if (off + msize > offset) {
      piece_off = msize - (off + msize - offset);
      break;
    }
    off += msize;
  }

  if (i == temlist.size()) {
    if (ZNS_DEBUG_R) {
      std::cout << __func__ << " name: " << filename_ << " error: No fit map! "
                << std::endl;
    }

    return Status::IOError();
  }

  /* Create one read per piece */
  left = n;
  while (left) {
    map          = &temlist.at(i);
    msize        = map->g.num * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    msize        = (msize - piece_off > left) ? left : msize - piece_off;
    uint64_t tmp = map->g.start;  // bug-fix
    off          = tmp * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;

    if (ZNS_DEBUG_R)
      std::cout << __func__ << " name: " << filename_ << " map: " << i
                << " node: " << map->g.node_id << " start: " << map->g.start
                << " num: " << map->g.num << " piece_off: " << piece_off
                << " left " << left << " readoff: " << off
                << " readsize: " << msize << std::endl;

    off += piece_off;
    ret = zrocks_read(map->g.node_id, off, scratch + (n - left), msize);
    if (ret) {
      return Status::IOError();
    }

    left -= msize;
    piece_off = 0;
    i++;
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
  Slice             result;
  Status            st;
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

  prefetch_sz  = n;
  prefetch_off = offset;

  flag->clear(std::memory_order_release);
#endif /* ZNS_PREFETCH */

  return Status::OK();
}

size_t ZNSRandomAccessFile::GetUniqueId(char* id, size_t max_size) const {
  if (ZNS_DEBUG)
    std::cout << __func__ << std::endl;

  if (max_size < (kMaxVarint64Length * 3)) {
    return 0;
  }

  char*    rid        = id;
  uint64_t base       = 0;
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

  rid = EncodeVarint64(rid, (uint64_t)znsFilePtr);
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
Status ZNSWritableFile::Append(const rocksdb::Slice&,
                               const rocksdb::DataVerificationInfo&) {
  return Status::OK();
}

Status ZNSWritableFile::PositionedAppend(const rocksdb::Slice&, uint64_t,
                                         const rocksdb::DataVerificationInfo&) {
  return Status::OK();
}
Status ZNSWritableFile::Append(const Slice& data) {
  if (ZNS_DEBUG_AF)
    std::cout << __func__ << filename_ << " size: " << data.size() << std::endl;

  size_t size   = data.size();
  size_t offset = 0;
  Status s;

  if (!cache_off) {
    std::cout << __func__ << filename_ << " failed : cache is NULL." << std::endl;
    return Status::IOError();
  }

  size_t cpytobuf_size = ZNS_MAX_BUF;
  if (cache_off + data.size() > wcache + ZNS_MAX_BUF) {
    if (ZNS_DEBUG_AF)
      std::cout << __func__
                << " Maximum buffer size is "
                   "ZNS_MAX_BUF(65536*ZNS_ALIGMENT)/(1024*1024) MB  data "
                   "size is "
                << data.size() << std::endl;

    size = cpytobuf_size - (cache_off - wcache);
    memcpy(cache_off, data.data(), size);
    cache_off += size;
    s = Sync();
    if (!s.ok()) {
      return Status::IOError();
    }

    offset = size;
    size   = data.size() - size;

    while (size > cpytobuf_size) {
      memcpy(cache_off, data.data() + offset, cpytobuf_size);
      offset = offset + cpytobuf_size;
      size   = size - cpytobuf_size;
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
  if (ZNS_DEBUG_AF)
    std::cout << __func__ << filename_ << " size: " << size << std::endl;

  env_zns->filesMutex.Lock();
  if (env_zns->files[filename_] == NULL) {
    env_zns->filesMutex.Unlock();
    return Status::OK();
  }
  env_zns->filesMutex.Unlock();

  size_t cache_size = (size_t)(cache_off - wcache);
  size_t trun_size  = znsfile->size - size;

  if (cache_size < trun_size) {
    // TODO
    // env_zns->filesMutex.Unlock();
    return Status::OK();
  }

  cache_off -= trun_size;
  filesize_     = size;
  znsfile->size = size;

  // env_zns->filesMutex.Unlock();
  return Status::OK();
}

Status ZNSWritableFile::Close() {
  if (ZNS_DEBUG) {
    std::cout << __func__ << " file: " << filename_ << "level " << znsfile->level << std::endl;
  }

  Sync();
  return Status::OK();
}

Status ZNSWritableFile::Flush() {
  if (ZNS_DEBUG_AF) {
    std::cout << __func__ << std::endl;
  }

  return Status::OK();
}

Status ZNSWritableFile::Sync() {
  struct zrocks_map maps[2];
  uint16_t          pieces = 0;
  size_t            size;
  int               ret, i;

  if (!cache_off)
    return Status::OK();

  size = (size_t)(cache_off - wcache);
  if (!size)
    return Status::OK();

#if ZNS_OBJ_STORE
  ret = zrocks_new(ztl_id, wcache, size, znsfile->level);
#else
  ret = zrocks_write(wcache, size, znsfile->level, maps, &pieces);
#endif

  if (ret) {
    std::cout << __func__ << " file: " << filename_
              << " ZRocks (write) error: " << ret << std::endl;
    return Status::IOError();
  }

  if (ZNS_DEBUG_W) {
    std::cout << __func__ << " file: " << filename_ << " size: " << size
              << " level: " << znsfile->level << std::endl;
  }

#if !ZNS_OBJ_STORE
  // write page
  env_zns->filesMutex.Lock();
  if (env_zns->files[filename_] == NULL) {
    env_zns->filesMutex.Unlock();
    return Status::OK();
  }
  env_zns->filesMutex.Unlock();

  for (i = 0; i < pieces; i++) {
    znsfile->map.push_back(maps[i]);
    if (ZNS_DEBUG_W) {
      std::cout << __func__ << " file: " << filename_
                << " DONE. node_id: " << maps[i].g.node_id
                << " start: " << maps[i].g.start << " len: " << maps[i].g.num
                << std::endl;
    }
  }

  env_zns->FlushUpdateMetaData(znsfile);
  // env_zns->filesMutex.Unlock();
#endif

  cache_off = wcache;
  return Status::OK();
}

Status ZNSWritableFile::Fsync() {
  if (ZNS_DEBUG_AF)
    std::cout << __func__ << filename_ << std::endl;
  return Sync();
}

Status ZNSWritableFile::InvalidateCache(size_t offset, size_t length) {
  if (ZNS_DEBUG)
    std::cout << __func__ << " offset: " << offset << " length: " << length
              << std::endl;
  return Status::OK();
}

void ZNSWritableFile::SetWriteLifeTimeHint(Env::WriteLifeTimeHint hint) {
  if (ZNS_DEBUG)
    std::cout << __func__ << " : filename_" << filename_ << " hint: " << hint << std::endl;
  write_hint_ = hint;
  znsfile->level = hint - Env::WLTH_SHORT;
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
  if (ZNS_DEBUG)
    std::cout << "WARNING: " << __func__ << " offset: " << offset
              << " nbytes: " << nbytes << std::endl;
  return Status::OK();
}

size_t ZNSWritableFile::GetUniqueId(char* id, size_t max_size) const {
  if (ZNS_DEBUG)
    std::cout << __func__ << std::endl;

  if (max_size < (kMaxVarint64Length * 3)) {
    return 0;
  }

  char*    rid  = id;
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
