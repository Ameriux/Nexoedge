// SPDX-License-Identifier: Apache-2.0

#include <sys/stat.h> // mkdir()
#include <sys/types.h> // mkdir()
#include <arpa/inet.h> // inet_pton(), inet_ntop(), htonl()
#include <string> // stoi

#include <boost/algorithm/string/case_conv.hpp>

#include <glog/logging.h>
#include <linux/limits.h>

#include "config.hh"

// see ContainerType in common/define.hh
const char *Config::ContainerTypeName[] = {
    "FS",                  // 0
    "Alibaba",
    "AWS",
    "Azure",
    "Generic_S3",          // 5

    "Unknown"
};

const char *Config::LogLevelName[] = {
    "INFO",                // 0
    "WARNING",
    "ERROR",
    "FATAL",

    "Unknown"              // 5
};

// see DistributionPolicy in common/define.hh
const char *Config::DistributionPolicyName[] = {
    "Static",              // 0
    "Round-Robin",
    "Least-Used",

    "Unknown"
};

// see ChunkScanSamplingPolicy in common/define.hh
const char *Config::ChunkScanSamplingPolicyName[] = {
    "None",               // 0
    "Chunk-level",
    "Stripe-level",
    "File-level",
    "Container-level",    // 5

    "Unknown"
};

// see MetaStore in common/define.hh
const char *Config::MetaStoreName[] = {
    "Redis",              // 0

    "Unknown"
};

void Config::setConfigPath (std::string dir) {
    char gpath[PATH_MAX], ppath[PATH_MAX], apath[PATH_MAX];
    const char *dirPath = dir.c_str();
    snprintf(gpath, PATH_MAX, "%s/%s", dirPath, "general.ini");
    snprintf(ppath, PATH_MAX, "%s/%s", dirPath, "proxy.ini");
    snprintf(apath, PATH_MAX, "%s/%s", dirPath, "agent.ini");
    setConfigPath(gpath, ppath, apath, dirPath);
}

