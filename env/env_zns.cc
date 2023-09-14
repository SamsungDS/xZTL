//  Copyright (c) 2019, Samsung Electronics.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
//  Written by Ivan L. Picoli <i.picoli@samsung.com>

#include <sys/time.h>
#include <iostream>
#include <memory>
// #include "util/crc32c.h"
#include "env_zns.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/file_system.h"
#include "file/filename.h"

#define FILE_METADATA_BUF_SIZE (80 * 1024 * 1024)
#define FLUSH_INTERVAL         (60 * 60)
#define SLEEP_TIME             5
#define MAX_META_ZONE          2
#define MD_WRITE_FULL          0x1d
#define GC_DETECTION_TIME      (1000 * 1000 * 10)

enum Operation { Base, Update, Replace, Delete, GCChange };

namespace rocksdb {

std::uint32_t ZNSFile::GetFileMetaLen() {
  uint32_t metaLen = sizeof(ZrocksFileMeta);
  metaLen += map.size() * sizeof(struct zrocks_map);
  return metaLen;
}

std::uint32_t ZNSFile::WriteMetaToBuf(unsigned char* buf, bool update) {
  // reserved single file head
  std::uint32_t length = sizeof(ZrocksFileMeta);

  ZrocksFileMeta fileMetaData;
  fileMetaData.filesize = size;
  fileMetaData.level    = level;
  fileMetaData.pieceNum = map.size();
  std::uint32_t i       = 0;
  if (update) {
    i                     = startIndex;
    fileMetaData.pieceNum = map.size() - startIndex;
  }

  memcpy(fileMetaData.filename, name.c_str(), name.length());
  memcpy(buf, &fileMetaData, sizeof(ZrocksFileMeta));
  for (; i < map.size(); i++) {
    memcpy(buf + length, &map[i], sizeof(struct zrocks_map));
    length += sizeof(struct zrocks_map);
  }

  startIndex = map.size();

  return length;
}

void ZNSFile::PrintMetaData() {
  std::cout << __func__ << " FileName: " << name << " level: " << level
            << " size: " << size << std::endl;
  for (uint32_t i = 0; i < map.size(); i++) {
    struct zrocks_map& pInfo = map[i];
    std::cout << " nodeId: " << pInfo.g.node_id << " start: " << pInfo.g.start
              << " num: " << pInfo.g.num << std::endl;
  }
}

void ZNSFile::GetWRLock() {
  fMutex.lock();
  is_writing = true;
}

bool ZNSFile::TryGetWRLock() {
  if (!fMutex.try_lock()) return false;
  is_writing = true;
  return true;
}

void ZNSFile::ReleaseWRLock() {
  assert(is_writing);
  is_writing = false;
  fMutex.unlock();
}

bool ZNSFile::IsWR() { return is_writing; }


/* ### ZNS Environment method implementation ###
void ZNSEnv::NodeSta(std::int32_t znode_id, size_t n) {
  double seconds;

  if (start_ns == 0) {
    GET_NANOSECONDS(start_ns, ts_s);
  }

  read_bytes[znode_id] += n;

  GET_NANOSECONDS(end_ns, ts_e);
  seconds      = (double)(end_ns - start_ns) / (double)1000000000;  // NOLINT
  int totalcnt = 0, readcnt = 0;
  if (seconds >= 2) {
    for (int i = 0; i < ZNS_MAX_NODE_NUM; i++) {
      if (alloc_flag[i]) {
        totalcnt++;
      }
    }

    for (int i = 0; i < ZNS_MAX_NODE_NUM; i++) {
      if (read_bytes[i]) {
        readcnt++;
        read_bytes[i] = 0;
      }
    }

    GET_NANOSECONDS(start_ns, ts_s);
  }
} */

Status ZNSEnv::NewSequentialFile(const std::string&               fname,
                                 std::unique_ptr<SequentialFile>* result,
                                 const EnvOptions&                options) {
  std::string nfname = NormalizePath(fname);
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << std::endl;

  if (IsFilePosix(nfname) || files[nfname] == NULL) {
    return posixEnv->NewSequentialFile(nfname, result, options);
  }

  result->reset();

  ZNSSequentialFile* f = new ZNSSequentialFile(nfname, this, options);
  result->reset(dynamic_cast<SequentialFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewRandomAccessFile(const std::string&                 fname,
                                   std::unique_ptr<RandomAccessFile>* result,
                                   const EnvOptions&                  options) {
  std::string nfname = NormalizePath(fname);
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << std::endl;

  if (IsFilePosix(nfname)) {
    return posixEnv->NewRandomAccessFile(nfname, result, options);
  }

  ZNSRandomAccessFile* f = new ZNSRandomAccessFile(nfname, this, options);
  result->reset(dynamic_cast<RandomAccessFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewWritableFile(const std::string&             fname,
                               std::unique_ptr<WritableFile>* result,
                               const EnvOptions&              options) {
  std::string nfname = NormalizePath(fname);
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << std::endl;

  uint32_t fileNum = 0;

  if (IsFilePosix(nfname)) {
    return posixEnv->NewWritableFile(nfname, result, options);
  } else {
    posixEnv->NewWritableFile(nfname, result, options);
  }

  filesMutex.Lock();
  fileNum = files.count(nfname);
  if (fileNum != 0) {
    delete files[nfname];
    files.erase(nfname);
  }

  files[nfname]          = new ZNSFile(nfname, 0);
  files[nfname]->uuididx = uuididx++;

  ZNSWritableFile* f = new ZNSWritableFile(nfname, this, options);
  result->reset(dynamic_cast<WritableFile*>(f));
  filesMutex.Unlock();

  return Status::OK();
}


Status ZNSEnv::DeleteFile(const std::string& fname) {
  std::string nfname = NormalizePath(fname);
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << std::endl;

  unsigned           i;
  struct zrocks_map* map;

  if (IsFilePosix(nfname)) {
    return posixEnv->DeleteFile(nfname);
  }
  posixEnv->DeleteFile(nfname);

  filesMutex.Lock();
  if (files.find(nfname) == files.end() || files[nfname] == NULL) {
    filesMutex.Unlock();
    return Status::OK();
  }

  ZNSFile* znsfile = files[nfname];
  for (i = 0; i < znsfile->map.size(); i++) {
    map = &files[nfname]->map.at(i);
    zrocks_trim(map, false);
  }

  delete files[nfname];
  files.erase(nfname);

  FlushDelMetaData(nfname);
  filesMutex.Unlock();
  return Status::OK();
}

Status ZNSEnv::GetFileSize(const std::string& fname, std::uint64_t* size) {
  std::string nfname = NormalizePath(fname);
  if (IsFilePosix(nfname)) {
    if (ZNS_DEBUG)
      std::cout << __func__ << ":" << nfname << std::endl;
    return posixEnv->GetFileSize(nfname, size);
  }

  filesMutex.Lock();
  if (files.find(nfname) == files.end() || files[nfname] == NULL) {
    filesMutex.Unlock();
    return Status::OK();
  }

  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << "size: " << files[nfname]->size
              << std::endl;

  *size = files[nfname]->size;
  filesMutex.Unlock();

  return Status::OK();
}

Status ZNSEnv::GetFileModificationTime(const std::string& fname,
                                       std::uint64_t*     file_mtime) {
  std::string nfname = NormalizePath(fname);
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << std::endl;

  if (IsFilePosix(nfname)) {
    return posixEnv->GetFileModificationTime(nfname, file_mtime);
  }

  /* TODO: Get SST files modification time from ZNS */
  *file_mtime = 0;

  return Status::OK();
}

Status ZNSEnv::RenameFile(const std::string& src,
                    const std::string& target) {
  std::string nsrc = NormalizePath(src);
  std::string ntarget = NormalizePath(target);
  if (ZNS_DEBUG) {
    std::cout << __func__ << ": src " << nsrc << " target: " << ntarget << std::endl;
  }

  if (IsFilePosix(nsrc)) {
    return posixEnv->RenameFile(nsrc, ntarget);
  }

  posixEnv->RenameFile(nsrc, ntarget);
  filesMutex.Lock();
  if (files.find(nsrc) == files.end() || files[nsrc] == NULL) {
    filesMutex.Unlock();
    return Status::OK();
  }

  ZNSFile* zns = files[nsrc];
  zns->name = ntarget;
  files[ntarget] = zns;
  files.erase(nsrc);

  FlushReplaceMetaData(nsrc, ntarget);
  filesMutex.Unlock();

  return Status::OK();
}

Status ZNSEnv::FileExists(const std::string& fname) {
  std::string nfname = NormalizePath(fname);
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << nfname << std::endl;
  // return posixEnv->FileExists(fname); // for percona current not find

  filesMutex.Lock();
  if (files.find(nfname) != files.end() && files[nfname] != nullptr) {
    filesMutex.Unlock();
    return Status::OK();
  }

  filesMutex.Unlock();
  return posixEnv->FileExists(nfname);
}

Status ZNSEnv::FlushMetaData() {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  memset(metaBuf, 0, FILE_METADATA_BUF_SIZE);
  // reserved head position
  std::uint32_t fileNum = 0;

  MetaZoneHead mzHead;
  mzHead.info.sequence = sequence++;
  memcpy(metaBuf, &mzHead, sizeof(MetaZoneHead));
  int dataLen = sizeof(MetaZoneHead) + sizeof(MetadataHead) + sizeof(fileNum);
  if (ZNS_DEBUG_META)
    std::cout << __func__ << " Start FlushMetaData " << std::endl;

  std::map<std::string, ZNSFile*>::iterator itermap = files.begin();
  for (; itermap != files.end(); ++itermap) {
    ZNSFile* zfile = itermap->second;
    if (zfile == NULL) {
      continue;
    }

    fileNum++;
    if (ZNS_DEBUG_META)
      zfile->PrintMetaData();
    if (dataLen + zfile->GetFileMetaLen() >= FILE_METADATA_BUF_SIZE) {
      std::cout << __func__ << ": buf over flow" << std::endl;
      return Status::MemoryLimit();
    }

    int length = zfile->WriteMetaToBuf(metaBuf + dataLen);
    dataLen += length;
  }

  memcpy(metaBuf + sizeof(struct MetaZoneHead) + sizeof(MetadataHead), &fileNum,
         sizeof(fileNum));

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.dataLength = dataLen - sizeof(MetadataHead) - sizeof(MetaZoneHead);
  metadataHead.tag        = Base;

  memcpy(metaBuf + sizeof(MetaZoneHead), &metadataHead, sizeof(MetadataHead));
  int ret = zrocks_write_file_metadata(metaBuf, dataLen);
  if (ret == MD_WRITE_FULL) {
    std::cout << __func__ << ": zrocks_write_metadata Full " << ret << std::endl;
    ret = zrocks_write_file_metadata(metaBuf, dataLen);
  } else if (ret != 0) {
     std::cout << __func__ << ": zrocks_write_metadata error " << ret << std::endl;
  }

  if (ZNS_DEBUG_META)
    std::cout << __func__ << " End FlushMetaData " << std::endl;

  return Status::OK();
}

Status ZNSEnv::FlushUpdateMetaData(ZNSFile* zfile) {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  if (zfile->startIndex == zfile->map.size()) {
    return Status::OK();
  }
  unsigned char* buf = NULL;
  buf = reinterpret_cast<unsigned char*>(zrocks_alloc(ZNS_ALIGMENT));
  if (buf == NULL) {
    return Status::MemoryLimit();
  }

  memset(buf, 0, ZNS_ALIGMENT);

  // reserved head position
  int dataLen = sizeof(MetadataHead);
  if (ZNS_DEBUG_META) {
    std::cout << __func__ << " Start FlushUpdateMetaData " << zfile->name
              << " level: " << zfile->level << " size: " << zfile->size
              << std::endl;
    for (std::uint32_t i = zfile->startIndex; i < zfile->map.size(); i++) {
      struct zrocks_map& pInfo = zfile->map[i];
      std::cout << " nodeId: " << pInfo.g.node_id << " start: " << pInfo.g.start
                << " num: " << pInfo.g.num << std::endl;
    }
  }

  int length = zfile->WriteMetaToBuf(buf + dataLen, true);
  dataLen += length;

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.tag        = Update;
  metadataHead.dataLength = dataLen - sizeof(MetadataHead);

  /* std::uint32_t crc = 0;
  crc = crc32c::Extend(crc, (const char*)buf + sizeof(MetadataHead),
                       metadataHead.dataLength);
  crc = crc32c::Mask(crc);
  metadataHead.crc = crc; */

  memcpy(buf, &metadataHead, sizeof(MetadataHead));
  metaMutex.Lock();
  int ret = zrocks_write_file_metadata(buf, dataLen);
  if (ret == MD_WRITE_FULL) {
    std::cout << __func__ << ": zrocks_write_metadata FULL " << ret
              << std::endl;
    FlushMetaData();
  } else if (ret != 0) {
    std::cout << __func__ << ": zrocks_write_metadata error ret " << ret
              << std::endl;
  }
  metaMutex.Unlock();

  if (ZNS_DEBUG_META)
    std::cout << __func__ << " End FlushUpdateMetaData " << std::endl;
  zrocks_free(buf);

  return Status::OK();
}

Status ZNSEnv::FlushGCChangeMetaData(ZNSFile* zfile) {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  unsigned char* buf = NULL;
  buf = reinterpret_cast<unsigned char*>(zrocks_alloc(ZNS_ALIGMENT));
  if (buf == NULL) {
    return Status::MemoryLimit();
  }

  memset(buf, 0, ZNS_ALIGMENT);

  // reserved head position
  int dataLen = sizeof(MetadataHead);
  if (ZNS_DEBUG_META) {
    std::cout << __func__ << " Start FlushUpdateMetaData " << zfile->name
              << " level: " << zfile->level << " size: " << zfile->size
              << std::endl;
    for (std::uint32_t i = zfile->startIndex; i < zfile->map.size(); i++) {
      struct zrocks_map& pInfo = zfile->map[i];
      std::cout << " nodeId: " << pInfo.g.node_id << " start: " << pInfo.g.start
                << " num: " << pInfo.g.num << std::endl;
    }
  }

  int length = zfile->WriteMetaToBuf(buf + dataLen);
  dataLen += length;

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.tag        = GCChange;
  metadataHead.dataLength = dataLen - sizeof(MetadataHead);

  memcpy(buf, &metadataHead, sizeof(MetadataHead));

  metaMutex.Lock();
  int ret = zrocks_write_file_metadata(buf, dataLen);
  if (ret == MD_WRITE_FULL) {
    std::cout << __func__ << ": zrocks_write_metadata FULL " << ret
              << std::endl;
    FlushMetaData();
  } else if (ret != 0) {
    std::cout << __func__ << ": zrocks_write_metadata error ret " << ret
              << std::endl;
  }
  metaMutex.Unlock();

  if (ZNS_DEBUG_META)
    std::cout << __func__ << " End FlushUpdateMetaData " << std::endl;
  zrocks_free(buf);

  return Status::OK();
}

Status ZNSEnv::FlushDelMetaData(const std::string& fileName) {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  unsigned char* buf = NULL;
  buf = reinterpret_cast<unsigned char*>(zrocks_alloc(ZNS_ALIGMENT));
  if (buf == NULL) {
    return Status::MemoryLimit();
  }

  memset(buf, 0, ZNS_ALIGMENT);
  // reserved head position
  int dataLen = sizeof(MetadataHead);
  if (ZNS_DEBUG_META)
    std::cout << __func__ << " Start FlushDelMetaData fileName: " << fileName
              << std::endl;

  memcpy(buf + dataLen, fileName.c_str(), fileName.length());
  dataLen += FILE_NAME_LEN;

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.tag        = Delete;
  metadataHead.dataLength = dataLen - sizeof(MetadataHead);

  /* std::uint32_t crc = 0;
  crc = crc32c::Extend(crc, (const char*)buf + sizeof(MetadataHead),
                       metadataHead.dataLength);
  crc = crc32c::Mask(crc);
  metadataHead.crc = crc; */
  memcpy(buf, &metadataHead, sizeof(MetadataHead));
  metaMutex.Lock();
  int ret = zrocks_write_file_metadata(buf, dataLen);
  if (ret == MD_WRITE_FULL) {
    std::cout << __func__ << ": zrocks_write_metadata FULL " << ret
              << std::endl;
    FlushMetaData();
  } else if (ret != 0) {
    std::cout << __func__ << ": zrocks_write_metadata error ret " << ret
              << std::endl;
  }
  metaMutex.Unlock();

  if (ZNS_DEBUG_META)
    std::cout << __func__ << " End FlushDelMetaData " << std::endl;

  zrocks_free(buf);
  return Status::OK();
}

Status ZNSEnv::FlushReplaceMetaData(const std::string& srcName,
                                    const std::string& destName) {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  unsigned char* buf = NULL;
  buf = reinterpret_cast<unsigned char*>(zrocks_alloc(ZNS_ALIGMENT));
  if (buf == NULL) {
    return Status::MemoryLimit();
  }

  memset(buf, 0, ZNS_ALIGMENT);
  // reserved head position
  int dataLen = sizeof(MetadataHead);
  if (ZNS_DEBUG_META) {
    std::cout << __func__ << " Start FlushReplaceMetaData srcName " << srcName
              << " destName " << destName << std::endl;
  }

  memcpy(buf + dataLen, srcName.c_str(), srcName.length());
  dataLen += FILE_NAME_LEN;

  memcpy(buf + dataLen, destName.c_str(), destName.length());
  dataLen += FILE_NAME_LEN;

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.tag        = Replace;
  metadataHead.dataLength = dataLen - sizeof(MetadataHead);

  /* std::uint32_t crc = 0;
  crc = crc32c::Extend(crc, (const char*)metaBuf + sizeof(MetadataHead),
                       metadataHead.dataLength);
  crc = crc32c::Mask(crc);
  metadataHead.crc = crc; */

  memcpy(buf, &metadataHead, sizeof(MetadataHead));
  metaMutex.Lock();
  int ret = zrocks_write_file_metadata(buf, dataLen);
  if (ret == MD_WRITE_FULL) {
    std::cout << __func__ << ": zrocks_write_metadata FULL " << ret
              << std::endl;
    FlushMetaData();
  } else if (ret != 0) {
    std::cout << __func__ << ": zrocks_write_metadata error ret " << ret
              << std::endl;
  }
  metaMutex.Unlock();

  if (ZNS_DEBUG_META)
    std::cout << __func__ << " End FlushReplaceMetaData " << std::endl;
  zrocks_free(buf);
  return Status::OK();
}

void ZNSEnv::RecoverFileFromBuf(unsigned char* buf,
                        std::uint32_t& praseLen, bool replace) {
  praseLen                    = 0;
  ZrocksFileMeta fileMetaData = *(reinterpret_cast<ZrocksFileMeta*>(buf));

  ZNSFile* znsFile = files[fileMetaData.filename];
  if (znsFile && replace) {
    znsFile->map.clear();
  }

  if (znsFile == NULL) {
    znsFile = new ZNSFile(fileMetaData.filename, -1);
  }

  if (znsFile == NULL) {
    return;
  }

  znsFile->size  = fileMetaData.filesize;
  znsFile->level = fileMetaData.level;

  std::uint32_t len = sizeof(ZrocksFileMeta);
  for (std::int32_t i = 0; i < fileMetaData.pieceNum; i++) {
    struct zrocks_map p = *(reinterpret_cast<struct zrocks_map*>(buf + len));
    znsFile->map.push_back(p);
    len += sizeof(struct zrocks_map);
  }

  if (ZNS_DEBUG_META) {
    // znsFile->PrintMetaData();
  }

  filesMutex.Lock();
  files[znsFile->name] = znsFile;
  filesMutex.Unlock();

  praseLen = len;
}

void ZNSEnv::ClearMetaData() {
  std::map<std::string, ZNSFile*>::iterator iter;
  for (iter = files.begin(); iter != files.end(); ++iter) {
    delete iter->second;
  }
  files.clear();
}

void ZNSEnv::SetNodesInfo() {
  std::map<std::string, ZNSFile*>::iterator iter;
  for (iter = files.begin(); iter != files.end(); ++iter) {
    ZNSFile* znsfile = iter->second;
    if (!znsfile) {
      continue;
    }
    for (uint32_t i = 0; i < znsfile->map.size(); i++) {
      struct zrocks_map& pInfo = znsfile->map[i];
      zrocks_node_set(pInfo.g.node_id, znsfile->level, pInfo.g.num);
      // std::cout << " nodeId: " << pInfo.g.node_id << " level: " <<
      // znsfile->level <<std::endl;
    }
  }
}

void ZNSEnv::PrintMetaData() {
  std::map<std::string, ZNSFile*>::iterator iter;
  for (iter = files.begin(); iter != files.end(); ++iter) {
    ZNSFile* znsfile = iter->second;
    if (znsfile != NULL) {
      znsfile->PrintMetaData();
    }
  }
}

Status ZNSEnv::LoadMetaData() {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  metaBuf =
      reinterpret_cast<unsigned char*>(zrocks_alloc(FILE_METADATA_BUF_SIZE));
  if (metaBuf == NULL) {
    return Status::MemoryLimit();
  }
  memset(metaBuf, 0, FILE_METADATA_BUF_SIZE);

  std::cout << __func__ << " Start LoadMetaData " << std::endl;

  std::uint8_t  metaZoneNum              = MAX_META_ZONE;
  std::uint64_t metaSlbas[MAX_META_ZONE] = {0};
  zrocks_get_metadata_slbas(metaSlbas, &metaZoneNum);
  int                                    ret = 0;
  std::map<std::uint32_t, std::uint64_t> seqSlbaMap;
  for (std::uint8_t i = 0; i < metaZoneNum; i++) {
    ret = zrocks_read_metadata(metaSlbas[i], metaBuf, sizeof(MetaZoneHead));
    if (ret) {
      std::cout << "read superblock err" << std::endl;
      return Status::IOError();
    }

    MetaZoneHead* mzHead = (MetaZoneHead*)metaBuf;
    if (mzHead->info.magic == METADATA_MAGIC) {
      seqSlbaMap[mzHead->info.sequence] = metaSlbas[i];
      if (mzHead->info.sequence > sequence) {
        sequence = mzHead->info.sequence;
      }
    }
  }

  std::map<std::uint32_t, std::uint64_t>::reverse_iterator iter;
  for (iter = seqSlbaMap.rbegin(); iter != seqSlbaMap.rend(); ++iter) {
    ClearMetaData();
    std::uint64_t readSlba = iter->second + (sizeof(MetaZoneHead) / ZNS_ALIGMENT);
    while (true) {
      std::uint32_t readLen  = ZNS_ALIGMENT;
      std::uint32_t praseLen = 0;
      ret                    = zrocks_read_metadata(readSlba, metaBuf, readLen);
      if (ret) {
        std::cout << __func__ << ":  zrocks_read_metadata head error"
                  << std::endl;
        return Status::IOError();
      }

      MetadataHead* metadataHead = (MetadataHead*)metaBuf;
      if (metadataHead->dataLength == 0) {
        zrocks_switch_zone(iter->second);
        sequence++;
        goto LOAD_END;
      }

      if (metadataHead->dataLength + sizeof(MetadataHead) > readLen) {
        ret = zrocks_read_metadata(
            readSlba + readLen / ZNS_ALIGMENT, metaBuf + readLen,
            metadataHead->dataLength + sizeof(MetadataHead) - readLen);
        if (ret) {
          std::cout << __func__ << ":  zrocks_read_metadata head error"
                    << std::endl;
          return Status::IOError();
        }
      }

      praseLen += sizeof(MetadataHead);
      readSlba +=
          (sizeof(MetadataHead) + metadataHead->dataLength) / ZNS_ALIGMENT;
      switch (metadataHead->tag) {
        case Base: {
          std::uint32_t fileNum = *(std::uint32_t*)(metaBuf + praseLen);
          praseLen += sizeof(fileNum);
          for (std::uint16_t i = 0; i < fileNum; i++) {
            std::uint32_t fileMetaLen = 0;
            RecoverFileFromBuf(metaBuf + praseLen, fileMetaLen, false);
            praseLen += fileMetaLen;
          }
        } break;
        case Update: {
          std::uint32_t fileMetaLen = 0;
          RecoverFileFromBuf(metaBuf + praseLen, fileMetaLen, false);
        } break;
        case Replace: {
          std::string srcFileName = (char*)metaBuf + praseLen;
          praseLen += FILE_NAME_LEN;
          std::string dstFileName = (char*)metaBuf + praseLen;
          ZNSFile*    znsFile     = files[srcFileName];
          files.erase(srcFileName);
          files[dstFileName] = znsFile;
        } break;
        case Delete: {
          std::string fileName = (char*)metaBuf + praseLen;
          files.erase(fileName);
        } break;
        case GCChange: {
          std::uint32_t fileMetaLen = 0;
          RecoverFileFromBuf(metaBuf + praseLen, fileMetaLen, true);
        } break;
        default:
          break;
      }
    }
  }

LOAD_END:
  SetNodesInfo();
  if (ZNS_DEBUG_META) {
    PrintMetaData();
  }

  zrocks_clear_invalid_nodes();

  filesMutex.Lock();
  FlushMetaData();
  filesMutex.Unlock();

  std::cout << __func__ << " End LoadMetaData " << std::endl;
  return Status::OK();
}

void ZNSEnv::GCWorker() {
  uint32_t id;

  while (run_gc_worker_) {
    usleep(GC_DETECTION_TIME);
    if (ZNS_DEBUG_GC) {
      std::cout << __func__ << " free nodes: " << zrocks_gc_get_free_nodes_num()
                << " threshold: " << gc_nodes_threshold << std::endl;
    }

    if (zrocks_gc_get_free_nodes_num() >= gc_nodes_threshold) {
      continue;
    }

    /* Get invalid percent of sst full nodes. */
    uint32_t nr_nodes = zrocks_gc_get_nodes_num();
    if (ZNS_DEBUG_GC) {
      std::cout << __func__ << " Total nr_nodes: " << nr_nodes << std::endl;
    }

    uint32_t* invalid_percent = new uint32_t[nr_nodes]();
    zrocks_gc_get_full_nodes(invalid_percent);

    /* Sort by descending invalid percent. */
    std::vector<std::pair<uint32_t, uint32_t>> invalid_nid_map;
    for (id = 0; id < nr_nodes; id++) {
      if (invalid_percent[id] == 100 ||
          invalid_percent[id] < ZNS_GC_NODE_IVD_MIN_PERCENT)
        continue;
      invalid_nid_map.push_back(std::make_pair(invalid_percent[id], id));
    }

    if (ZNS_DEBUG_GC) {
      std::cout << __func__
                << " invalid_nid_map.size(): " << invalid_nid_map.size()
                << std::endl;
    }

    if (!invalid_nid_map.size()) {
      if (ZNS_DEBUG_GC) {
        std::cout << __func__
                  << " There is no node with an inefficiency higher than 80%. "
                  << std::endl;
      }
      continue;
    }
    std::sort(invalid_nid_map.begin(), invalid_nid_map.end(),
              std::greater<std::pair<uint32_t, uint32_t>>());

    for (auto& point : invalid_nid_map) {
      if (zrocks_gc_get_free_nodes_num() >= gc_nodes_threshold) {
        if (ZNS_DEBUG_GC) {
          std::cout << __func__ << " There are enough free nodes. "
                    << std::endl;
        }
        break;
      }

      if (ZNS_DEBUG_GC) {
        std::cout << __func__ << " node : " << point.second
                  << "  invalid percent : " << point.first << std::endl;
      }

      uint32_t node_id = point.second;
      auto     iter    = nid_file_map.find(node_id);
      if (iter != nid_file_map.end()) {
        std::vector<std::string> fn_list = iter->second;
        for (auto& fn : fn_list) {
          MigrateNodeFile(node_id, fn);
        }
      }
    }
  }
}

/* Return OK if migration is successful. */
void ZNSEnv::MigrateNodeFile(const std::uint32_t nid,
                             const std::string   file_name) {
  struct zrocks_map               maps[2];
  uint16_t                        pieces = 0;
  int                             ret;
  uint16_t                        i;
  std::vector<struct zrocks_map*> map_change;
  ZNSFile*                        znsfile;
  int                             cnid, clvl;

  filesMutex.Lock();
  if (files.find(file_name) == files.end()) {
    filesMutex.Unlock();
    if (ZNS_DEBUG_GC) {
      std::cout << __func__ << " File [" << file_name << "] has been deleted!"
                << std::endl;
    }
    return;
  }

  znsfile = files[file_name];
  if (znsfile->IsWR()) {
    filesMutex.Unlock();
    if (ZNS_DEBUG_GC) {
      std::cout << __func__ << " File [" << file_name
                << "] can not be migrated [ writing ] !" << std::endl;
    }
    return;
  }

  std::vector<struct zrocks_map> map_copy(znsfile->map);
  cnid = znsfile->current_nid;
  clvl = znsfile->level;

  filesMutex.Unlock();

  if (ZNS_DEBUG_GC) {
    std::cout << __func__ << " Migrate file [" << file_name << "] start!"
              << std::endl;
  }

  auto m = map_copy.begin();
  while (m != map_copy.end() && run_gc_worker_) {
    struct zrocks_map* value = &(*m);

    if (value->g.node_id != nid) {
      m++;
      continue;
    }

    uint64_t tmp   = value->g.start;
    uint64_t off   = tmp * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    size_t   msize = value->g.num * ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;

    if (ZNS_DEBUG_GC) {
      std::cout << __func__ << " SRC file: " << file_name
                << " DONE. node_id: " << nid << " start: " << value->g.start
                << " len: " << value->g.num << std::endl;
    }

    ret = zrocks_read(nid, off, gc_buffer, msize, true);
    if (ret) {
      m++;
      continue;
    }
    ret = zrocks_write(gc_buffer, msize, clvl, maps, &pieces, true);
    if (ret || !pieces) {
      m++;
      continue;
    }

    /* Update the map. */
    m = map_copy.erase(m);

    for (std::uint16_t p = 0; p < pieces; p++) {
      m = map_copy.emplace(m, maps[p]);
      m++;

      if (ZNS_DEBUG_GC) {
        std::cout << __func__ << " DST file: " << file_name
                  << " DONE. node_id: " << maps[p].g.node_id
                  << " start: " << maps[p].g.start << " len: " << maps[p].g.num
                  << std::endl;
      }

      if (cnid != maps[p].g.node_id) {
        cnid = maps[p].g.node_id;
        filesMutex.Lock();
        nid_file_map[cnid].emplace_back(file_name);
        filesMutex.Unlock();
      }

      map_change.emplace_back(&maps[p]);
    }
  }

  filesMutex.Lock();
  if (files.find(file_name) == files.end()) {
    filesMutex.Unlock();
    for (i = 0; i < map_change.size(); i++) {
      zrocks_trim(map_change[i], true);
    }
    return;
  }

  /* Waiting for read lock release. */
  ZNSWriteLock rl(znsfile);
  for (i = 0; i < znsfile->map.size(); i++) {
    if (znsfile->map[i].g.node_id == nid) {
      zrocks_trim(&znsfile->map[i], true);
    }
  }
  znsfile->map.assign(map_copy.begin(), map_copy.end());
  FlushGCChangeMetaData(znsfile);
  filesMutex.Unlock();
}

/* ### The factory method for creating a ZNS Env ### */
Status NewZNSEnv(Env** zns_env, const std::string& dev_name) {
  ZNSEnv* znsEnv = new ZNSEnv(dev_name);
  *zns_env       = znsEnv;

  znsEnv->envStartMutex.Lock();
  if (znsEnv->isEnvStart) {
    znsEnv->envStartMutex.Unlock();
    return Status::OK();
  }

  // Load metaData
  Status status = znsEnv->LoadMetaData();
  if (!status.ok()) {
    znsEnv->envStartMutex.Unlock();
    std::cout << __func__ << ": znsEnv LoadMetaData error" << std::endl;
    return Status::IOError();
  }

  znsEnv->envStartMutex.Unlock();

  return Status::OK();
}

#ifndef ROCKSDB_LITE
extern "C" FactoryFunc<Env> xztl_env_reg;
  FactoryFunc<Env> xztl_env_reg =
#if (ROCKSDB_MAJOR >= 7) || (ROCKSDB_MAJOR == 6 && ROCKSDB_MINOR >= 29)
  ObjectLibrary::Default()->AddFactory<Env>(
    ObjectLibrary::PatternEntry("xztl", false).AddSeparator(":"),
#else
  ObjectLibrary::Default()->Register<Env>(
    "xztl:.*",
#endif
    [](const std::string& uri,
    std::unique_ptr<Env>* guard,
    std::string* errmsg) {
      std::cout << "Input uri is " << uri.c_str() << std::endl;

      Env* env = nullptr;
      std::string devName = uri;
      devName.replace(0, strlen("xztl:"), "");
      Status s = NewZNSEnv(&env, devName);
      if (!s.ok()) {
        *errmsg = "Failed to NewZNSEnv!";
      }

      guard->reset(env);
      return guard->get();
});
#endif

}  // namespace rocksdb

