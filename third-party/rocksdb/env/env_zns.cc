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
#include "env/env_zns.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

#define FILE_METADATA_BUF_SIZE (80 * 1024 * 1024)
#define FLUSH_INTERVAL         (60 * 60)
#define SLEEP_TIME             5
#define MAX_META_ZONE          2
#define MD_WRITE_FULL          0x1d

enum Operation { Base, Update, Replace, Delete };

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
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << std::endl;

  if (IsFilePosix(fname)) {
    return posixEnv->NewSequentialFile(fname, result, options);
  }

  result->reset();

  ZNSSequentialFile* f = new ZNSSequentialFile(fname, this, options);
  result->reset(dynamic_cast<SequentialFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewRandomAccessFile(const std::string&                 fname,
                                   std::unique_ptr<RandomAccessFile>* result,
                                   const EnvOptions&                  options) {
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << std::endl;

  if (IsFilePosix(fname)) {
    return posixEnv->NewRandomAccessFile(fname, result, options);
  }

  ZNSRandomAccessFile* f = new ZNSRandomAccessFile(fname, this, options);
  result->reset(dynamic_cast<RandomAccessFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewWritableFile(const std::string&             fname,
                               std::unique_ptr<WritableFile>* result,
                               const EnvOptions&              options) {
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << std::endl;

  uint32_t fileNum = 0;

  if (IsFilePosix(fname)) {
    return posixEnv->NewWritableFile(fname, result, options);
  } else {
    posixEnv->NewWritableFile(fname, result, options);
  }

  filesMutex.Lock();
  fileNum = files.count(fname);
  filesMutex.Unlock();

  if (fileNum != 0) {
    delete files[fname];
    files.erase(fname);
  }

  filesMutex.Lock();
  files[fname]          = new ZNSFile(fname, 0);
  files[fname]->uuididx = uuididx++;
  filesMutex.Unlock();

  ZNSWritableFile* f = new ZNSWritableFile(fname, this, options, 0);
  result->reset(dynamic_cast<WritableFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewWritableLeveledFile(const std::string&             fname,
                                      std::unique_ptr<WritableFile>* result,
                                      const EnvOptions& options, int level) {
  level += 1;
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << " lvl: " << level << std::endl;

  uint32_t fileNum = 0;

  if (IsFilePosix(fname)) {
    return posixEnv->NewWritableFile(fname, result, options);
  } else {
    posixEnv->NewWritableFile(fname, result, options);
  }

  if (level < 0) {
    return NewWritableFile(fname, result, options);
  }

  filesMutex.Lock();
  fileNum = files.count(fname);
  filesMutex.Unlock();

  if (fileNum != 0) {
    delete files[fname];
    files.erase(fname);
  }

  filesMutex.Lock();
  files[fname]          = new ZNSFile(fname, level);
  files[fname]->uuididx = uuididx++;
  filesMutex.Unlock();

  ZNSWritableFile* f = new ZNSWritableFile(fname, this, options, level);
  result->reset(dynamic_cast<WritableFile*>(f));

  return Status::OK();
}

Status ZNSEnv::DeleteFile(const std::string& fname) {
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << std::endl;

  unsigned           i;
  struct zrocks_map* map;

  if (IsFilePosix(fname)) {
    return posixEnv->DeleteFile(fname);
  }
  posixEnv->DeleteFile(fname);

  filesMutex.Lock();
  if (files.find(fname) == files.end() || files[fname] == NULL) {
    filesMutex.Unlock();
    return Status::OK();
  }

  ZNSFile* znsfile = files[fname];
  for (i = 0; i < znsfile->map.size(); i++) {
    map = &files[fname]->map.at(i);
    zrocks_trim(map);
  }

  delete files[fname];
  files.erase(fname);
  filesMutex.Unlock();

  FlushDelMetaData(fname);
  return Status::OK();
}

Status ZNSEnv::GetFileSize(const std::string& fname, std::uint64_t* size) {
  if (IsFilePosix(fname)) {
    if (ZNS_DEBUG)
      std::cout << __func__ << ":" << fname << std::endl;
    return posixEnv->GetFileSize(fname, size);
  }

  filesMutex.Lock();
  if (files.find(fname) == files.end() || files[fname] == NULL) {
    filesMutex.Unlock();
    return Status::OK();
  }

  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << "size: " << files[fname]->size
              << std::endl;

  *size = files[fname]->size;
  filesMutex.Unlock();

  return Status::OK();
}

Status ZNSEnv::GetFileModificationTime(const std::string& fname,
                                       std::uint64_t*     file_mtime) {
  if (ZNS_DEBUG)
    std::cout << __func__ << ":" << fname << std::endl;

  if (IsFilePosix(fname)) {
    return posixEnv->GetFileModificationTime(fname, file_mtime);
  }

  /* TODO: Get SST files modification time from ZNS */
  *file_mtime = 0;

  return Status::OK();
}

Status ZNSEnv::FlushMetaData() {
  if (!ZNS_META_SWITCH) {
    return Status::OK();
  }

  filesMutex.Lock();
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
      filesMutex.Unlock();
      return Status::MemoryLimit();
    }

    int length = zfile->WriteMetaToBuf(metaBuf + dataLen);
    dataLen += length;
  }
  filesMutex.Unlock();

  memcpy(metaBuf + sizeof(struct MetaZoneHead) + sizeof(MetadataHead), &fileNum,
         sizeof(fileNum));

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.dataLength = dataLen - sizeof(MetadataHead) - sizeof(MetaZoneHead);
  metadataHead.tag        = Base;

  /* std::uint32_t crc = 0;
  crc               = crc32c::Extend(
                    crc, (const char*)metaBuf + sizeof(SuperBlock) +
  sizeof(MetadataHead), metadataHead.dataLength);
  // std::cout << __func__ << ": crc " << crc << " crc32c::Mask(crc) " <<
  // crc32c::Mask(crc) << " metadataHead.dataLength " << metadataHead.dataLength
  // << std::endl;
  crc              = crc32c::Mask(crc);
  metadataHead.crc = crc; */

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

Status ZNSEnv::FlushReplaceMetaData(std::string& srcName,
                                    std::string& destName) {
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

void ZNSEnv::RecoverFileFromBuf(unsigned char* buf, std::uint32_t& praseLen) {
  praseLen                    = 0;
  ZrocksFileMeta fileMetaData = *(reinterpret_cast<ZrocksFileMeta*>(buf));
  // std::cout << " filename: " << fileMetaData.filename <<std::endl;

  ZNSFile* znsFile = files[fileMetaData.filename];
  if (znsFile == NULL) {
    znsFile = new ZNSFile(fileMetaData.filename, -1);
  }

  if (znsFile == NULL) {
    return;
  }

  znsFile->size  = fileMetaData.filesize;
  znsFile->level = fileMetaData.level;

  std::uint32_t len = sizeof(ZrocksFileMeta);
  for (std::uint16_t i = 0; i < fileMetaData.pieceNum; i++) {
    struct zrocks_map p = *(reinterpret_cast<struct zrocks_map*>(buf + len));
    znsFile->map.push_back(p);
    // std::cout << " nodeId: " << p.g.node_id << " start: " << p.g.start <<  "
    // num: " << p.g.num <<std::endl;
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

      /* uint32_t crc = 0;
      crc = crc32c::Extend(crc, (const char*)metaBuf + sizeof(MetadataHead),
                           metadataHead->dataLength);
      // std::cout << __func__ << " crc " << crc << " metadataHead->crc " <<
      // metadataHead->crc << " umask crc: " <<
      // crc32c::Unmask(metadataHead->crc) << std::endl;
      if (crc != crc32c::Unmask(metadataHead->crc)) {
        std::cout << __func__ << ":  crc verify failed" << std::endl;
        return Status::IOError();
      } */

      praseLen += sizeof(MetadataHead);
      readSlba +=
          (sizeof(MetadataHead) + metadataHead->dataLength) / ZNS_ALIGMENT;
      switch (metadataHead->tag) {
        case Base: {
          std::uint32_t fileNum = *(std::uint32_t*)(metaBuf + praseLen);
          praseLen += sizeof(fileNum);
          for (std::uint16_t i = 0; i < fileNum; i++) {
            std::uint32_t fileMetaLen = 0;
            RecoverFileFromBuf(metaBuf + praseLen, fileMetaLen);
            praseLen += fileMetaLen;
          }
          // printf("LOAD ALL\n");
          // PrintMetaData();
          // printf("LOAD ALL End\n");
        } break;
        case Update: {
          std::uint32_t fileMetaLen = 0;
          // printf("LOAD Update\n");
          RecoverFileFromBuf(metaBuf + praseLen, fileMetaLen);
          // printf("LOAD Update End\n");
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
          // printf("LOAD Delete\n");
          // std::cout << "delete " << fileName << std::endl;
          // printf("LOAD Delete End\n");
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

  FlushMetaData();

  std::cout << __func__ << " End LoadMetaData " << std::endl;
  return Status::OK();
}

/* ### The factory method for creating a ZNS Env ### */
Status NewZNSEnv(Env** zns_env, const std::string& dev_name) {
  static ZNSEnv znsenv(dev_name);
  ZNSEnv*       znsEnv = &znsenv;
  *zns_env             = znsEnv;

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

}  // namespace rocksdb