void Config::setConfigPath (const char *generalPath, const char *proxyPath, const char *agentPath, const char *dirPath) {
    // read general config file, do not catch error here, since general.ini is required
    boost::property_tree::ini_parser::read_ini(generalPath, _generalPt);

    // read agent config file
    try {
        boost::property_tree::ini_parser::read_ini(agentPath, _agentPt);
    } catch (std::exception &e) {
        LOG(WARNING) << "Missing configuration file 'agent.ini' for Agent";
    }
    // read proxy config file
    try {
        boost::property_tree::ini_parser::read_ini(proxyPath, _proxyPt);
    } catch (std::exception &e) {
        LOG(ERROR) << "Missing configuration file 'proxy.ini' for Proxy";
    }

    // parse general properties
    if (!_generalPt.empty()) {
        // logging
        _general.glogToConsole = readBool(_generalPt, "log.glog_to_console");
        if (_general.glogToConsole == false) {
            _general.glogdir = readString(_generalPt, "log.glog_dir");
            if (_general.glogdir.empty()) {
                _general.glogdir = std::string("/tmp/ncloud_log");
            }
            // create the directory for glog first
            mkdir(_general.glogdir.c_str(), 0755);
        }
        _general.level = parseLogLevel(readString(_generalPt, "log.level"));
        if (_general.level < 0) { _general.level = google::GLOG_ERROR; }

        // retry
        _general.retry.num = readInt(_generalPt, "retry.num");
        _general.retry.intv = std::max(readInt(_generalPt, "retry.interval"), 0);

        // network
        const int curveKeySize = 41;
        _general.network.listenToAllInterfaces = readBool(_generalPt, "network.listen_all_ips");
        _general.network.tcpKeepAlive.enabled = readBool(_generalPt, "network.tcp_keep_alive");
        _general.network.tcpKeepAlive.idle = readInt(_generalPt, "network.tcp_keep_alive_idle");
        if (_general.network.tcpKeepAlive.idle <= 0)
            _general.network.tcpKeepAlive.idle = -1;
        _general.network.tcpKeepAlive.intv = readInt(_generalPt, "network.tcp_keep_alive_intv");
        if (_general.network.tcpKeepAlive.intv <= 0)
            _general.network.tcpKeepAlive.intv = -1;
        _general.network.tcpKeepAlive.cnt = readInt(_generalPt, "network.tcp_keep_alive_cnt");
        if (_general.network.tcpKeepAlive.cnt <= 0)
            _general.network.tcpKeepAlive.cnt = -1;
        _general.network.tcpBuffer = readInt(_generalPt, "network.tcp_buffer_size");
        if (_general.network.tcpBuffer < 0)
            _general.network.tcpBuffer = -1; 
        else if (_general.network.tcpBuffer < (1 << 20))
            _general.network.tcpBuffer = 1 << 20;
        _general.network.useCurve = readBool(_generalPt, "network.use_curve");
        _general.network.proxy.curvePublicKey = readBytesFromFile(readString(_generalPt, "network.proxy_curve_public_key_file"), curveKeySize);
        _general.network.proxy.curveSecretKey = readBytesFromFile(readString(_generalPt, "network.proxy_curve_secret_key_file"), curveKeySize);
        _general.network.agent.curvePublicKey = readBytesFromFile(readString(_generalPt, "network.agent_curve_public_key_file"), curveKeySize);
        _general.network.agent.curveSecretKey = readBytesFromFile(readString(_generalPt, "network.agent_curve_secret_key_file"), curveKeySize);
        if (_general.network.useCurve
            && (_general.network.proxy.curvePublicKey == nullptr
                || _general.network.agent.curvePublicKey == nullptr
            )
        ) {
            LOG(ERROR) << "Insufficient public keys for network communication.";
            exit(-1);
        }

        // data integrity
        _general.dataIntegrity.verifyChunkChecksum = readBool(_generalPt, "data_integrity.verify_chunk_checksum");

        // failure detection
        _general.failureDetection.timeout = std::max(readInt(_generalPt, "failure_detection.timeout"), 500);

        // event
        _general.event.eventProbeTimeout = std::max(readInt(_generalPt, "event.event_probe_timeout"), 2000);

        // benchmark
        _general.benchmark.stripeEnabled = readBool(_generalPt, "benchmark.stripe_enabled");

        // proxy hosts
        _proxy.numProxy = readInt(_generalPt, "proxy.num_proxy");
        if (_proxy.numProxy < 1 || _proxy.numProxy > MAX_NUM_PROXY) {
            LOG(ERROR) << "The number of proxy should be within 1 and " << MAX_NUM_PROXY;
            exit(-1);
        }
        char pname[32];
        for (int i = 0; i < _proxy.numProxy; i++) { 
            sprintf(pname, "proxy%02d.ip", i + 1);
            _proxy.addrs[i].ip = readString(_generalPt, pname);
            sprintf(pname, "proxy%02d.coord_port", i + 1);
            _proxy.addrs[i].cport = readInt(_generalPt, pname);
            if (_proxy.addrs[i].cport >= (1 << 16)) {
                LOG(ERROR) << "Port number for coordinator must be within 0 and 65535";
                exit(-1);
            }
        }
    }

    // parse agent properties
    if (!_agentPt.empty()) {
        // agent host
        _agent.ip = readString(_agentPt, "agent.ip");
        _agent.port = readInt(_agentPt, "agent.port");
        if (_agent.port >= (1 << 16)) {
            LOG(ERROR) << "Port number for chunk transfer must be within 0 and 65535";
            exit(-1);
        }
        _agent.cport = readInt(_agentPt, "agent.coord_port");
        if (_agent.cport >= (1 << 16)) {
            LOG(ERROR) << "Port number for coordinator must be within 0 and 65535";
            exit(-1);
        }
        // agent misc settings
        _agent.misc.numWorkers = readInt(_agentPt, "misc.num_workers");
        if (_agent.misc.numWorkers > MAX_NUM_WORKERS)
            _agent.misc.numWorkers = MAX_NUM_WORKERS;
        else if (_agent.misc.numWorkers < 1)
            _agent.misc.numWorkers = 1;
        _agent.misc.numZmqThread = readInt(_agentPt, "misc.zmq_thread");
        if (_agent.misc.numZmqThread < 1)
            _agent.misc.numZmqThread = 1;
        _agent.misc.copyBlockSize = readULL(_agentPt, "misc.copy_block_size");
        _agent.misc.flushOnClose = readBool(_agentPt, "misc.flush_on_close");
        _agent.misc.registerToProxy = readBool(_agentPt, "misc.register_to_proxy");
        // agent containers
        _agent.numContainers = readInt(_agentPt, "agent.num_containers");
        char pname[32];
        std::string containerTypeName;
        for (int i = 0; i < _agent.numContainers; i++) {
            sprintf(pname, "container%02d.id", i + 1);
            _agent.containers[i].id = readInt(_agentPt, pname);
            sprintf(pname, "container%02d.url", i + 1);
            _agent.containers[i].url = readString(_agentPt, pname);
            sprintf(pname, "container%02d.capacity", i + 1);
            _agent.containers[i].capacity = readULL(_agentPt, pname);
            sprintf(pname, "container%02d.type", i + 1);
            _agent.containers[i].type = parseContainerType(readString(_agentPt, pname));
            if (_agent.containers[i].type >= ContainerType::UNKNOWN_CONTAINER) {
                _agent.containers[i].type = ContainerType::FS_CONTAINER;
            }
            if (
                _agent.containers[i].type == ContainerType::AWS_CONTAINER ||
                _agent.containers[i].type == ContainerType::ALI_CONTAINER ||
                _agent.containers[i].type == ContainerType::GENERIC_S3_CONTAINER
            ) {
                sprintf(pname, "container%02d.region", i + 1);
                _agent.containers[i].region = readString(_agentPt, pname);
                sprintf(pname, "container%02d.key_id", i + 1);
                _agent.containers[i].keyId = readString(_agentPt, pname);
            }
            if (
                _agent.containers[i].type == ContainerType::AWS_CONTAINER ||
                _agent.containers[i].type == ContainerType::ALI_CONTAINER ||
                _agent.containers[i].type == ContainerType::AZURE_CONTAINER ||
                _agent.containers[i].type == ContainerType::GENERIC_S3_CONTAINER
            ) {
                sprintf(pname, "container%02d.key", i + 1);
                _agent.containers[i].key = readString(_agentPt, pname);
            }
            if (
                _agent.containers[i].type == ContainerType::AWS_CONTAINER ||
                _agent.containers[i].type == ContainerType::AZURE_CONTAINER ||
                _agent.containers[i].type == ContainerType::GENERIC_S3_CONTAINER
            ) {
                try {
                    sprintf(pname, "container%02d.http_proxy_ip", i + 1);
                    _agent.containers[i].httpProxy.ip = readString(_agentPt, pname);
                    sprintf(pname, "container%02d.http_proxy_port", i + 1);
                    _agent.containers[i].httpProxy.port = readInt(_agentPt, pname);
                } catch (std::exception &e) {
                    // no proxy provided
                    _agent.containers[i].httpProxy.ip = "";
                    _agent.containers[i].httpProxy.port = 0; 
                }
            }
            if (
                _agent.containers[i].type == ContainerType::GENERIC_S3_CONTAINER
            ) {
                sprintf(pname, "container%02d.endpoint", i + 1);
                _agent.containers[i].endpoint = readString(_agentPt, pname);
                try {
                    sprintf(pname, "container%02d.verify_ssl", i + 1);
                    _agent.containers[i].verifySSL = readBool(_agentPt, pname);
                } catch (std::exception &e) {
                    _agent.containers[i].verifySSL = false;
                }
            }
        }
    }

    // parse proxy properties
    if (!_proxyPt.empty()) {
        // TODO: proxy num, change it to start from 0 for easy array access
        _proxy.myProxyNum = readInt(_proxyPt, "proxy.num") - 1;
        if (_proxy.myProxyNum < 0 || _proxy.myProxyNum >= _proxy.numProxy) {
            LOG(ERROR) << "Proxy number (" << _proxy.myProxyNum + 1 << ") is out of range, should be within 1 and " << _proxy.numProxy; 
            exit(-1);
        }
        // namespace id
        _proxy.namespaceId = readInt(_proxyPt, "proxy.namespace_id");
        if (_proxy.namespaceId < 0 || _proxy.namespaceId >= INVALID_NAMESPACE_ID) {
            LOG(ERROR) << "Proxy namespace should be within 0 and " << (INVALID_NAMESPACE_ID - 1) << ", got " << _proxy.namespaceId;
            exit(-1);
        }
        // interface
        _proxy.interface = readString(_proxyPt, "proxy.interface");
        if (_proxy.interface != "zmq" && _proxy.interface != "redis" && _proxy.interface != "all" && _proxy.interface != "s3") {
            LOG(WARNING) << "Unknown interface type " << _proxy.interface;
            _proxy.interface = std::string("redis");
        }
        // storage class
        _proxy.storageClass.filePath = readString(_proxyPt, "storage_class.path");
        std::string scPath = _proxy.storageClass.filePath;
        if (_proxy.storageClass.filePath[0] != '/' && strcmp(dirPath, ".") != 0)
            scPath = std::string(dirPath).append("/").append(_proxy.storageClass.filePath);
        boost::property_tree::ini_parser::read_ini(scPath.c_str(), _storageClassPt);
        for (boost::property_tree::ptree::iterator it = _storageClassPt.begin(); it != _storageClassPt.end(); it++) {
            _proxy.storageClass.classes.insert(it->first);
            if (readBool(_storageClassPt, std::string(it->first).append(".default").c_str())) {
                if (_proxy.storageClass.defaultClass.empty()) {
                    _proxy.storageClass.defaultClass = it->first;
                } else {
                    LOG(ERROR) << "Only one default storage class is allowed.";
                    exit(-1);
                }
            }
        }
        // metastore
        _proxy.metastore.type = parseMetaStoreType(readString(_proxyPt, "metastore.type"));
        if (_proxy.metastore.type == MetaStoreType::UNKNOWN_METASTORE) {
            _proxy.metastore.type = MetaStoreType::REDIS;
        }
        switch (_proxy.metastore.type) {
        case MetaStoreType::REDIS:
            _proxy.metastore.redis.ip = readString(_proxyPt, "metastore.ip");
            _proxy.metastore.redis.port = readInt(_proxyPt, "metastore.port");
            if (_proxy.metastore.redis.port > (1 << 16)) {
                LOG(ERROR) << "Port number for metastore must be within 0 and 65536";
                exit(-1);
            }
            _proxy.metastore.redis.ssl.caCertPath = readString(_proxyPt, "metastore.ssl_ca_cert_path");
            _proxy.metastore.redis.ssl.trustedCertsDir = readString(_proxyPt, "metastore.ssl_trusted_certs_dir");
            _proxy.metastore.redis.ssl.clientCertPath = readString(_proxyPt, "metastore.ssl_client_cert_path");
            _proxy.metastore.redis.ssl.clientKeyPath = readString(_proxyPt, "metastore.ssl_client_key_path");
            _proxy.metastore.redis.ssl.domainName = readString(_proxyPt, "metastore.ssl_domain_name");
            break;
        default:
            break;
        }
        _proxy.metastore.redis.auth.user = readString(_proxyPt, "metastore.auth_user");
        _proxy.metastore.redis.auth.password = readString(_proxyPt, "metastore.auth_password");
        // auto recovery
        _proxy.recovery.enabled = readBool(_proxyPt, "recovery.trigger_enabled");
        _proxy.recovery.recoverIntv = std::max(readInt(_proxyPt, "recovery.trigger_start_interval"), 5);
        _proxy.recovery.scanIntv = std::max(readInt(_proxyPt, "recovery.scan_interval"), 5);
        _proxy.recovery.scanChunkIntv = std::max(readInt(_proxyPt, "recovery.scan_chunk_interval"), 0);
        _proxy.recovery.chunkBatchSize = std::max(readInt(_proxyPt, "recovery.scan_chunk_batch_size"), 1);
        _proxy.recovery.batchSize = std::max(readInt(_proxyPt, "recovery.batch_size"), 1);
        _proxy.recovery.chunkScanSampling.policy = parseChunkScanSamplingPolicy(readString(_proxyPt, "recovery.chunk_scan_sampling_policy"));
        if (_proxy.recovery.chunkScanSampling.policy >= ChunkScanSamplingPolicy::UNKNOWN_SAMPLING_POLICY)
            _proxy.recovery.chunkScanSampling.policy = ChunkScanSamplingPolicy::NONE_SAMPLING_POLICY;
        _proxy.recovery.chunkScanSampling.rate = std::min(readFloat(_proxyPt, "recovery.chunk_scan_sampling_rate"), 1.0);
        if (_proxy.recovery.chunkScanSampling.rate <= 0) {
            LOG(ERROR) << "Chunk scan sampling rate must be (0,1]";
            exit(-1);
        }
        // proxy misc settings
        _proxy.misc.numZmqThread = readInt(_proxyPt, "misc.zmq_thread");
        if (_proxy.misc.numZmqThread < 1)
            _proxy.misc.numZmqThread = 1;
        _proxy.misc.repairAtProxy = readBool(_proxyPt, "misc.repair_at_proxy");
        _proxy.misc.repairUsingCAR = readBool(_proxyPt, "misc.repair_using_car");
        _proxy.misc.overwriteFiles = readBool(_proxyPt, "misc.overwrite_files");
        _proxy.misc.reuseDataConn = readBool(_proxyPt, "misc.reuse_data_connection");
        _proxy.misc.livenessCacheTime = std::max(readInt(_proxyPt, "misc.liveness_cache_time"), 0);
        _proxy.misc.scanJournalIntv = readInt(_proxyPt, "misc.journal_check_interval");
        if (_proxy.misc.scanJournalIntv > 0 && _proxy.misc.scanJournalIntv < 30)
            _proxy.misc.scanJournalIntv = 30;
        // agent list
        boost::property_tree::ptree agentListPt;
        try {
            std::string agentListPath = readString(_proxyPt, "misc.agent_list");
            if (agentListPath[0] != '/' && strcmp(dirPath, ".") != 0)
                agentListPath = std::string(dirPath).append("/").append(agentListPath);
            boost::property_tree::ini_parser::read_ini(agentListPath.c_str(), agentListPt);
            for (boost::property_tree::ptree::iterator it = agentListPt.begin(); it != agentListPt.end(); it++) {
                std::string ip = readString(agentListPt, std::string(it->first).append(".ip").c_str());
                unsigned short port = readInt(agentListPt, std::string(it->first).append(".port").c_str());
                if (port == 0) continue;
                _proxy.misc.agentList.emplace_back(std::make_pair(ip, port));
            }
        } catch (std::exception &e) {
        }
        // proxy data distribution settings
        _proxy.dataDistribution.policy = parseDistributionPolicy(readString(_proxyPt, "data_distribution.policy"));
        if (_proxy.dataDistribution.policy >= DistributionPolicy::UNKNOWN_DIST_POLICY)
            _proxy.dataDistribution.policy = 0;
        std::string ranges = readString(_proxyPt, "data_distribution.near_ip_ranges");
        _proxy.dataDistribution.numNearIpRanges = 0;
        for (size_t idx = 0; idx < ranges.size();) {
            // find the network mask length
            size_t slash = ranges.find('/', idx);
            if (slash == std::string::npos)
                break;
            size_t end = ranges.find(' ', slash);
            if (end == std::string::npos)
                end = ranges.size();
            int maskLength = std::stoi(ranges.c_str() + slash + 1);
            // convert the ip
            int ret = inet_pton(AF_INET, ranges.substr(idx, slash - idx).c_str(), &_proxy.dataDistribution.nearIpRanges[_proxy.dataDistribution.numNearIpRanges]);
            // advance idx to next ip string
            idx = end + 1;
            // skip if failed to parse the range, or network mask is incorrect
            if (ret != 1 || maskLength > 32)
                continue;
            // mask the network address
            _proxy.dataDistribution.nearIpRangeMasks[_proxy.dataDistribution.numNearIpRanges] = maskLength;
            _proxy.dataDistribution.nearIpRanges[_proxy.dataDistribution.numNearIpRanges++] &= htonl(0xffffffff << (32 - maskLength));
            LOG(INFO) << "Range " << _proxy.dataDistribution.numNearIpRanges << " " << std::hex << _proxy.dataDistribution.nearIpRanges[_proxy.dataDistribution.numNearIpRanges - 1] << "/" << std::dec << _proxy.dataDistribution.nearIpRangeMasks[_proxy.dataDistribution.numNearIpRanges - 1];
            if (_proxy.dataDistribution.numNearIpRanges >= MAX_NUM_NEAR_IP_RANGES)
                break;
        }
        // proxy background write settings
        _proxy.backgroundWrite.writeRedundancy = readBool(_proxyPt, "background_write.write_redundancy_in_background");
        _proxy.backgroundWrite.ackRedundancy = _proxy.backgroundWrite.writeRedundancy || readBool(_proxyPt, "background_write.ack_redundancy_in_background");
        _proxy.backgroundWrite.numWorker = std::min(1, readInt(_proxyPt, "background_write.num_background_chunk_worker"));
        _proxy.backgroundWrite.taskCheckIntv = std::max(readInt(_proxyPt, "background_write.background_task_check_interval"), 5);
        // zmq request 
        _proxy.zmqITF.numWorkers = std::min(std::max(1, readInt(_proxyPt, "zmq_interface.num_workers")), MAX_NUM_WORKERS);
        _proxy.zmqITF.port = readInt(_proxyPt, "zmq_interface.port");

        // ldap authentication
        _proxy.ldapAuth.uri = readString(_proxyPt, "ldap_auth.uri");
        _proxy.ldapAuth.userOrg = readString(_proxyPt, "ldap_auth.user_organization");
        _proxy.ldapAuth.dnSuffix = readString(_proxyPt, "ldap_auth.dn_suffix");

        // immutable management api
        _proxy.immutableMgtApi.enabled = readBool(_proxyPt, "immutable_mgt_apis.enabled");
        if (_proxy.immutableMgtApi.enabled) {
            _proxy.immutableMgtApi.ip = readString(_proxyPt, "immutable_mgt_apis.ip");
            _proxy.immutableMgtApi.port = readInt(_proxyPt, "immutable_mgt_apis.port");
            if (_proxy.immutableMgtApi.port <= 0 || _proxy.immutableMgtApi.port > 65535) {
                LOG(WARNING) << "Invalid port for immutable policy management APIs, setting it to the default (" << 59003 << ")";
                _proxy.immutableMgtApi.port = 59003;
            }
            _proxy.immutableMgtApi.numWorkers = readInt(_proxyPt, "immutable_mgt_apis.num_workers");
            if (_proxy.immutableMgtApi.numWorkers <= 0) {
                LOG(WARNING) << "Invalid number of workers for immutable policy management APIs, setting it to the default (" << 4 << ")";
                _proxy.immutableMgtApi.numWorkers = 4;
            }
            _proxy.immutableMgtApi.timeoutInSeconds = std::max(1, readInt(_proxyPt, "immutable_mgt_apis.timeout"));
            _proxy.immutableMgtApi.sslCert = readString(_proxyPt, "immutable_mgt_apis.ssl_cert");
            _proxy.immutableMgtApi.sslCertKey = readString(_proxyPt, "immutable_mgt_apis.ssl_cert_key");
            _proxy.immutableMgtApi.sslCertPassword = readString(_proxyPt, "immutable_mgt_apis.ssl_cert_password");
            _proxy.immutableMgtApi.sslDH = readString(_proxyPt, "immutable_mgt_apis.ssl_dh");
            _proxy.immutableMgtApi.jwt.asymmetric.privateKey = readString(_proxyPt, "immutable_mgt_apis.jwt_private_key");
            _proxy.immutableMgtApi.jwt.asymmetric.publicKey = readString(_proxyPt, "immutable_mgt_apis.jwt_public_key");
            _proxy.immutableMgtApi.jwt.symmetric.secretKey = readString(_proxyPt, "immutable_mgt_apis.jwt_secret_key");
            if (
                !_proxy.immutableMgtApi.jwt.asymmetric.privateKey.empty()
                && !_proxy.immutableMgtApi.jwt.asymmetric.publicKey.empty()
            ) {
                _proxy.immutableMgtApi.jwt.useAsymmetic = true;
            } else if (
                !_proxy.immutableMgtApi.jwt.symmetric.secretKey.empty()
            ) {
                _proxy.immutableMgtApi.jwt.useAsymmetic = false;
            } else {
                _proxy.immutableMgtApi.jwt.useAsymmetic = false;
                LOG(WARNING) << "No keys set for JWT token generation, using the default one";
                _proxy.immutableMgtApi.jwt.symmetric.secretKey = "nexoedge_jwt_generation";
            }
        }

        // reporter db
        _proxy.reporterDB.ip = readString(_proxyPt, "reporter_db.ip");
        _proxy.reporterDB.port = readInt(_proxyPt, "reporter_db.port");
        _proxy.reporterDB.recordBufSize = readInt(_proxyPt, "reporter_db.record_buffer_size");

        // staging
        _proxy.staging.enabled = readBool(_proxyPt, "staging.enabled");
        _proxy.staging.url = readString(_proxyPt, "staging.url");
        _proxy.staging.autoClean.policy = readString(_proxyPt, "staging.autoclean_policy");
        _proxy.staging.autoClean.scanIntv = readInt(_proxyPt, "staging.autoclean_scan_interval");
        _proxy.staging.autoClean.numDaysExpire = readInt(_proxyPt, "staging.autoclean_num_days_expire");
        _proxy.staging.bgwrite.policy = readString(_proxyPt, "staging.bgwrite_policy");
        _proxy.staging.bgwrite.scanIntv = readInt(_proxyPt, "staging.bgwrite_scan_interval");
        _proxy.staging.bgwrite.scheduledTime = readString(_proxyPt, "staging.bgwrite_scheduled_time");

        // replication
        _proxy.replication.enabled = readBool(_proxyPt, "replication.enabled");
        _proxy.replication.masterName = readString(_proxyPt, "replication.master_name");
        _proxy.replication.numSentinels = readInt(_proxyPt, "replication.num_sentinels");
        if (_proxy.replication.numSentinels <= 0 || _proxy.replication.numSentinels > MAX_NUM_SENTINELS) {
            LOG(ERROR) << "Number of sentinel instances must be within 1 and " << MAX_NUM_SENTINELS;
            exit(-1);
        }
        for (int i = 0; i < _proxy.replication.numSentinels; i++) {
            std::string section = "sentinel" + std::to_string(i + 1);
            _proxy.replication.sentinels[i].ip = readString(_proxyPt, (section + ".ip").c_str());
            _proxy.replication.sentinels[i].port = readInt(_proxyPt, (section + ".port").c_str());
            
            if (_proxy.replication.sentinels[i].ip.empty()) {
                LOG(ERROR) << "IP address for sentinel " << (i + 1) << " is not specified";
                exit(-1);
            }
            
            if (_proxy.replication.sentinels[i].port <= 0 || _proxy.replication.sentinels[i].port > 65535) {
                LOG(ERROR) << "Invalid port for sentinel " << (i + 1) << ": " << _proxy.replication.sentinels[i].port;
                exit(-1);
            }
            
            LOG(INFO) << "Sentinel " << (i + 1) << ": " << _proxy.replication.sentinels[i].ip 
                      << ":" << _proxy.replication.sentinels[i].port;
        }
    }

    printConfig();
}


