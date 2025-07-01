// SPDX-License-Identifier: Apache-2.0

#ifndef _SENTINEL_CLIENT_CC
#define _SENTINEL_CLIENT_CC

#include "sentinel_client.hh"
#include "../../../common/config.hh"

#include <cstring>
#include <unistd.h>
#include <glog/logging.h>

SentinelClient::SentinelClient() {
    Config &config = Config::getInstance();
    
    // initialize member variables
    _is_monitoring = false;
    _master_switch_cb = nullptr;
    _slave_status_cb = nullptr;
    _monitor_thread = 0;
    
    _master_name = config.getProxyReplicationMasterName();
    
    auto sentinels = config.getProxyReplicationSentinelsContext();
    for (const auto& sentinel : sentinels) {
        // connection to Sentinel
        redisContext* ctx = redisConnect(sentinel.first.c_str(), sentinel.second);
        if (ctx == NULL || ctx->err) {
            if (ctx) {
                LOG(ERROR) << "failed to connect Sentinel: " << ctx->errstr << " (" 
                          << sentinel.first << ":" << sentinel.second << ")";
                redisFree(ctx);
            } else {
                LOG(ERROR) << "failed to allocate Sentinel connection context";
            }
            continue;
        }
        
        // 连接成功，添加到连接列表
        _sentinel_contexts.push_back(ctx);
        LOG(INFO) << "successfully connected Sentinel: " << sentinel.first << ":" << sentinel.second;
    }
    
    // 检查是否至少有一个Sentinel连接成功
    if (_sentinel_contexts.empty()) {
        LOG(ERROR) << "all Sentinel connections failed, SentinelClient may not work properly";
    } else {
        LOG(INFO) << "successfully connected to " << _sentinel_contexts.size() << " Sentinel instances";
    }
    
    // initialize master and slave node info
    // at this time, we don't get any info, we will get it when we call the corresponding method
}

SentinelClient::~SentinelClient() {
    // stop monitoring thread
    StopMonitoring();
    
    // release all Redis connections
    for (auto ctx : _sentinel_contexts) {
        redisFree(ctx);
    }
    _sentinel_contexts.clear();
}

bool SentinelClient::GetMasterRedisNodeInfo(RedisNodeInfo &masterInfo) {
    std::lock_guard<std::mutex> lock(_sentinel_mutex);
    
    // if there is no available Sentinel connection
    if (_sentinel_contexts.empty()) {
        LOG(ERROR) << "NO available Sentinel connection!";
        return false;
    }
    
    // 遍历所有Sentinel连接，尝试获取主节点信息
    for (auto ctx : _sentinel_contexts) {
        // 发送master命令给Sentinel
        redisReply *reply = (redisReply*)redisCommand(
            ctx,
            "SENTINEL master %s",
            _master_name.c_str()
        );
        
        // 检查返回是否成功
        if (reply == NULL) {
            LOG(WARNING) << "failed to get master node info from Sentinel: connection error";
            continue;
        }
        
        // 解析回复
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 10) {
            // 在数组中搜索ip和port
            for (size_t i = 0; i < reply->elements; i += 2) {
                // 查找ip字段
                if (strcmp(reply->element[i]->str, "ip") == 0 && i+1 < reply->elements) {
                    masterInfo.ip = reply->element[i+1]->str;
                }
                // 查找port字段
                else if (strcmp(reply->element[i]->str, "port") == 0 && i+1 < reply->elements) {
                    masterInfo.port = atoi(reply->element[i+1]->str);
                }
                // 查找flags字段，判断节点状态
                else if (strcmp(reply->element[i]->str, "flags") == 0 && i+1 < reply->elements) {
                    std::string flags = reply->element[i+1]->str;
                    masterInfo.isAlive = (flags.find("s_down") == std::string::npos && 
                                         flags.find("o_down") == std::string::npos);
                }
            }
            
            // 设置为主节点
            masterInfo.isMaster = true;
            
            // 更新缓存的主节点信息
            _current_master = masterInfo;
            
            freeReplyObject(reply);
            return true;
        }
        
        // 释放回复对象
        freeReplyObject(reply);
    }
    
    // 所有尝试都失败
    LOG(ERROR) << "failed to get master node info from any Sentinel";
    return false;
}

