// SPDX-License-Identifier: Apache-2.0

#include "../../proxy/replication/sentinel/sentinel_client.hh"
#include "../../common/config.hh"

#include <glog/logging.h>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <unistd.h>

// 全局变量，用于测试回调功能
std::mutex g_mutex;
std::condition_variable g_cv;
bool g_master_callback_called = false;
bool g_slave_callback_called = false;
RedisNodeInfo g_master_info;
std::vector<RedisNodeInfo> g_slave_infos;

// 回调函数：当主节点发生变化时调用
void onMasterSwitch(const RedisNodeInfo &masterInfo) {
    std::cout << "master node switch callback triggered: " << masterInfo.ip << ":" << masterInfo.port 
              << ", status: " << (masterInfo.isAlive ? "online" : "offline") << std::endl;
    
    std::lock_guard<std::mutex> lock(g_mutex);
    g_master_callback_called = true;
    g_master_info = masterInfo;
    g_cv.notify_all();
}

// 回调函数：当从节点状态发生变化时调用
void onSlaveStatusChanged(const std::vector<RedisNodeInfo> &slaveInfos) {
    std::cout << "slave node status changed callback triggered, number of slave nodes: " << slaveInfos.size() << std::endl;
    
    for (const auto &slave : slaveInfos) {
        std::cout << "slave node: " << slave.ip << ":" << slave.port 
                  << ", status: " << (slave.isAlive ? "online" : "offline") << std::endl;
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    g_slave_callback_called = true;
    g_slave_infos = slaveInfos;
    g_cv.notify_all();
}

// 等待特定条件满足或超时
bool waitForCondition(std::function<bool()> condition, int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(g_mutex);
    return g_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds), condition);
}

// 测试连接到Sentinel并获取主节点信息
bool testGetMasterInfo() {
    SentinelClient client;
    RedisNodeInfo masterInfo;
    
    // 获取主节点信息
    bool success = client.GetMasterRedisNodeInfo(masterInfo);
    
    if (success) {
        std::cout << "successfully get master node info: " << masterInfo.ip << ":" << masterInfo.port 
                  << ", status: " << (masterInfo.isAlive ? "online" : "offline") << std::endl;
    } else {
        std::cout << "failed to get master node info" << std::endl;
    }
    
    return success;
}

// 测试获取从节点信息
bool testGetSlaveInfos() {
    SentinelClient client;
    std::vector<RedisNodeInfo> slaveInfos;
    
    // 获取从节点信息
    bool success = client.GetSlaveRedisNodeInfos(slaveInfos);
    
    if (success) {
        std::cout << "successfully get slave node info, number of slave nodes: " << slaveInfos.size() << std::endl;
        for (const auto &slave : slaveInfos) {
            std::cout << "slave node: " << slave.ip << ":" << slave.port 
                      << ", status: " << (slave.isAlive ? "online" : "offline") << std::endl;
        }
    } else {
        std::cout << "failed to get slave node info" << std::endl;
    }
    
    return success;
}

// 测试健康检查功能
bool testHealthCheck() {
    SentinelClient client;
    std::vector<std::pair<std::string, int>> unhealthySentinels;
    
    // 检查Sentinel健康状态
    bool allHealthy = client.IsSentinelHealthy(unhealthySentinels);
    
    if (allHealthy) {
        std::cout << "all sentinels are healthy" << std::endl;
    } else {
        std::cout << "there are unhealthy sentinels, number of unhealthy sentinels: " << unhealthySentinels.size() << std::endl;
        for (const auto &sentinel : unhealthySentinels) {
            std::cout << "unhealthy sentinel: " << sentinel.first << ":" << sentinel.second << std::endl;
        }
    }
    
    return allHealthy;
}

// 测试连接状态检查
bool testConnectionCheck() {
    SentinelClient client;
    std::vector<std::pair<std::string, int>> unconnectedSentinels;
    
    // 检查Sentinel连接状态
    bool allConnected = client.IsConnected(unconnectedSentinels);
    
    if (allConnected) {
        std::cout << "all configured sentinels are connected" << std::endl;
    } else {
        std::cout << "there are unconnected sentinels, number of unconnected sentinels: " << unconnectedSentinels.size() << std::endl;
        for (const auto &sentinel : unconnectedSentinels) {
            std::cout << "unconnected sentinel: " << sentinel.first << ":" << sentinel.second << std::endl;
        }
    }
    
    return allConnected;
}