// General

int Config::getLogLevel() const {
    assert(!_generalPt.empty());
    return _general.level;
}

bool Config::glogToConsole() const {
    assert(!_generalPt.empty());
    return _general.glogToConsole;
}

std::string Config::getGlogDir() const {
    assert(!_generalPt.empty());
    return _general.glogdir;
}

int Config::getRetryInterval() const {
    assert(!_generalPt.empty());
    return _general.retry.intv;
}

int Config::getNumRetry() const {
    assert(!_generalPt.empty());
    return _general.retry.num;
}

bool Config::listenToAllInterfaces() const {
    assert(!_generalPt.empty());
    return _general.network.listenToAllInterfaces;
}

bool Config::manualTcpKeepAlive() const {
    assert(!_generalPt.empty());
    return _general.network.tcpKeepAlive.enabled;
}

int Config::getTcpKeepAliveIdle() const {
    assert(!_generalPt.empty());
    return _general.network.tcpKeepAlive.idle;
}

int Config::getTcpKeepAliveIntv() const {
    assert(!_generalPt.empty());
    return _general.network.tcpKeepAlive.intv;
}

int Config::getTcpKeepAliveCnt() const {
    assert(!_generalPt.empty());
    return _general.network.tcpKeepAlive.cnt;
}

int Config::getTcpBufferSize() const {
    assert(!_generalPt.empty());
    return _general.network.tcpBuffer;
}