bool SentinelClient::GetSlaveRedisNodeInfos(std::vector<RedisNodeInfo> &slaveInfos) {
    std::lock_guard<std::mutex> lock(_sentinel_mutex);
    
    // 清空现有列表
    slaveInfos.clear();
    
    // 如果没有可用的Sentinel连接
    if (_sentinel_contexts.empty()) {
        LOG(ERROR) << "NO available Sentinel connection!";
        return false;
    }
    
    // 遍历所有Sentinel连接，尝试获取从节点信息
    for (auto ctx : _sentinel_contexts) {
        // 发送slaves命令给Sentinel
        redisReply *reply = (redisReply*)redisCommand(
            ctx,
            "SENTINEL slaves %s",
            _master_name.c_str()
        );
        
        // 检查返回是否成功
        if (reply == NULL) {
            LOG(WARNING) << "failed to get slave node info from Sentinel: connection error";
            continue;
        }
        
        // 解析回复
        if (reply->type == REDIS_REPLY_ARRAY) {
            // 清空缓存的从节点信息
            _current_slaves.clear();
            
            // 遍历每个从节点
            for (size_t i = 0; i < reply->elements; i++) {
                RedisNodeInfo slaveInfo;
                
                // 每个从节点信息是一个数组
                redisReply *slaveReply = reply->element[i];
                if (slaveReply->type != REDIS_REPLY_ARRAY) {
                    continue;
                }
                
                // 解析从节点信息
                for (size_t j = 0; j < slaveReply->elements; j += 2) {
                    // 查找ip字段
                    if (j+1 < slaveReply->elements && 
                        strcmp(slaveReply->element[j]->str, "ip") == 0) {
                        slaveInfo.ip = slaveReply->element[j+1]->str;
                    }
                    // 查找port字段
                    else if (j+1 < slaveReply->elements && 
                             strcmp(slaveReply->element[j]->str, "port") == 0) {
                        slaveInfo.port = atoi(slaveReply->element[j+1]->str);
                    }
                    // 查找flags字段，判断节点状态
                    else if (j+1 < slaveReply->elements && 
                             strcmp(slaveReply->element[j]->str, "flags") == 0) {
                        std::string flags = slaveReply->element[j+1]->str;
                        slaveInfo.isAlive = (flags.find("s_down") == std::string::npos && 
                                           flags.find("o_down") == std::string::npos);
                    }
                }
                
                // 设置为从节点
                slaveInfo.isMaster = false;
                
                // 添加到从节点列表和缓存
                slaveInfos.push_back(slaveInfo);
                _current_slaves.push_back(slaveInfo);
            }
            
            freeReplyObject(reply);
            return true;
        }
        
        // 释放回复对象
        freeReplyObject(reply);
    }
    
    // 所有尝试都失败
    LOG(ERROR) << "failed to get slave node info from any Sentinel";
    return false;
}

void SentinelClient::RegisterSwitchCallback(
    std::function<void(const RedisNodeInfo &)> master_switch_callback, 
    std::function<void(const std::vector<RedisNodeInfo> &)> slave_status_callback
) {
    std::lock_guard<std::mutex> lock(_callback_mutex);
    
    _master_switch_cb = master_switch_callback;
    _slave_status_cb = slave_status_callback;
    
    LOG(INFO) << "registered callback function: " 
              << (master_switch_callback ? "master node switch " : "") 
              << (slave_status_callback ? "slave node status change" : "");
}