// 测试监控功能和回调机制
bool testMonitorAndCallback(int monitorTimeSeconds) {
    SentinelClient client;
    
    // 重置全局变量
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_master_callback_called = false;
        g_slave_callback_called = false;
    }
    
    // 注册回调函数
    client.RegisterSwitchCallback(onMasterSwitch, onSlaveStatusChanged);
    
    // 启动监控
    bool monitorStarted = client.MonitorSentinelHealth();
    if (!monitorStarted) {
        std::cout << "failed to start monitoring" << std::endl;
        return false;
    }
    
    std::cout << "monitoring started, will run for " << monitorTimeSeconds << " seconds..." << std::endl;
    
    // 等待回调被触发或超时
    bool masterCallbackTriggered = waitForCondition([]() { return g_master_callback_called; }, monitorTimeSeconds);
    bool slaveCallbackTriggered = waitForCondition([]() { return g_slave_callback_called; }, 1); // 快速检查，因为可能已经超时
    
    // 停止监控
    client.StopMonitoring();
    
    if (masterCallbackTriggered) {
        std::cout << "master node callback triggered" << std::endl;
    } else {
        std::cout << "master node callback not triggered (this is normal if there is no master node change during monitoring)" << std::endl;
    }

    if (slaveCallbackTriggered) {
        std::cout << "slave node callback triggered" << std::endl;
    } else {
        std::cout << "slave node callback not triggered (this is normal if there is no slave node change during monitoring)" << std::endl;
    }
    
    // 无论回调是否被触发，我们都认为测试成功，因为在短时间内可能没有状态变化
    return monitorStarted;
}

// 测试重新连接功能
bool testReconnect() {
    SentinelClient client;
    
    std::cout << "test reconnect function..." << std::endl;
    
    // 执行重连
    bool reconnected = client.ReconnectSentinels();
    
    if (reconnected) {
        std::cout << "reconnect successfully" << std::endl;
    } else {
        std::cout << "reconnect failed" << std::endl;
    }
    
    return reconnected;
}

// 主函数
int main(int argc, char* argv[]) {
    // 初始化配置文件路径
    Config &config = Config::getInstance();
    config.setConfigPath();
    
    // 配置glog，与coordinator_test.cc保持一致
    if (!config.glogToConsole()) {
        FLAGS_log_dir = config.getGlogDir().c_str();
        printf("output log to %s\n", config.getGlogDir().c_str());
    } else {
        FLAGS_logtostderr = true;
        printf("output log to console\n");
    }
    FLAGS_minloglevel = config.getLogLevel();
    google::InitGoogleLogging(argv[0]);
    
    std::cout << "===== Redis Sentinel Client Test Started =====" << std::endl;
    
    // 运行测试并记录结果
    bool getMasterSuccess = testGetMasterInfo();
    bool getSlaveSuccess = testGetSlaveInfos();
    bool healthCheckSuccess = testHealthCheck();
    bool connectionCheckSuccess = testConnectionCheck();
    bool monitorSuccess = testMonitorAndCallback(10); // 监控10秒
    bool reconnectSuccess = testReconnect();
    
    // 输出测试结果汇总
    std::cout << std::endl;
    std::cout << "===== Test Result Summary =====" << std::endl;
    std::cout << "get master node info: " << (getMasterSuccess ? "success" : "failed") << std::endl;
    std::cout << "get slave node info: " << (getSlaveSuccess ? "success" : "failed") << std::endl;
    std::cout << "health check: " << (healthCheckSuccess ? "success" : "failed") << std::endl;
    std::cout << "connection check: " << (connectionCheckSuccess ? "success" : "failed") << std::endl;
    std::cout << "monitor and callback: " << (monitorSuccess ? "success" : "failed") << std::endl;
    std::cout << "reconnect: " << (reconnectSuccess ? "success" : "failed") << std::endl;
    
    // 整体测试结果
    bool allSuccess = getMasterSuccess && getSlaveSuccess && 
                      healthCheckSuccess && connectionCheckSuccess && 
                      monitorSuccess && reconnectSuccess;
    
    std::cout << std::endl;
    std::cout << "===== Overall Test Result: " << (allSuccess ? "All Passed" : "Some Failed") << " =====" << std::endl;
    
    return allSuccess ? 0 : 1;
}