int Config::useCurve() const {
    assert(!_generalPt.empty());
    return _general.network.useCurve;
}

const char *Config::getAgentCurvePublicKey() const {
    assert(!_agentPt.empty());
    return _general.network.agent.curvePublicKey;
}

const char *Config::getAgentCurveSecretKey() const {
    assert(!_agentPt.empty());
    return _general.network.agent.curveSecretKey;
}

const char *Config::getProxyCurvePublicKey() const {
    assert(!_proxyPt.empty());
    return _general.network.proxy.curvePublicKey;
}

const char *Config::getProxyCurveSecretKey() const {
    assert(!_proxyPt.empty());
    return _general.network.proxy.curveSecretKey;
}

int Config::getEventProbeTimeout() const {
    assert(!_generalPt.empty());
    return _general.event.eventProbeTimeout;
}

bool Config::verifyChunkChecksum() const{
    assert(!_generalPt.empty());
    return _general.dataIntegrity.verifyChunkChecksum;
}

bool Config::getBenchmarkStripeEnabled() const{
    assert(!_generalPt.empty());
    return _general.benchmark.stripeEnabled;
}


// Agent
std::string Config::getAgentIP() const {
    assert(!_agentPt.empty());
    return _agent.ip;
}

unsigned short Config::getAgentPort() const {
    assert(!_agentPt.empty());
    return _agent.port;
}

unsigned short Config::getAgentCPort() const {
    assert(!_agentPt.empty());
    return _agent.cport;
}

int Config::getNumContainers() const {
    assert(!_agentPt.empty());
    return _agent.numContainers;
}

int Config::getContainerId(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return INVALID_CONTAINER_ID;
    return _agent.containers[i].id;
} 

std::string Config::getContainerPath(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return std::string();
    return _agent.containers[i].url;
} 

unsigned long int Config::getContainerCapacity(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return -1;
    return _agent.containers[i].capacity;
} 

unsigned short Config::getContainerType(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return -1;
    return _agent.containers[i].type;
}

std::string Config::getContainerRegion(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return std::string(); 
    return _agent.containers[i].region;
}

std::string Config::getContainerKeyId(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return std::string(); 
    return _agent.containers[i].keyId;
}

std::string Config::getContainerKey(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return std::string(); 
    return _agent.containers[i].key;
}

std::string Config::getContainerHttpProxyIP(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return std::string(); 
    return _agent.containers[i].httpProxy.ip;
}

