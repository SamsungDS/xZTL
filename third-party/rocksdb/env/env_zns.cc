//  Copyright (c) 2019, Samsung Electronics.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
//  Written by Ivan L. Picoli <i.picoli@samsung.com>
#include <sys/time.h>
#include <iostream>
#include <memory>

#include "env/env_zns.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

#define FILE_METADATA_BUF_SIZE (40 * 1024 * 1024)
#define FLUSH_INTERVAL (60 * 60)
#define SLEEP_TIME 5

namespace rocksdb {

std::uint32_t ZNSFile::GetFileMetaLen() {
  uint32_t metaLen = sizeof(ZrocksFileMeta);
  return metaLen;
}

std::uint32_t ZNSFile::WriteMetaToBuf(unsigned char* buf) {
  // reserved single file head
  std::uint32_t length = sizeof(ZrocksFileMeta);

  ZrocksFileMeta fileMetaData;
  fileMetaData.magic = FILE_METADATA_MAGIC;
  fileMetaData.filesize = size;
  fileMetaData.level = level;
  fileMetaData.znode_id = znode_id;
  memcpy(fileMetaData.filename, name.c_str(), name.length());
  memcpy(buf, &fileMetaData, sizeof(ZrocksFileMeta));

  return length;
}

void ZNSFile::PrintMetaData() {
  std::cout << __func__ << " FileName: " << name << " znode_id: " << znode_id
            << std::endl;
}
/* ### ZNS Environment method implementation ### */
void ZNSEnv::NodeSta(std::int32_t znode_id, size_t n) {
  double seconds;

  if (start_ns == 0) {
    GET_NANOSECONDS(start_ns, ts_s);
  }

  read_bytes[znode_id] += n;

  GET_NANOSECONDS(end_ns, ts_e);
  seconds = (double)(end_ns - start_ns) / (double)1000000000;  // NOLINT
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
}

Status ZNSEnv::NewSequentialFile(const std::string& fname,
                                 std::unique_ptr<SequentialFile>* result,
                                 const EnvOptions& options) {
  if (ZNS_DEBUG) std::cout << __func__ << ":" << fname << std::endl;

  if (IsFilePosix(fname)) {
    return posixEnv->NewSequentialFile(fname, result, options);
  }

  result->reset();

  ZNSSequentialFile* f = new ZNSSequentialFile(fname, this, options);
  result->reset(dynamic_cast<SequentialFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewRandomAccessFile(const std::string& fname,
                                   std::unique_ptr<RandomAccessFile>* result,
                                   const EnvOptions& options) {
  if (ZNS_DEBUG) std::cout << __func__ << ":" << fname << std::endl;

  if (IsFilePosix(fname)) {
    return posixEnv->NewRandomAccessFile(fname, result, options);
  }

  ZNSRandomAccessFile* f = new ZNSRandomAccessFile(fname, this, options);
  result->reset(dynamic_cast<RandomAccessFile*>(f));

  return Status::OK();
}

Status ZNSEnv::NewWritableFile(const std::string& fname,
                               std::unique_ptr<WritableFile>* result,
                               const EnvOptions& options) {
  if (ZNS_DEBUG) std::cout << __func__ << ":" << fname << std::endl;

  uint32_t fileNum = 0;

  if (IsFilePosix(fname)) {
    return posixEnv->NewWritableFile(fname, result, options);
  } else {
    posixEnv->NewWritableFile(fname, result, options);
  }

  ZNSWritableFile* f = new ZNSWritableFile(fname, this, options, 0);
  result->reset(dynamic_cast<WritableFile*>(f));

  filesMutex.Lock();
  fileNum = files.count(fname);
  if (fileNum != 0) {
    delete files[fname];
    files.erase(fname);
  }

  files[fname] = new ZNSFile(fname, 0);
  files[fname]->uuididx = uuididx++;
  filesMutex.Unlock();

  return Status::OK();
}

Status ZNSEnv::NewWritableLeveledFile(const std::string& fname,
                                      std::unique_ptr<WritableFile>* result,
                                      const EnvOptions& options, int level) {
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

  ZNSWritableFile* f = new ZNSWritableFile(fname, this, options, level);
  result->reset(dynamic_cast<WritableFile*>(f));

  filesMutex.Lock();
  fileNum = files.count(fname);
  if (fileNum != 0) {
    delete files[fname];
    files.erase(fname);
  }

  files[fname] = new ZNSFile(fname, level);
  files[fname]->uuididx = uuididx++;
  filesMutex.Unlock();

  return Status::OK();
}

Status ZNSEnv::DeleteFile(const std::string& fname) {
  if (ZNS_DEBUG) std::cout << __func__ << ":" << fname << std::endl;

  if (IsFilePosix(fname)) {
    return posixEnv->DeleteFile(fname);
  }
  posixEnv->DeleteFile(fname);

#if !ZNS_OBJ_STORE
  filesMutex.Lock();
  if (files.find(fname) == files.end() || files[fname] == NULL) {
    filesMutex.Unlock();
    return Status::OK();
  }

  if (files[fname]->znode_id != -1) zrocks_trim(files[fname]->znode_id);

  delete files[fname];
  files.erase(fname);
  FlushMetaData();
  filesMutex.Unlock();
#endif

  return Status::OK();
}

Status ZNSEnv::GetFileSize(const std::string& fname, std::uint64_t* size) {
  if (IsFilePosix(fname)) {
    if (ZNS_DEBUG) std::cout << __func__ << ":" << fname << std::endl;
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
                                       std::uint64_t* file_mtime) {
  if (ZNS_DEBUG) std::cout << __func__ << ":" << fname << std::endl;

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

  unsigned char* buf =
      reinterpret_cast<unsigned char*>(zrocks_alloc(FILE_METADATA_BUF_SIZE));
  if (buf == NULL) {
    return Status::MemoryLimit();
  }

  memset(buf, 0, FILE_METADATA_BUF_SIZE);
  // reserved head position
  int dataLen = sizeof(MetadataHead);
  if (ZNS_DEBUG_META)
    std::cout << __func__ << " Start FlushMetaData " << std::endl;
  int fileNum = 0;
  std::map<std::string, ZNSFile*>::iterator itermap = files.begin();
  for (; itermap != files.end(); ++itermap) {
    ZNSFile* zfile = itermap->second;
    if (zfile == NULL) {
      continue;
    }

    fileNum++;
    if (ZNS_DEBUG_META) zfile->PrintMetaData();
    if (dataLen + zfile->GetFileMetaLen() >= FILE_METADATA_BUF_SIZE) {
      std::cout << __func__ << ": buf over flow" << std::endl;
      zrocks_free(buf);
      return Status::MemoryLimit();
    }

    int length = zfile->WriteMetaToBuf(buf + dataLen);
    dataLen += length;
  }

  // sector align
  if (dataLen % ZNS_ALIGMENT != 0) {
    dataLen = (dataLen / ZNS_ALIGMENT + 1) * ZNS_ALIGMENT;
  }

  MetadataHead metadataHead;
  metadataHead.meta.dataLength = dataLen - sizeof(MetadataHead);
  metadataHead.meta.magic = METADATA_MAGIC;
  metadataHead.meta.fileNum = fileNum;
  metadataHead.meta.time = time(NULL);

  memcpy(buf, &metadataHead, sizeof(MetadataHead));
  int ret = zrocks_write_file_metadata(buf, dataLen);
  if (ret) {
    std::cout << __func__ << ": zrocks_write_metadata error" << std::endl;
  }
  if (ZNS_DEBUG_META)
    std::cout << __func__ << " End FlushMetaData " << std::endl;

  zrocks_free(buf);
  return Status::OK();
}

void ZNSEnv::RecoverFileFromBuf(unsigned char* buf, std::uint32_t& praseLen) {
  praseLen = 0;
  ZrocksFileMeta fileMetaData = *(reinterpret_cast<ZrocksFileMeta*>(buf));
  if (fileMetaData.magic != FILE_METADATA_MAGIC) {
    std::cout << __func__ << ": fileMetaData magic error" << std::endl;
    return;
  }

  ZNSFile* znsFile = NULL;
  znsFile = new ZNSFile(fileMetaData.filename, -1);
  if (znsFile == NULL) {
    return;
  }

  znsFile->size = fileMetaData.filesize;
  znsFile->level = fileMetaData.level;
  znsFile->znode_id = fileMetaData.znode_id;

  std::uint32_t len = sizeof(ZrocksFileMeta);
  if (ZNS_DEBUG_META) {
    znsFile->PrintMetaData();
  }

  filesMutex.Lock();
  files[znsFile->name] = znsFile;
  filesMutex.Unlock();

  praseLen = len;
}

Status ZNSEnv::LoadMetaData() {
  std::cout << __func__ << " Start LoadMetaData " << std::endl;
  std::uint64_t startLBA = zrocks_get_metadata_slba();
  std::uint64_t lastLBA = startLBA;

  MetadataHead metadataHead;
  int ret = zrocks_read_metadata(startLBA, (unsigned char*)&metadataHead,
                                 sizeof(metadataHead));
  if (ret) {
    std::cout << __func__ << ": first zrocks_read_metadata error" << std::endl;
    return Status::IOError();
  }

  if (metadataHead.meta.magic != METADATA_MAGIC) {
    std::cout << __func__ << ": no metadata" << std::endl;
    return Status::OK();
  }

  while (metadataHead.meta.magic == METADATA_MAGIC) {
    lastLBA = startLBA;
    startLBA +=
        (sizeof(metadataHead) + metadataHead.meta.dataLength) / ZNS_ALIGMENT;

    metadataHead.meta.magic = 0;
    ret = zrocks_read_metadata(startLBA, (unsigned char*)&metadataHead,
                               sizeof(metadataHead));
    if (ret) {
      std::cout << __func__ << ": zrocks_read_metadata error " << ret
                << std::endl;
      return Status::IOError();
    }
  }

  // read right head
  ret = zrocks_read_metadata(lastLBA, (unsigned char*)&metadataHead,
                             sizeof(metadataHead));
  if (ret) {
    std::cout << __func__ << ": zrocks_read_metadata lastLBA error " << ret
              << std::endl;
    return Status::IOError();
  }

  if (metadataHead.meta.dataLength == 0) {
    std::cout << __func__ << ": not found metadata" << std::endl;
    return Status::OK();
  }

  unsigned char* buf = NULL;
  buf = new unsigned char[FILE_METADATA_BUF_SIZE];
  if (buf == NULL) {
    return Status::MemoryLimit();
  }

  memset(buf, 0, FILE_METADATA_BUF_SIZE);

  // read data
  ret = zrocks_read_metadata(lastLBA + sizeof(metadataHead) / ZNS_ALIGMENT, buf,
                             metadataHead.meta.dataLength);
  if (ret) {
    delete[] buf;
    return Status::IOError();
  }

  std::uint32_t len = 0;
  for (std::uint32_t i = 0; i < metadataHead.meta.fileNum; i++) {
    std::uint32_t tmpLen = 0;
    RecoverFileFromBuf(buf + len, tmpLen);
    len += tmpLen;
  }

  std::cout << __func__ << " End LoadMetaData " << std::endl;

  delete[] buf;
  return Status::OK();
}

/* ### The factory method for creating a ZNS Env ### */
Status NewZNSEnv(Env** zns_env, const std::string& dev_name) {
  static ZNSEnv znsenv(dev_name);
  ZNSEnv* znsEnv = &znsenv;
  *zns_env = znsEnv;

#if !ZNS_OBJ_STORE
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
#endif

  return Status::OK();
}

}  // namespace rocksdb
