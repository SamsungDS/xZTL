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
#define FLUSH_INTERVAL  (60 * 60)
#define SLEEP_TIME 5

namespace rocksdb {

std::uint32_t ZNSFile::GetFileMetaLen() {
    uint32_t metaLen = sizeof(ZrocksFileMetaHead) + map.size() * sizeof(struct zrocks_map);
    return metaLen;
}

std::uint32_t ZNSFile::WriteMetaToBuf(unsigned char* buf) {
    // reserved single file head
    std::uint32_t length = sizeof(ZrocksFileMetaHead);
    for (std::uint32_t i = 0; i < map.size(); i++) {
        memcpy(buf + length, &map[i], sizeof(struct zrocks_map));
        length += sizeof(struct zrocks_map);
    }

    ZrocksFileMetaHead fileMetaData;
    fileMetaData.magic = FILE_METADATA_MAGIC;
    fileMetaData.zrocksMapNum = map.size();
    fileMetaData.filesize = size;
    fileMetaData.level = level;
    fileMetaData.misalignsize = misalignsize;
    memcpy(fileMetaData.misalign, misalign, misalignsize);

    memcpy(fileMetaData.filename, name.c_str(), name.length());

    memcpy(buf, &fileMetaData, sizeof(ZrocksFileMetaHead));

    return length;
}

void ZNSFile::PrintMetaData() {
    std::cout << __func__ <<  " FileName: " << name << std::endl;
    /*std::vector<struct zrocks_map>::iterator iterMap = map.begin();
    for (; iterMap != map.end(); ++iterMap) {
       std::cout << __func__ <<  "   zrocks_map: " << iterMap->addr << std::endl;
    }

    std::cout << " Misalignsize:" << misalignsize << std::endl;
    for (std::uint32_t i = 0; i < misalignsize; i++) {
        printf("%02x ", misalign[i]);
        if ((i+1)%50==0) {
            printf("\n");
       }
    }
    printf("\n");*/
}

/* ### ZNS Environment method implementation ### */

Status ZNSEnv::NewSequentialFile(const std::string& fname,
	    		   std::unique_ptr<SequentialFile>* result,
			   const EnvOptions& options) {
    if (ZNS_DEBUG) std::cout << __func__ <<  ":" << fname << std::endl;

    if (IsFilePosix(fname)) {
	return posixEnv->NewSequentialFile(fname, result, options);
    }

    result->reset();

    ZNSSequentialFile *f = new ZNSSequentialFile (fname, this, options);
    result->reset(dynamic_cast<SequentialFile*>(f));

    return Status::OK();
}

Status ZNSEnv::NewRandomAccessFile(const std::string& fname,
			    std::unique_ptr<RandomAccessFile>* result,
			    const EnvOptions& options) {
    if (ZNS_DEBUG) std::cout << __func__ <<  ":" << fname << std::endl;

    if (IsFilePosix(fname)) {
	return posixEnv->NewRandomAccessFile(fname, result, options);
    }

    ZNSRandomAccessFile *f = new ZNSRandomAccessFile (fname, this, options);
    result->reset(dynamic_cast<RandomAccessFile*>(f));

    return Status::OK();
}

Status ZNSEnv::NewWritableFile(const std::string& fname,
			    std::unique_ptr<WritableFile>* result,
			    const EnvOptions& options) {
    if (ZNS_DEBUG) std::cout << __func__ <<  ":" << fname << std::endl;

    if (IsFilePosix(fname)) {
	return posixEnv->NewWritableFile(fname, result, options);
    } else {
        posixEnv->NewWritableFile(fname, result, options);
    }

    ZNSWritableFile *f = new ZNSWritableFile (fname, this, options, 0);
    result->reset(dynamic_cast<WritableFile*>(f));

    if (files.count(fname) != 0) {
        DeleteFile(fname);
    }

    filesMutex.Lock();
    files[fname] = new ZNSFile(fname, 0);
    files[fname]->uuididx = uuididx++;
    filesMutex.Unlock();

    return Status::OK();
}

Status ZNSEnv::NewWritableLeveledFile(const std::string& fname,
			    std::unique_ptr<WritableFile>* result,
			    const EnvOptions& options,
			    int level) {
    if (ZNS_DEBUG) std::cout << __func__ <<  ":" << fname << " lvl: "
							<< level << std::endl;

    if (IsFilePosix(fname)) {
	return posixEnv->NewWritableFile(fname, result, options);
    } else {
        posixEnv->NewWritableFile(fname, result, options);
    }
    if (level < 0) {
      return NewWritableFile(fname, result, options);
    }

    ZNSWritableFile *f = new ZNSWritableFile (fname, this, options, level);
    result->reset(dynamic_cast<WritableFile*>(f));

    if (files.count(fname) != 0) {
        DeleteFile(fname);
    }

    filesMutex.Lock();
    files[fname] = new ZNSFile(fname, level);
    files[fname]->uuididx = uuididx++;
    filesMutex.Unlock();

    return Status::OK();
}

Status ZNSEnv::DeleteFile(const std::string& fname) {
#if !ZNS_OBJ_STORE
    unsigned i;
    struct zrocks_map *map;
#endif

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
    for (i = 0; i < files[fname]->map.size(); i++) {
	map = &files[fname]->map.at(i);
	zrocks_trim(map, files[fname]->level);
    }

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
    if (files.find(fname) == files.end() ||  files[fname] == NULL) {
        filesMutex.Unlock();
        return Status::OK();
    }

    if (ZNS_DEBUG)
	std::cout << __func__ << ":" << fname << "size: " << files[fname]->size << std::endl;

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
    unsigned char* buf = reinterpret_cast<unsigned char*>(zrocks_alloc(FILE_METADATA_BUF_SIZE));
    if (buf == NULL) {
        return Status::MemoryLimit();
    }

    memset(buf, 0, FILE_METADATA_BUF_SIZE);
    // reserved head position
    int dataLen = sizeof(MetadataHead);
    if (ZNS_DEBUG)
        std::cout << __func__ << " Start FlushMetaData " << std::endl;
    int fileNum = 0;
    std::map<std::string, ZNSFile*>::iterator itermap = files.begin();
    for (; itermap != files.end(); ++itermap) {
        ZNSFile* zfile = itermap->second;
        if (zfile == NULL) {
            continue;
        }

        fileNum++;
        if (ZNS_DEBUG)
            zfile->PrintMetaData();
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
    if (ZNS_DEBUG)
        std::cout << __func__ << " End FlushMetaData " << std::endl;

    zrocks_free(buf);
    return Status::OK();
}

void ZNSEnv::RecoverFileFromBuf(unsigned char* buf, std::uint32_t& praseLen) {
    praseLen = 0;
    ZrocksFileMetaHead fileMetaData = *(reinterpret_cast<ZrocksFileMetaHead*>(buf));
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
    znsFile->misalignsize = fileMetaData.misalignsize;
    memcpy(znsFile->misalign, fileMetaData.misalign, fileMetaData.misalignsize);

    std::uint32_t len = sizeof(ZrocksFileMetaHead);
    for (std::uint32_t i = 0; i < fileMetaData.zrocksMapNum; i++) {
        struct zrocks_map entry = *(reinterpret_cast<const struct zrocks_map*>(buf+len));
        znsFile->map.push_back(entry);
        len += sizeof(struct zrocks_map);
    }
    if (ZNS_DEBUG) {
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
    int ret = zrocks_read_metadata(startLBA, (unsigned char*)&metadataHead, sizeof(metadataHead));
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
        startLBA += (sizeof(metadataHead) + metadataHead.meta.dataLength) / ZNS_ALIGMENT;

        metadataHead.meta.magic = 0;
        ret = zrocks_read_metadata(startLBA, (unsigned char*)&metadataHead, sizeof(metadataHead));
        if (ret) {
            std::cout << __func__ << ": zrocks_read_metadata error " << ret << std::endl;
            return Status::IOError();
        }
    }

    // read right head
    ret = zrocks_read_metadata(lastLBA, (unsigned char*)&metadataHead, sizeof(metadataHead));
    if (ret) {
        std::cout << __func__ << ": zrocks_read_metadata lastLBA error " << ret << std::endl;
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
    ret = zrocks_read_metadata(lastLBA + sizeof(metadataHead) / ZNS_ALIGMENT, buf, metadataHead.meta.dataLength);
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

void* ZNSEnv::WorkThread(void* arg) {
    ZNSEnv* znsEnv = reinterpret_cast<ZNSEnv*>(arg);
    if (znsEnv == NULL) {
        return NULL;
    }

    znsEnv->isFlushRuning = true;
    time_t lastTime = time(NULL);
    while (znsEnv->isFlushRuning) {
        time_t curTime = time(NULL);
        if (curTime - lastTime >= FLUSH_INTERVAL) {
            Status ret = znsEnv->FlushMetaData();
            lastTime = curTime;
        }

        sleep(SLEEP_TIME);
    }

    return NULL;
}

Status ZNSEnv::StartFlushThread() {
    if (pthread_create(&flushThread, NULL, WorkThread, this)) {
        std::cout << __func__ << ": pthread_create error" << std::endl;
        return Status::Corruption();
    }

    return Status::OK();
}

void ZNSEnv::StopFlushThread() {
    isFlushRuning = false;
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

    // start metaData flush thread
    /*status = znsEnv->StartFlushThread();
    if (!status.ok()) {
        znsEnv->envStartMutex.Unlock();
        std::cout << __func__ << ": znsEnv StartFlushThread error" << std::endl;
        return Status::Incomplete();
    }*/

    znsEnv->envStartMutex.Unlock();
#endif

    return Status::OK();
}

} // namespace rocksdb