unsigned short Config::getContainerHttpProxyPort(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return 0; 
    return _agent.containers[i].httpProxy.port;
}

std::string Config::getContainerEndpoint(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return std::string(); 
    return _agent.containers[i].endpoint;
}

bool Config::getContainerVerifySSL(int i) const {
    assert(!_agentPt.empty());
    if (i >= _agent.numContainers)
        return true; 
    return _agent.containers[i].verifySSL;
}

int Config::getAgentNumWorkers() const {
    assert(!_agentPt.empty());
    return _agent.misc.numWorkers;
}

int Config::getAgentNumZmqThread() const {
    assert(!_agentPt.empty());
    return _agent.misc.numZmqThread;
}

unsigned long int Config::getCopyBlockSize() const {
    assert(!_agentPt.empty());
    return _agent.misc.copyBlockSize;
}

bool Config::getAgentFlushOnClose() const {
    assert(!_agentPt.empty());
    return _agent.misc.flushOnClose;
}

bool Config::getAgentRegisterToProxy() const {
    assert(!_agentPt.empty());
    return _agent.misc.registerToProxy;
}

// Proxy

int Config::getNumProxy() const {
    assert(!_generalPt.empty());
    return _proxy.numProxy;
}

unsigned char  Config::getProxyNamespaceId() const {
    assert(!_proxyPt.empty());
    return _proxy.namespaceId;
}

int Config::getMyProxyNum() const {
    assert(!_proxyPt.empty());
    return _proxy.myProxyNum;
}

std::string Config::getProxyInterface() const {
    assert(!_proxyPt.empty());
    return _proxy.interface;
}

std::string Config::getProxyIP(int i) const {
    assert(!_generalPt.empty());
    return (i < _proxy.numProxy)? _proxy.addrs[i].ip : INVALID_IP;
}

unsigned short Config::getProxyCPort(int i) const {
    assert(!_generalPt.empty());
    return (i < _proxy.numProxy)? _proxy.addrs[i].cport : INVALID_PORT;
}

std::string Config::getStorageClassesFilePath() const {
    assert(!_proxyPt.empty());
    return _proxy.storageClass.filePath;
}

std::string Config::getDefaultStorageClass() const {
    assert(!_proxyPt.empty());
    return _proxy.storageClass.defaultClass;
}

bool Config::hasStorageClass(std::string sc) const {
    assert(!_proxyPt.empty());
    return _proxy.storageClass.classes.count(sc) > 0;
}

int Config::getNumStorageClasses() const {
    assert(!_proxyPt.empty());
    return _proxy.storageClass.classes.size();
}

std::set<std::string> Config::getStorageClasses() const {
    assert(!_proxyPt.empty());
    return _proxy.storageClass.classes;
}

int Config::getCodingScheme(std::string storageClass) const {
    std::string sc = storageClass.empty()? _proxy.storageClass.defaultClass : storageClass;
    int coding = parseCodingScheme(readString(_storageClassPt, sc.append(".coding").c_str()));
    if (coding < 0 || coding >= CodingScheme::UNKNOWN_CODE)
        coding = CodingScheme::UNKNOWN_CODE;
    return coding;
}

int Config::getN(std::string storageClass) const {
    return getStorageClassConfig(storageClass, "n", -1, 0);
}

int Config::getK(std::string storageClass) const {
    return getStorageClassConfig(storageClass, "k", -1, 0);
}

int Config::getF(std::string storageClass) const {
    return getStorageClassConfig(storageClass, "f", -1, 0);
}

int Config::getMaxChunkSize(std::string storageClass) const {
    return getStorageClassConfig(storageClass, "max_chunk_size", 0, 0, 1 << 30);
}

int Config::getStorageClassConfig(std::string storageClass, std::string config, int dv, int min, int max) const {
    std::string sc = storageClass.empty()? _proxy.storageClass.defaultClass : storageClass;
    return readIntWithBoundsAndDefault(_storageClassPt, sc.append(".").append(config).c_str(), dv, min, max);
}

int Config::readIntWithBounds(const boost::property_tree::ptree &pt, const char *key, int min, int max) const {
    assert(!_storageClassPt.empty());
    int value = readInt(_storageClassPt, key);
    return value <= min ? min : (value > max? max : value);
}

int Config::readIntWithBoundsAndDefault(const boost::property_tree::ptree &pt, const char *key, int dv, int min, int max) const {
    int value = dv;
    try {
        value = readIntWithBounds(pt, key, min, max);
    } catch (std::exception &e) {
    }
    return value;
}

int Config::getProxyMetaStoreType() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.type;
}

std::string Config::getProxyMetaStoreIP() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.ip;
}

unsigned short Config::getProxyMetaStorePort() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.port;
}

std::string Config::getProxyMetaStoreSSLCACertPath() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.ssl.caCertPath;
}

std::string Config::getProxyMetaStoreSSLTrustedCertsDir() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.ssl.trustedCertsDir;
}

std::string Config::getProxyMetaStoreSSLClientCertPath() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.ssl.clientCertPath;
}

std::string Config::getProxyMetaStoreSSLClientKeyPath() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.ssl.clientKeyPath;
}

std::string Config::getProxyMetaStoreSSLDomainName() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.ssl.domainName;
}

std::string Config::getProxyMetaStoreUser() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.auth.user;
}

std::string Config::getProxyMetaStorePassword() const {
    assert(!_proxyPt.empty());
    return _proxy.metastore.redis.auth.password;
}

int Config::getProxyNumZmqThread() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.numZmqThread;
}

bool Config::isRepairAtProxy() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.repairAtProxy;
}

bool Config::isRepairUsingCAR() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.repairUsingCAR;
}

bool Config::overwriteFiles() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.overwriteFiles;
}

bool Config::reuseDataConn() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.reuseDataConn;
}

int Config::getLivenessCacheTime() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.livenessCacheTime;
}

std::vector<std::pair<std::string, unsigned short> > Config::getAgentList() {
    return _proxy.misc.agentList;
}

int Config::getJournalCheckInterval() const {
    assert(!_proxyPt.empty());
    return _proxy.misc.scanJournalIntv;
}

int Config::getProxyDistributePolicy() const {
    assert(!_proxyPt.empty());
    return _proxy.dataDistribution.policy;
}

bool Config::ackRedundancyInBackground() const {
    assert(!_proxyPt.empty());
    return _proxy.backgroundWrite.ackRedundancy;
}

bool Config::writeRedundancyInBackground() const {
    assert(!_proxyPt.empty());
    return _proxy.backgroundWrite.writeRedundancy;
}

bool Config::getProxyNumBgChunkWorker() const {
    assert(!_proxyPt.empty());
    return _proxy.backgroundWrite.numWorker;
}

int Config::getBgTaskCheckInterval() const {
    assert(!_proxyPt.empty());
    return _proxy.backgroundWrite.taskCheckIntv;
}

int* Config::getProxyNearIpRanges(int &numRanges) const {
    assert(!_proxyPt.empty());
    numRanges = _proxy.dataDistribution.numNearIpRanges;
    if (_proxy.dataDistribution.numNearIpRanges == 0) 
        return 0;
    int *ipRanges = new int[_proxy.dataDistribution.numNearIpRanges * 2];
    for (int i = 0; i < _proxy.dataDistribution.numNearIpRanges; i++) {
        ipRanges[i] = _proxy.dataDistribution.nearIpRanges[i];
        ipRanges[i + _proxy.dataDistribution.numNearIpRanges] = _proxy.dataDistribution.nearIpRangeMasks[i];
    }
    return ipRanges;
}

bool Config::isAgentNear(const char *ipStr) const {
    unsigned int ip;
    // invalid ip string
    if (inet_pton(AF_INET, ipStr, &ip) != 1)
        return false;
    // search the ip ranges; TODO improve for larger number of ranges?
    for (int i = 0; i < _proxy.dataDistribution.numNearIpRanges; i++) {
        if ((ip & htonl(0xffffffff << (32 - _proxy.dataDistribution.nearIpRangeMasks[i]))) == _proxy.dataDistribution.nearIpRanges[i])
            return true;
    }
    return false;
}

