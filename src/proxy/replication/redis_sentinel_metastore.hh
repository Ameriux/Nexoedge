// SPDX-License-Identifier: Apache-2.0

#ifndef __REDIS_SENTINEL_METASTORE_HH__
#define __REDIS_SENTINEL_METASTORE_HH__

#include <string>
#include <vector>
#include <memory>

#include "../metastore/redis_metastore.hh"
#include "sentinel/sentinel_client.hh"
#include "sentinel/connection_pool.hh"

class RedisSentinelMetaStore : public RedisMetaStore {
public:
    /**
     * Constructor that initializes the Redis Sentinel metastore
     **/
    RedisSentinelMetaStore();
    
    /**
     * Destructor that cleans up the Redis Sentinel metastore
     **/
    ~RedisSentinelMetaStore();

    //------------------- 读操作重写 -------------------//
    
    /**
     * See MetaStore::getMeta()
     **/
    bool getMeta(File &f, int getBlocks = 3) override;
    
    /**
     * See MetaStore::getFileName()
     **/
    bool getFileName(boost::uuids::uuid fuuid, File &f) override;
    
    /**
     * See MetaStore::getFileList()
     **/
    unsigned int getFileList(FileInfo **list, unsigned char namespaceId = INVALID_NAMESPACE_ID, bool withSize = true, bool withTime = true, bool withVersions = false, std::string prefix = "") override;
    
    /**
     * See MetaStore::getFolderList()
     **/
    unsigned int getFolderList(std::vector<std::string> &list, unsigned char namespaceId = INVALID_NAMESPACE_ID, std::string prefix = "", bool skipSubfolders = true) override;
    
    /**
     * See MetaStore::getMaxNumKeysSupported()
     **/
    unsigned long int getMaxNumKeysSupported() override;
    
    /**
     * See MetaStore::getNumFiles()
     **/
    unsigned long int getNumFiles() override;
    
    /**
     * See MetaStore::getNumFilesToRepair()
     **/
    unsigned long int getNumFilesToRepair() override;
    
    /**
     * See MetaStore::getFilesToRepair()
     **/
    int getFilesToRepair(int numFiles, File files[]) override;
    
    /**
     * See MetaStore::getFilesPendingWriteToCloud()
     **/
    int getFilesPendingWriteToCloud(int numFiles, File files[]) override;
    
    /**
     * See MetaStore::getNextFileForTaskCheck()
     **/
    bool getNextFileForTaskCheck(File &file) override;
    
    /**
     * See MetaStore::getFileJournal()
     **/
    void getFileJournal(const FileInfo &file, std::vector<std::tuple<Chunk, int, bool, bool>> &records) override;
    
    /**
     * See MetaStore::getFilesWithJounal()
     **/
    int getFilesWithJounal(FileInfo **list) override;
    
    /**
     * See MetaStore::fileHasJournal()
     **/
    bool fileHasJournal(const File &file) override;
    
    //------------------- 写操作重写 -------------------//
    
    /**
     * See MetaStore::putMeta()
     **/
    bool putMeta(const File &f) override;
    
    /**
     * See MetaStore::deleteMeta()
     **/
    bool deleteMeta(File &f) override;
    
    /**
     * See MetaStore::renameMeta()
     **/
    bool renameMeta(File &sf, File &df) override;
    
    /**
     * See MetaStore::updateTimestamps()
     **/
    bool updateTimestamps(const File &f) override;
    
    /**
     * See MetaStore::updateChunks()
     **/
    int updateChunks(const File &f, int version) override;
    
    /**
     * See MetaStore::markFileAsNeedsRepair()
     **/
    bool markFileAsNeedsRepair(const File &file) override;
    
    /**
     * See MetaStore::markFileAsRepaired()
     **/
    bool markFileAsRepaired(const File &file) override;
    
    /**
     * See MetaStore::markFileAsPendingWriteToCloud()
     **/
    bool markFileAsPendingWriteToCloud(const File &file) override;
    
    /**
     * See MetaStore::markFileAsWrittenToCloud()
     **/
    bool markFileAsWrittenToCloud(const File &file, bool removePending = false) override;
    
    /**
     * See MetaStore::updateFileStatus()
     **/
    bool updateFileStatus(const File &file) override;
    
    /**
     * See MetaStore::lockFile()
     **/
    bool lockFile(const File &file) override;
    
    /**
     * See MetaStore::unlockFile()
     **/
    bool unlockFile(const File &file) override;
    
    /**
     * See MetaStore::addChunkToJournal()
     **/
    bool addChunkToJournal(const File &file, const Chunk &chunk, int containerId, bool isWrite) override;
    
    /**
     * See MetaStore::updateChunkInJournal()
     **/
    bool updateChunkInJournal(const File &file, const Chunk &chunk, bool isWrite, bool deleteRecord, int containerId) override;

protected:
    /**
     * Reconnect to the Redis instance
     **/
    void reconnect() override;
    
    /**
     * Get a Redis context for read operations
     *
     * @return Redis context for read operations
     **/
    redisContext* getReadContext();
    
    /**
     * Get a Redis context for write operations
     *
     * @return Redis context for write operations
     **/
    redisContext* getWriteContext();

private:
    std::unique_ptr<SentinelClient> _sentinel_client;    /**< Sentinel client for Redis node monitoring */
    std::unique_ptr<ConnectionPool> _connection_pool;    /**< Connection pool for Redis connections */
    std::string _master_name;                            /**< Redis master name in Sentinel */
    std::vector<std::pair<std::string, int>> _sentinels; /**< List of Sentinel hosts */
    std::string _auth_pass;                              /**< Redis auth password(optional) */
    
};

#endif // __REDIS_SENTINEL_METASTORE_HH__