bool SentinelClient::MonitorSentinelHealth() {
    std::lock_guard<std::mutex> lock(_sentinel_mutex);
    
    // 检查是否已经在监控中
    if (_is_monitoring) {
        LOG(WARNING) << "monitoring thread is already running";
        return true;
    }
    
    // 检查是否有可用的Sentinel连接
    if (_sentinel_contexts.empty()) {
        LOG(ERROR) << "NO available Sentinel connection, cannot start monitoring";
        return false;
    }
    
    // 启动监控线程
    _is_monitoring = true;
    if (pthread_create(&_monitor_thread, NULL, &SentinelClient::MonitorThread, this) != 0) {
        LOG(ERROR) << "failed to create monitoring thread";
        _is_monitoring = false;
        return false;
    }
    
    LOG(INFO) << "successfully started Sentinel monitoring thread";
    return true;
}

void SentinelClient::StopMonitoring() {
    // 使用局部作用域以减少锁的持有时间
    {
        std::lock_guard<std::mutex> lock(_sentinel_mutex);
        if (!_is_monitoring) {
            return;  // 已经停止了
        }
        _is_monitoring = false;
    }
    
    // 等待线程终止
    if (_monitor_thread != 0) {
        pthread_join(_monitor_thread, NULL);
        _monitor_thread = 0;
        LOG(INFO) << "Sentinel monitoring thread stopped";
    }
}

bool SentinelClient::IsSentinelHealthy(std::vector<std::pair<std::string, int>> &unhealthy_sentinels) {
    std::lock_guard<std::mutex> lock(_sentinel_mutex);
    
    // 清空列表
    unhealthy_sentinels.clear();
    
    // 如果没有Sentinel连接，直接返回不健康
    if (_sentinel_contexts.empty()) {
        LOG(ERROR) << "NO available Sentinel connection";
        return false;
    }
    
    bool allHealthy = true;
    
    // 检查每个Sentinel实例
    for (auto ctx : _sentinel_contexts) {
        redisReply *reply = (redisReply*)redisCommand(ctx, "PING");
        
        // 检查是否能够成功Ping通
        bool healthy = (reply != NULL && 
                      (reply->type == REDIS_REPLY_STATUS || reply->type == REDIS_REPLY_STRING) &&
                      strcmp(reply->str, "PONG") == 0);
        
        if (!healthy) {
            // 添加到不健康列表
            char ip[256];
            int port = 0;
            
            if (ctx->tcp.host) {
                strncpy(ip, ctx->tcp.host, sizeof(ip));
                ip[sizeof(ip)-1] = '\0';
                port = ctx->tcp.port;
                
                unhealthy_sentinels.push_back(std::make_pair(std::string(ip), port));
                allHealthy = false;
                
                LOG(ERROR) << "Sentinel instance is unhealthy: " << ip << ":" << port;
            }
        }
        
        if (reply) {
            freeReplyObject(reply);
        }
    }
    
    return allHealthy;
}

bool SentinelClient::IsConnected(std::vector<std::pair<std::string, int>> &unconnected_sentinels) {
    std::lock_guard<std::mutex> lock(_sentinel_mutex);
    
    // 清空列表
    unconnected_sentinels.clear();
    
    // 获取所有配置的Sentinel
    Config &config = Config::getInstance();
    auto configuredSentinels = config.getProxyReplicationSentinelsContext();
    
    // 如果没有配置Sentinel，返回未连接
    if (configuredSentinels.empty()) {
        return false;
    }
    
    // 检查每个配置的Sentinel是否已连接
    for (const auto& sentinel : configuredSentinels) {
        bool connected = false;
        
        // 在现有连接中查找
        for (auto ctx : _sentinel_contexts) {
            if (ctx->tcp.host && 
                strcmp(ctx->tcp.host, sentinel.first.c_str()) == 0 && 
                ctx->tcp.port == sentinel.second) {
                connected = true;
                break;
            }
        }
        
        // 如果未连接，添加到列表
        if (!connected) {
            unconnected_sentinels.push_back(sentinel);
        }
    }
    
    return unconnected_sentinels.empty();
}