int Config::getProxyZmqNumWorkers() const {
    assert(!_proxyPt.empty());
    return _proxy.zmqITF.numWorkers;
}

unsigned short Config::getProxyZmqPort() const {
    assert(!_proxyPt.empty());
    return _proxy.zmqITF.port;
}

bool Config::autoFileRecovery() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.enabled;
}

int Config::getFileRecoverInterval() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.recoverIntv;
}

int Config::getFileScanInterval() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.scanIntv;
}

time_t Config::getChunkScanInterval() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.scanChunkIntv * HOUR_IN_SECONDS;
}

int Config::getChunkScanBatchSize() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.chunkBatchSize;
}

int Config::getFileRecoverBatchSize() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.batchSize;
}

int Config::getChunkScanSamplingPolicy() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.chunkScanSampling.policy;
}

double Config::getChunkScanSamplingRate() const {
    assert(!_proxyPt.empty());
    return _proxy.recovery.chunkScanSampling.rate;
}

int Config::getFailureTimeout() const {
    assert(!_generalPt.empty());
    return _general.failureDetection.timeout;
}

std::string Config::getProxyLdapUri() const {
    assert(!_proxyPt.empty());
    return _proxy.ldapAuth.uri;
}

std::string Config::getProxyLdapUserOrganization() const {
    assert(!_proxyPt.empty());
    return _proxy.ldapAuth.userOrg;
}

std::string Config::getProxyLdapDnSuffix() const {
    assert(!_proxyPt.empty());
    return _proxy.ldapAuth.dnSuffix;
}

bool Config::enableImmutableMgtApi() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.enabled;
}

std::string Config::getProxyImmutableMgtApiIP() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.ip;
}

unsigned short Config::getProxyImmutableMgtApiPort() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.port;
}

int Config::getProxyImmutableMgtApiNumWorkerThreads() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.numWorkers;
}

std::string Config::getProxyImmutableMgtApiSSLCert() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.sslCert;
}

std::string Config::getProxyImmutableMgtApiSSLCertKey() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.sslCertKey;
}

std::string Config::getProxyImmutableMgtApiSSLCertPassword() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.sslCertPassword;
}

std::string Config::getProxyImmutableMgtApiSSLDH() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.sslDH;
}

bool Config::proxyImmutableMgtApiJWTUseAsymmetric() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.jwt.useAsymmetic;
}

std::string Config::getProxyImmutableMgtApiJWTSecretKey() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.jwt.symmetric.secretKey;
}

std::string Config::getProxyImmutableMgtApiJWTPrivateKey() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.jwt.asymmetric.privateKey;
}

std::string Config::getProxyImmutableMgtApiJWTPublicKey() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.jwt.asymmetric.publicKey;
}

int Config::getProxyImmutableMgtApiSessionTimeoutInSeconds() const {
    assert(!_proxyPt.empty());
    return _proxy.immutableMgtApi.timeoutInSeconds;
}

std::string Config::getProxyReporterDBIP() const {
    assert(!_proxyPt.empty());
    return _proxy.reporterDB.ip;
}

unsigned short Config::getProxyReporterDBPort() const {
    assert(!_proxyPt.empty());
    return _proxy.reporterDB.port;
}

int Config::getProxyReporterDBRecordBufferSize() const {
    assert(!_proxyPt.empty());
    return _proxy.reporterDB.recordBufSize;
}

bool Config::sendStatsToReporterDB() const {
    assert(!_proxyPt.empty());
    return !_proxy.reporterDB.ip.empty();
}

bool Config::proxyStagingEnabled() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.enabled;
}

std::string Config::getProxyStagingStorageURL() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.url;
}

std::string Config::getProxyStagingAutoCleanPolicy() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.autoClean.policy;
}

int Config::getProxyStagingAutoCleanNumDaysExpire() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.autoClean.numDaysExpire;
}

int Config::getProxyStagingAutoCleanScanIntv() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.autoClean.scanIntv;
}

std::string Config::getProxyStagingBackgroundWritePolicy() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.bgwrite.policy;
}

int Config::getProxyStagingBackgroundWriteScanInterval() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.bgwrite.scanIntv;
}

std::string Config::getProxyStagingBackgroundWriteTimestamp() const {
    assert(!_proxyPt.empty());
    return _proxy.staging.bgwrite.scheduledTime;
}

bool Config::proxyReplicationEnabled() const {
    assert(!_proxyPt.empty());
    return _proxy.replication.enabled;
}

std::string Config::getProxyReplicationMasterName() const {
    assert(!_proxyPt.empty());
    return _proxy.replication.masterName;
}

int Config::getProxyReplicationNumSentinels() const {
    assert(!_proxyPt.empty());
    return _proxy.replication.numSentinels;
}

std::vector<std::pair<std::string, int>> Config::getProxyReplicationSentinelsContext() const {
    assert(!_proxyPt.empty());
    std::vector<std::pair<std::string, int>> sentinels;
    for (int i = 0; i < _proxy.replication.numSentinels; i++) {
        sentinels.push_back(std::make_pair(_proxy.replication.sentinels[i].ip, _proxy.replication.sentinels[i].port));
    }
    return sentinels;
}

// Print

