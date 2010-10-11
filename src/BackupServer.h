/* Copyright (c) 2009 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * \file
 * Declarations for the backup server, currently all backup RPC
 * requests are handled by this module including all the heavy lifting
 * to complete the work requested by the RPCs.
 */

#ifndef RAMCLOUD_BACKUPSERVER_H
#define RAMCLOUD_BACKUPSERVER_H

#include <string>

#include "Common.h"
#include "backuprpc.h"
#include "BackupClient.h"
#include "Bitmap.h"

namespace RAMCloud {

struct BackupException : public Exception {
    BackupException() : Exception() {}
    explicit BackupException(std::string msg) : Exception(msg) {}
    explicit BackupException(int errNo) : Exception(errNo) {}
};

struct BackupLogIOException : public BackupException {
    BackupLogIOException() : BackupException() {}
    explicit BackupLogIOException(std::string msg): BackupException(msg) {}
    explicit BackupLogIOException(int errNo) : BackupException(errNo) {}
};
struct BackupInvalidRPCOpException : public BackupException {
    BackupInvalidRPCOpException() : BackupException() {}
    explicit BackupInvalidRPCOpException(std::string msg)
        : BackupException(msg) {}
    explicit BackupInvalidRPCOpException(int errNo)
        : BackupException(errNo) {}
};
struct BackupSegmentOverflowException : public BackupException {
    BackupSegmentOverflowException() : BackupException() {}
    explicit BackupSegmentOverflowException(std::string msg)
        : BackupException(msg) {}
    explicit BackupSegmentOverflowException(int errNo)
        : BackupException(errNo) {}
};

const uint64_t SEGMENT_FRAMES = SEGMENT_COUNT * 2;
const uint64_t LOG_SPACE = SEGMENT_FRAMES * SEGMENT_SIZE;

const uint64_t INVALID_SEGMENT_NUM = ~(0ull);

class BackupServer : BackupClient {
  public:
    explicit BackupServer();
    explicit BackupServer(const char *logPath, int logOpenFlags = 0);
    virtual ~BackupServer();
    void run();
  private:
    void handleHeartbeat(const backup_rpc *req, backup_rpc *resp);
    void handleWrite(const backup_rpc *req, backup_rpc *resp);
    void handleBegin(const backup_rpc *req, backup_rpc *resp);
    void handleCommit(const backup_rpc *req, backup_rpc *resp);
    void handleFree(const backup_rpc *req, backup_rpc *resp);
    void handleGetSegmentList(const backup_rpc *req, backup_rpc *resp);
    void handleGetSegmentMetadata(const backup_rpc *req, backup_rpc *resp);
    void handleRetrieve(const backup_rpc *req, backup_rpc *resp);

    void handleRPC();

    virtual void heartbeat() {}
    virtual void writeSegment(uint64_t segNum, uint32_t offset,
                              const void *data, uint32_t len);
    virtual void commitSegment(uint64_t segNum);
    virtual void freeSegment(uint64_t segNum);
    virtual uint32_t getSegmentList(uint64_t *list, uint32_t maxSize);
    virtual uint32_t getSegmentMetadata(uint64_t segNum,
                                        RecoveryObjectMetadata *list,
                                        uint32_t maxSize);
    virtual void retrieveSegment(uint64_t segNum, void *buf);

    void flushSegment();
    void extractMetadata(const void *p,
                         uint64_t offset,
                         RecoveryObjectMetadata *meta);

    void reserveSpace();
    uint64_t frameForSegNum(uint64_t segnum);

    /** A file descriptor for the log file */
    int logFD;
    /**
     * The start of the active segment, it is pagesize aligned to
     * support O_DIRECT writes
     */
    char *seg;
    /** Segment number of the active segment */
    uint64_t openSegNum;

    /**
     * This array, given a segment frame, produces the current segment
     * number that is stored there.
     * SegmentFrame -> SegmentId
     */
    uint64_t segmentAtFrame[SEGMENT_FRAMES];

    /**
     * Tracks which segment frames are free on disk (i.e. frames that
     * contain live segments
     */
    Bitmap<SEGMENT_FRAMES> freeMap;

    friend class BackupServerTest;
    DISALLOW_COPY_AND_ASSIGN(BackupServer);
};

} // namespace RAMCloud

#endif