bool SentinelClient::ReconnectSentinels() {
    std::lock_guard<std::mutex> lock(_sentinel_mutex);
    
    // 释放所有现有连接
    for (auto ctx : _sentinel_contexts) {
        redisFree(ctx);
    }
    _sentinel_contexts.clear();
    
    // 重新连接所有Sentinel
    Config &config = Config::getInstance();
    auto sentinels = config.getProxyReplicationSentinelsContext();
    for (const auto& sentinel : sentinels) {
        // 创建与Sentinel实例的连接
        redisContext* ctx = redisConnect(sentinel.first.c_str(), sentinel.second);
        if (ctx == NULL || ctx->err) {
            if (ctx) {
                LOG(ERROR) << "failed to reconnect Sentinel: " << ctx->errstr << " (" 
                          << sentinel.first << ":" << sentinel.second << ")";
                redisFree(ctx);
            } else {
                LOG(ERROR) << "failed to allocate Sentinel connection context";
            }
            continue;
        }
        
        // 连接成功，添加到连接列表
        _sentinel_contexts.push_back(ctx);
        LOG(INFO) << "successfully reconnected Sentinel: " << sentinel.first << ":" << sentinel.second;
    }
    
    // 检查是否至少有一个Sentinel连接成功
    return !_sentinel_contexts.empty();
}

void* SentinelClient::MonitorThread(void* arg) {
    SentinelClient* client = static_cast<SentinelClient*>(arg);
    
    LOG(INFO) << "Sentinel monitoring thread started";
    
    while (true) {
        // 检查是否应该停止
        {
            std::lock_guard<std::mutex> lock(client->_sentinel_mutex);
            if (!client->_is_monitoring) {
                break;
            }
        }
        
        // 获取当前主节点信息
        RedisNodeInfo currentMaster;
        bool masterChanged = false;
        if (client->GetMasterRedisNodeInfo(currentMaster)) {
            // 检查主节点是否变化
            std::lock_guard<std::mutex> lock(client->_sentinel_mutex);
            if (client->_current_master.ip != currentMaster.ip || 
                client->_current_master.port != currentMaster.port) {
                masterChanged = true;
                client->_current_master = currentMaster;
            }
        }
        
        // 获取当前从节点信息
        std::vector<RedisNodeInfo> currentSlaves;
        bool slavesChanged = false;
        if (client->GetSlaveRedisNodeInfos(currentSlaves)) {
            // 检查从节点是否变化
            std::lock_guard<std::mutex> lock(client->_sentinel_mutex);
            if (client->_current_slaves.size() != currentSlaves.size()) {
                slavesChanged = true;
            } else {
                // 比较每个从节点
                for (size_t i = 0; i < currentSlaves.size(); ++i) {
                    if (i >= client->_current_slaves.size() || 
                        client->_current_slaves[i].ip != currentSlaves[i].ip ||
                        client->_current_slaves[i].port != currentSlaves[i].port ||
                        client->_current_slaves[i].isAlive != currentSlaves[i].isAlive) {
                        slavesChanged = true;
                        break;
                    }
                }
            }
            
            if (slavesChanged) {
                client->_current_slaves = currentSlaves;
            }
        }
        
        // 执行回调
        {
            std::lock_guard<std::mutex> lock(client->_callback_mutex);
            if (masterChanged && client->_master_switch_cb) {
                client->_master_switch_cb(client->_current_master);
                LOG(INFO) << "master node switch event triggered: " << client->_current_master.ip << ":" << client->_current_master.port;
            }
            
            if (slavesChanged && client->_slave_status_cb) {
                client->_slave_status_cb(client->_current_slaves);
                LOG(INFO) << "slave node status change event triggered: " << client->_current_slaves.size() << " slave nodes";
            }
        }
        
        // 睡眠，避免频繁查询
        sleep(2);
    }
    
    LOG(INFO) << "Sentinel monitoring thread exited normally";
    return NULL;
}

#endif // _SENTINEL_CLIENT_CC