void Config::printConfig() const {
    const int bufSize = 256 * 1 << 10; // 256KB
    char buf[bufSize];
    int length = 0;

    snprintf(buf, bufSize,
        "\n------ General ------\n"
        " Log level                   : %s\n"
        " Debug to console            : %s\n"
        " Debug log directory         : %s\n"
        " - Retry\n"
        "   - Number                  : %d\n"
        "   - Interval                : %dus\n"
        " - Network\n"
        "   - Listen to all IPs       : %s\n"
        "   - TCP keep alive          : %s\n"
        "   - TCP keep alive idle     : %d\n"
        "   - TCP keep alive interval : %d\n"
        "   - TCP keep alive count    : %d\n"
        "   - TCP buffer size         : %dB\n"
        "   - ZeroMQ CURVE            : %s\n"
        "     - Proxy public key      : %s\n"
        "     - Proxy secret key      : %s\n"
        "     - Agent public key      : %s\n"
        "     - Agent secret key      : %s\n"
        " - Data Integrity\n"
        "   - Verify chunk checksum   : %s\n"
        " - Failure Detection\n"
        "   - Timeout                 : %dms\n"
        " Event probe timeout         : %dms\n"
        " Num of proxy                : %d\n"
        " - Benchmark\n"
        "   - Stripe level enabled    : %s\n"
        , LogLevelName[getLogLevel()]
        , glogToConsole()? "true" : "false" 
        , getGlogDir().c_str()
        , getNumRetry()
        , getRetryInterval()
        , listenToAllInterfaces()? "true" : "false"
        , manualTcpKeepAlive()? "On" : "Off"
        , getTcpKeepAliveIdle()
        , getTcpKeepAliveIntv()
        , getTcpKeepAliveCnt()
        , getTcpBufferSize()
        , useCurve()? "On" : "Off"
        , getProxyCurvePublicKey()? "(provided)" : "(n/a)"
        , getProxyCurveSecretKey()? "(provided)" : "(n/a)"
        , getAgentCurvePublicKey()? "(provided)" : "(n/a)"
        , getAgentCurveSecretKey()? "(provided)" : "(n/a)"
        , verifyChunkChecksum()? "true" : "false"
        , getFailureTimeout()
        , getEventProbeTimeout()
        , getNumProxy()
        , getBenchmarkStripeEnabled() ? "true" : "false"
    );
    LOG(ERROR) << buf;
    
    // print myself first
    if (!_proxyPt.empty()) {
        length += snprintf(buf, bufSize,
            "\n------- Proxy %02d (Current) ------\n"
            " IP                          : %s\n"
            " Coordinator Port            : %d\n"
            " Interface                   : %s\n"
            , _proxy.myProxyNum + 1
            , getProxyIP(_proxy.myProxyNum).c_str()
            , getProxyCPort(_proxy.myProxyNum)
            , getProxyInterface().c_str()
        );
        length += snprintf(buf + length, bufSize - length,
            " - MetaStore                 : %s\n",
            MetaStoreName[getProxyMetaStoreType()]
        );
        switch (getProxyMetaStoreType()) {
        case MetaStoreType::REDIS:
            length += snprintf(buf + length, bufSize - length,
                "   - IP                      : %s\n"
                "   - Port                    : %d\n"
                "   - SSL/TLS CA              : %s\n"
                "   - SSL/TLS Trusted Certs   : %s\n"
                "   - SSL/TLS Client Cert     : %s\n"
                "   - SSL/TLS Client Key      : %s\n"
                "   - SSL/TLS Domain name     : %s\n"
                , getProxyMetaStoreIP().c_str()
                , getProxyMetaStorePort()
                , getProxyMetaStoreSSLCACertPath().c_str()
                , getProxyMetaStoreSSLTrustedCertsDir().c_str()
                , getProxyMetaStoreSSLClientCertPath().c_str()
                , getProxyMetaStoreSSLClientKeyPath().c_str()
                , getProxyMetaStoreSSLDomainName().c_str()
            );
            break;
        }
        int numClasses = getNumStorageClasses();
        length += snprintf(buf + length, bufSize - length,
            " - Storage classes (%d)\n"
            , numClasses
        );
        std::set<std::string> classes = getStorageClasses();
        std::set<std::string>::iterator classIt = classes.begin(), classEd = classes.end();
        std::string defaultClass = getDefaultStorageClass();
        for (; classIt != classEd; classIt++) {
            length += snprintf(buf + length, bufSize - length,
                "   - [%s]\n"
                "     - coding                : %s\n"
                "     - n                     : %d\n"
                "     - k                     : %d\n"
                "     - f                     : %d\n"
                "     - Max chunk size        : %dB\n"
                "     - Is default            : %s\n"
                , classIt->c_str()
                , CodingSchemeName[getCodingScheme(*classIt)]
                , getN(*classIt)
                , getK(*classIt)
                , getF(*classIt)
                , getMaxChunkSize(*classIt)
                , *classIt == defaultClass? "true" : "false"
            );
        }
        char scanIntv[64];
        unsigned long int hours = getChunkScanInterval() / HOUR_IN_SECONDS;
        snprintf(scanIntv, 64, "%ld hour%s", hours, (hours > 1? "s" : ""));
        length += snprintf(buf + length, bufSize - length,
            " - Recovery                  : %s\n"
            "   - Trigger interval        : %ds\n"
            "   - File scan interval      : %ds\n"
            "     - Integrity scan        : %s\n"
            "       - Num chunks per batch: %d\n"
            "       - Sampling policy     : %s\n"
            "       - Sampling rate       : %.lf\n"
            "   - Num files per batch     : %d\n"
            , autoFileRecovery()? "On" : "Off"
            , getFileRecoverInterval()
            , getFileScanInterval()
            , getChunkScanInterval() > 0? scanIntv : "Off"
            , getChunkScanBatchSize()
            , ChunkScanSamplingPolicyName[getChunkScanSamplingPolicy()]
            , getChunkScanSamplingRate()
            , getFileRecoverBatchSize()
        );
        int numRanges = 0;
        int *ranges = getProxyNearIpRanges(numRanges);
        length += snprintf(buf + length, bufSize - length,
            " - Misc\n"
            "   - Num zmq threads         : %d\n"
            "   - Repair at Proxy         : %s\n"
            "   - Repair using CAR (RS)   : %s\n"
            "   - Overwrite files         : %s\n"
            "   - Reuse data connections  : %s\n"
            "   - Liveness Cache Time     : %ds\n"
            "   - Journal check interval  : %ds\n"
            , getProxyNumZmqThread()
            , isRepairAtProxy()? "true" : "false"
            , isRepairUsingCAR()? "true" : "false"
            , overwriteFiles()? "true" : "false"
            , reuseDataConn()? "true" : "false"
            , getLivenessCacheTime()
            , getJournalCheckInterval()
        );
        length += snprintf(buf + length, bufSize - length,
            " - Background chunk handler\n"
            "   - Num. of workers         : %d\n"
            "   - Write redundancy        : %s\n"
            "   - Ack redundancy          : %s\n"
            "   - Task check interval     : %ds\n"
            , getProxyNumBgChunkWorker()
            , writeRedundancyInBackground()? "true" : "false"
            , ackRedundancyInBackground()? "true" : "false"
            , getBgTaskCheckInterval()
        );
        length += snprintf(buf + length, bufSize - length,
            " - Data distribution\n"
            "   - Policy                  : %s\n"
            "   - Near IP ranges          : %d\n"
            , DistributionPolicyName[getProxyDistributePolicy()]
            , numRanges
        );
        char ipStr[INET_ADDRSTRLEN];
        for (int i = 0; i < numRanges; i++) {
            if (inet_ntop(AF_INET, &ranges[i], ipStr, INET_ADDRSTRLEN) != ipStr)
                continue;
            length += snprintf(buf + length, bufSize - length,
                "   - Range %03d                : %s/%u\n"
                , i + 1, ipStr, ranges[i + numRanges]
            );
        }
        delete [] ranges;
        length += snprintf(buf + length, bufSize - length,
            " - Zero-MQ interface\n"
            "   - Num. of workers         : %d\n"
            "   - Port                    : %d\n"
            , getProxyZmqNumWorkers()
            , getProxyZmqPort()
        );
        length += snprintf(buf + length, bufSize - length,
            " - Immutable Storage Policy Manager\n"
            "   - API                     : %s\n"
            "     - IP                    : %s\n"
            "     - Port                  : %u\n"
            "     - Number of workers     : %u\n"
            "     - SSL                   : %s\n"
            "       - Cert file path      : %s\n"
            "       - Cert key file path  : %s\n"
            "       - Cert pwd file path  : %s\n"
            "       - DH param. file path : %s\n"
            "     - JWT token\n"
            "       - Encryption          : %s\n"
            "       - Private key path    : %s\n"
            "       - Public key path     : %s\n"
            "       - Secret key path     : %s\n"
            "     - LDAP authentication\n"
            "       - URI                 : %s\n"
            "       - User organization   : %s\n"
            "       - DN suffix           : %s\n"
            "     - Connection timeout    : %us\n"
            , enableImmutableMgtApi()? "On" : "Off"
            , getProxyImmutableMgtApiIP().c_str()
            , getProxyImmutableMgtApiPort()
            , getProxyImmutableMgtApiNumWorkerThreads()
            , getProxyImmutableMgtApiSSLCert().empty() || getProxyImmutableMgtApiSSLCertKey().empty()? "Off" : "On"
            , getProxyImmutableMgtApiSSLCert().c_str()
            , getProxyImmutableMgtApiSSLCertKey().c_str()
            , getProxyImmutableMgtApiSSLCertPassword().c_str()
            , getProxyImmutableMgtApiSSLDH().c_str()
            , proxyImmutableMgtApiJWTUseAsymmetric()? "RSA256" : "SHA256"
            , getProxyImmutableMgtApiJWTPrivateKey().c_str()
            , getProxyImmutableMgtApiJWTPublicKey().c_str()
            , getProxyImmutableMgtApiJWTSecretKey().c_str()
            , getProxyLdapUri().c_str()
            , getProxyLdapUserOrganization().c_str()
            , getProxyLdapDnSuffix().c_str()
            , getProxyImmutableMgtApiSessionTimeoutInSeconds()
        );
        length += snprintf(buf + length, bufSize - length,
            " - Reporter DB (Redis)\n"
            "   - IP                      : %s\n"
            "   - Port                    : %hu\n"
            "   - Record buffer size      : %d\n"
            , getProxyReporterDBIP().c_str()
            , getProxyReporterDBPort()
            , getProxyReporterDBRecordBufferSize()
        );
        length += snprintf(buf + length, bufSize - length,
            " - Staging                   : %s\n"
            "   - Storage path            : %s\n"
            "   - Auto-clean              : %s\n"
            "     - Scan interval         : %ds\n"
            "     - Files expire after    : %d days\n"
            "   - Background write        : %s\n"
            "     - Scan interval         : %ds\n"
            "     - Scheduled time        : %s\n"
            , proxyStagingEnabled() ? "On" : "Off"
            , getProxyStagingStorageURL().c_str()
            , getProxyStagingAutoCleanPolicy().c_str()
            , getProxyStagingAutoCleanNumDaysExpire()
            , getProxyStagingAutoCleanScanIntv()
            , getProxyStagingBackgroundWritePolicy().c_str()
            , getProxyStagingBackgroundWriteScanInterval()
            , getProxyStagingBackgroundWriteTimestamp().c_str()
        );
        length += snprintf(buf + length, bufSize - length,
            " - Replication               : %s\n"
            "   - Master name             : %s\n"
            "   - Num of sentinels        : %d\n"
            , proxyReplicationEnabled() ? "On" : "Off"
            , getProxyReplicationMasterName().c_str()
            , getProxyReplicationNumSentinels()
        );
        for (int i = 0; i < getProxyReplicationNumSentinels(); i++) {
            length += snprintf(buf + length, bufSize - length,
                "   - Sentinel %02d \n"
                "     - IP                    : %s\n"
                "     - Port                  : %d\n"
                , i + 1
                , getProxyReplicationSentinelsContext()[i].first.c_str()
                , getProxyReplicationSentinelsContext()[i].second
            );
        }
        LOG(ERROR) << buf;
        length = 0;
    }
    // and then the others
    for (int i = 0; i < _proxy.numProxy; i++) {
        if (!_proxyPt.empty() && _proxy.myProxyNum == i)
            continue;
        length += snprintf(buf + length, bufSize - length,
            "\n------- Proxy %02d ------\n"
            " IP                          : %s\n"
            " Coordinator Port            : %d\n"
            , i + 1
            , getProxyIP(i).c_str()
            , getProxyCPort(i)
        );
    }
    if (length > 0) { LOG(ERROR) << buf; length = 0; }
    if (!_agentPt.empty()) {
        length += snprintf(buf + length, bufSize - length,
            "\n------- Agent  ------\n"
            " IP                          : %s\n"
            " Data Port                   : %d\n"
            " Coordinator Port            : %d\n"
            " Num of Workers              : %d\n"
            " Num of containers           : %d\n"
            " Num zmq threads             : %d\n"
            " Copy block size             : %luB\n"
            , getAgentIP().c_str()
            , getAgentPort()
            , getAgentCPort()
            , getAgentNumWorkers()
            , getNumContainers()
            , getAgentNumZmqThread()
            , getCopyBlockSize()
        );
        for (int i = 0; i < getNumContainers(); i++) {
            int type = getContainerType(i);
            length += snprintf(buf + length, bufSize - length,
                " - Container id              : %u\n"
                "   - Type                    : %s\n"
                "   - Url                     : %s\n"
                "   - Capacity                : %luB\n"
                "   - Http proxy              : %s\n"
                "   - Endpoint (generic S3 only): %s\n"
                "   - Verify SSL (generic S3 only): %s\n"
                , getContainerId(i)
                , ContainerTypeName[type]
                , getContainerPath(i).c_str()
                , getContainerCapacity(i)
                , getContainerHttpProxyIP(i).empty()? "" : getContainerHttpProxyIP(i).append(":").append(std::to_string(getContainerHttpProxyPort(i))).c_str()
                , getContainerEndpoint(i).c_str()
                , getContainerVerifySSL(i)? "true" : "false"
            );
        }
        LOG(ERROR) << buf;
        length = 0;
    }
}


