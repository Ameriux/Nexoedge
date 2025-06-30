// SPDX-License-Identifier: Apache-2.0

#ifndef _CONNECTION_POOL_HH
#define _CONNECTION_POOL_HH

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <hiredis/hiredis.h>
#include "sentinel_client.hh"

class ConnectionPool {
public:
    ConnectionPool();
    ~ConnectionPool();

    /**
     * Initialize the connection pool
     * 
     * @param[in] sentinel_client   Sentinel client for monitoring Redis nodes
     * @param[in] auth_pass         Redis auth password (if required)
     * @param[in] connection_timeout_ms  Connection timeout in milliseconds
     * @return whether initialization was successful
     */
    bool Initialize(SentinelClient* sentinel_client, 
                   const std::string& auth_pass = "", 
                   int connection_timeout_ms = 3000);

    /**
     * Get a connection for write operations (always to master)
     * 
     * @return redis context for write operations
     */
    redisContext* GetWriteConnection();

    /**
     * Get a connection for read operations (from master or slaves)
     * 
     * @param[in] prefer_slave    Whether to prefer slave nodes for read (true) or not (false)
     * @return redis context for read operations
     */
    redisContext* GetReadConnection(bool prefer_slave = true);

    /**
     * Force refresh all connections in the pool
     * 
     * @return whether refresh was successful
     */
    bool RefreshConnections();

    /**
     * Check health of all connections
     * 
     * @return number of healthy connections
     */
    int CheckConnectionsHealth();

private:
    // Connection management
    redisContext* CreateConnection(const std::string& host, int port);
    bool IsConnectionHealthy(redisContext* context);

    // Handle node status changes
    void HandleMasterSwitch(const RedisNodeInfo& new_master);
    void HandleSlaveStatusChange(const std::vector<RedisNodeInfo>& slaves);
    
    // Load balancing for read connections
    redisContext* GetNextReadConnection();
    
    // Private members
    SentinelClient* _sentinel_client;               /**< Pointer to sentinel client for monitoring Redis nodes */
    std::string _auth_pass;                         /**< Redis authentication password if required */
    int _connection_timeout_ms;                     /**< Connection timeout in milliseconds */
    
    // Connection storage
    redisContext* _master_connection;               /**< Connection to the master Redis node */
    std::vector<redisContext*> _slave_connections;  /**< Connections to slave Redis nodes */
    
    // Synchronization
    std::mutex _master_mutex;                       /**< Mutex for thread-safe master connection access */
    std::mutex _slaves_mutex;                       /**< Mutex for thread-safe slave connections access */
    
    // Load balancing state
    size_t _next_slave_index;                       /**< Index of next slave to use for read operations */
};

#endif