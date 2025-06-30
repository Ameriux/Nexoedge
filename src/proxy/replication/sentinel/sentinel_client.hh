// SPDX-License-Identifier: Apache-2.0

#ifndef _SENTINEL_CLIENT_HH
#define _SENTINEL_CLIENT_HH

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <hiredis/hiredis.h>

struct RedisNodeInfo {
    std::string ip;     /**< IP address of Redis node */
    int port;           /**< Port of Redis node */
    bool isAlive;       /**< Whether the Redis node is alive */
    bool isMaster;      /**< Whether the Redis node is master */
};

class SentinelClient {
public:
    SentinelClient();
    ~SentinelClient();

    /**
     * Get the master redis node info
     *
     * @param[out] masterInfo    master redis node info
     * @return whether the operation is sucessful
     **/
    bool GetMasterRedisNodeInfo(RedisNodeInfo &masterInfo);

    /**
     * Get the slave redis node info
     *
     * @param[out] slaveInfos    slave redis nodes info
     * @return whether the operation is sucessful
     **/
    bool GetSlaveRedisNodeInfos(std::vector<RedisNodeInfo> &slaveInfos);

    /**
     * Register switch callback
     *
     * @param[in] callback    callback function
     **/
    void RegisterSwitchCallback(std::function<void(const RedisNodeInfo &masterInfo)> master_switch_callback, std::function<void(const std::vector<RedisNodeInfo> &slaveInfos)> slave_status_callback = nullptr);

    /**
     * Monitoring the Redis node status
     *
     * @return whether the monitoring is sucessfully started
     **/
    bool MonitorSentinelHealth();

    /**
     * Stop monitoring the Redis node status
     *
     **/
    void StopMonitoring();

    /**
     * Check if the Sentinel is healthy
     *
     * @param[out] unhealthy_sentinels   Sentinel instances that are unhealthy
     * @return whether the Sentinel is healthy
     **/
    bool IsSentinelHealthy(std::vector<std::pair<std::string, int>> &unhealthy_sentinels);

    /**
     * Check if the Sentinel is connected
     *
     * @param[out] unconnected_sentinels   Sentinel instances that are not connected
     * @return whether the Sentinel is connected
     **/
    bool IsConnected(std::vector<std::pair<std::string, int>> &unconnected_sentinels);

    /**
     * Reconnect to the Sentinel
     *
     * @return whether the reconnection is sucessful
     **/
    bool ReconnectSentinels();  

    // TODO: add password support in initialization？
    // TODO: add monitoring parameters configuration？

private:
    std::vector<redisContext*> _sentinel_contexts;                              /**< Connections to Sentinel instances */
    std::string _master_name;                                                   /**< Name of the monitored master in Sentinel */
    
    std::function<void(const RedisNodeInfo&)> _master_switch_cb;                /**< Callback for master switch events */
    std::function<void(const std::vector<RedisNodeInfo>&)> _slave_status_cb;    /**< Callback for slave status changes */
    
    pthread_t _monitor_thread;                                                  /**< Thread for monitoring Sentinel events */
    bool _is_monitoring;                                                        /**< Whether monitoring thread is active */
    
    std::mutex _sentinel_mutex;                                                 /**< Mutex for thread-safe sentinel access */
    std::mutex _callback_mutex;                                                 /**< Mutex for thread-safe callback execution */
    
    RedisNodeInfo _current_master;                                              /**< Cached information about current master */    
    std::vector<RedisNodeInfo> _current_slaves;                                 /**< Cached information about current slaves */
    
    /**
     * Monitor thread function
     * 
     * @param[in] arg     Pointer to SentinelClient instance
     * @return NULL
     */
    static void* MonitorThread(void* arg);
};

#endif