// private helper functions

bool Config::readBool (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<bool>(key);
}

int Config::readInt (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<int>(key);
}

unsigned int Config::readUInt (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<unsigned int>(key);
}

long long Config::readLL (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<long long>(key);
}

unsigned long long Config::readULL (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<unsigned long long>(key);
}

double Config::readFloat (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<double>(key);
}

std::string Config::readString (const boost::property_tree::ptree &pt, const char *key) const {
    assert(!pt.empty());
    return pt.get<std::string>(key);
}

char* Config::readBytesFromFile (const std::string filepath, int numBytesToRead) const {
    // skip reading if no filename is available
    if (filepath.empty()) {
        LOG(ERROR) << "Empty filepath for read!";
        return nullptr;
    }
    // open the file for reading
    std::ifstream sourceFile (filepath, std::ios::binary | std::ios::in); 
    // skip reading if file openning fails
    if (!sourceFile.is_open()) {
        LOG(ERROR) << "Failed to open the file [" << filepath << "] for read!";
        return nullptr;
    }
    // read bytes into a buffer and return the buffer
    char *buf = new char[numBytesToRead];
    sourceFile.read(buf, numBytesToRead);
    bool failed = sourceFile.fail();
    if (failed) {
        LOG(ERROR) << "Failed to read " << numBytesToRead << " bytes from the file [" << filepath << "] (only " << sourceFile.tellg() << " bytes available)!";
        delete [] buf;
    }
    sourceFile.close();
    return failed? nullptr : buf;
}

unsigned short Config::parseContainerType(std::string typeName) const {
    for (int i = 0; i < ContainerType::UNKNOWN_CONTAINER; i++) {
        if (boost::algorithm::to_lower_copy(std::string(ContainerTypeName[i])) == boost::algorithm::to_lower_copy(typeName))
            return i;
    }
    return ContainerType::UNKNOWN_CONTAINER;
}

int Config::parseLogLevel(std::string levelName) const {
    for (int i = 0; i < google::NUM_SEVERITIES; i++) {
        if (boost::algorithm::to_lower_copy(std::string(LogLevelName[i])) == boost::algorithm::to_lower_copy(levelName))
            return i;
    }
    return -1; 
}

int Config::parseDistributionPolicy(std::string policyName) const {
    for (int i = 0; i < DistributionPolicy::UNKNOWN_DIST_POLICY; i++) {
        if (boost::algorithm::to_lower_copy(std::string(DistributionPolicyName[i])) == boost::algorithm::to_lower_copy(policyName))
            return i;
    }
    return DistributionPolicy::UNKNOWN_DIST_POLICY; 
}

int Config::parseCodingScheme(std::string schemeName) const {
    for (int i = 0; i < CodingScheme::UNKNOWN_CODE; i++) {
        if (boost::algorithm::to_lower_copy(std::string(CodingSchemeName[i])) == boost::algorithm::to_lower_copy(schemeName))
            return i;
    }
    return CodingScheme::UNKNOWN_CODE;
}

int Config::parseChunkScanSamplingPolicy(std::string policyName) const {
    for (int i = 0; i < ChunkScanSamplingPolicy::UNKNOWN_SAMPLING_POLICY; i++) {
        if (boost::algorithm::to_lower_copy(std::string(ChunkScanSamplingPolicyName[i])) == boost::algorithm::to_lower_copy(policyName)) {
            return i;
        }
    }
    return ChunkScanSamplingPolicy::UNKNOWN_SAMPLING_POLICY;
}

int Config::parseMetaStoreType(std::string storeName) const {
    for (int i = 0; i < MetaStoreType::UNKNOWN_METASTORE; i++) {
        if (boost::algorithm::to_lower_copy(std::string(MetaStoreName[i])) == boost::algorithm::to_lower_copy(storeName))
            return i;
    }
    return MetaStoreType::UNKNOWN_METASTORE;
}

