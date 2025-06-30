// SPDX-License-Identifier: Apache-2.0

#ifndef _SENTINEL_CLIENT_CC
#define _SENTINEL_CLIENT_CC

#include "sentinel_client.hh"

SentinelClient::SentinelClient() {
    Config &config = Config::getInstance();

    // initialize a connection to Sentinel
    _sentinel_contexts.clear();
    for (int i = 0; i < config.getProxyReplicationNumSentinels(); i++) {
        std::string ip = config.getProxyReplicationSentinelsContext()[i].first;
        int port = config.getProxyReplicationSentinelsContext()[i].second;
        
    }
}

SentinelClient::~SentinelClient() {
    if (_is_monitoring) {
        StopMonitoring();
        
        // wait for the monitoring thread to finish
        if (_monitor_thread) {
            pthread_join(_monitor_thread, NULL);
            _monitor_thread = 0;
        }
    }

    for (auto ctx : _sentinel_contexts) {
        redisFree(ctx);
    }
    _sentinel_contexts.clear();
    
    _master_switch_cb = nullptr;
    _slave_status_cb = nullptr;
}

bool SentinelClient::InitSentinelClient(const std::vector<std::pair<std::string, int>> &sentinels, const std::string &master_name) {{

}

#endif // _SENTINEL_CLIENT_CC