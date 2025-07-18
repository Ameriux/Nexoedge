# Configuration

## Configuration Files

Nexoedge has four configuration files:

- `general.ini`: Configurations required by all entities.
- `proxy.ini`: Configurations required by `proxy` and `ncloud-reporter`.
- `agent.ini`: Configurations required by `agent`.
- `storage_class.ini`: Configurations of storage policies required by `proxy`.

Proxy (`proxy`), agent (`agent`) and reporter (`ncloud_reporter`) search the directories in the following order for the configuration files:

1. Directory path provided as the first argument when starting the entities 
2. Directory path saved in the environment variable `NCLOUD_CONFIG_PATH`
3. Current directory

## Common Configuration

In `general.ini`,

- `log`: Logging
  - `level`: Level of messages to print for logging
  - `glog_to_console`: Whether to print the log messages to console
  - `glog_dir`: Directory to store log messages if not printed to console, default value is "/tmp/ncloud_log" 
- `retry`: Retry settings before giving up an operation
  - `num`: Number of retries
  - `interval`: Time to wait between retries (in microseconds)
- `data_integrity`: Data integrity
  - `verify_chunk_checksum`: whether to verify chunk checksum upon data access
- `failure_detection`: Failure detection
  - `timeout`: Timeout to declare agent failure (in milliseconds)
- `event`: Event listening settings
  - `event_probe_timeout`: Timeout of an event probe over a socket (in milliseconds)
- `benchmark`: Benchmark framework
  - `stripe_enabled`: Whether to enable stripe-level benchmark
- `network`: Network settings
  - `listen_all_ips`: Whether to listen to all IP address (i.e., 0.0.0.0/0)
  - `tcp_keep_alive`: Whether to use manual TCP keep-alive settings
  - `tcp_keep_alive_idle`: Connection idle time before sending the first TCP keep-alive packet (in seconds)
  - `tcp_keep_alive_intv`: Time interval between TCP keep-alive packets (in seconds)
  - `tcp_keep_alive_cnt`: Number of keep-alive packets to sent before giving up an unresponsive connection 
  - `tcp_buffer_size`: TCP Send/Receive buffer size (in bytes)
  - `use_curve`: Whether to enable the CURVE mechanism for all connections between proxies and agents for security
  - `agent_curve_public_key_file`: Agent permanent public key file for ZeroMQ CURVE (essential for agent and proxy)
  - `agent_curve_secret_key_file`: Agent permanent secret key file for ZeroMQ CURVE (essential for agent)
  - `proxy_curve_public_key_file`: Proxy permanent public key file for ZeroMQ CURVE (essential for agent and proxy)
  - `proxy_curve_secret_key_file`: Proxy permanent secret key file for ZeroMQ CURVE (essential for proxy)
- `proxy`: Proxy
  - ``num_proxy``: Number of proxies to connect
- `proxy[01-99]`: Proxy information
  - `ip`: IP address
  - `coord_port`: Port number for listening incoming coordinator requests from agents

## Proxy Configuration

In `proxy.ini`,

- `proxy`: Proxy
  - `num`: Entry number of the corresponding proxy to host in the proxy list in `general.ini`
  - `namespace_id`: Storage namespace ID
  - `interface`: Interface to use
- `storage_class`: Storage class configuration
  - `path`: Path to the storage class configuration file
- `metastore`: Metadata store
  - `type`: Type of metadata store
  - `ip`: IP address of the metadata store
  - `port`: Port of the metadata store
  - `ssl_ca_cert_path`: Path to an SSL/TLS CA cert for connections to the metadata store, leave blank if SSL/TLS is not used
  - `ssl_client_cert_path`: Path to an SSL/TLS client cert for connections to the metadata store, leave blank if SSL/TLS is not used
  - `ssl_client_key_path`: Path to an SSL/TLS client cert private key for connections to the metadata store, leave blank if SSL/TLS is not used
  - `ssl_trusted_certs_dir`: Path to an SSL/TLS-CA-cert-containing directory for connections to the metadata store, leave blank if not used 
  - `ssl_domain_name`: Domain name of the metadata store for SSL/TLS, leave blank if not used 
  - `auth_user`: User name for authentication, leave blank for passwordless access
  - `auth_password`: Password for authentication, leave blank for passwordless access
- `recovery`: Recovery
  - `trigger_enabled`: Whether to enable background automatic recovery
  - `trigger_start_interval`: Time between trying to trigger a recovery operation (in seconds)
  - `scan_interval`: Time between scanning of file metadata for files to recover (in seconds)
  - `batch_size`: Number of files to recover concurrently in each operation
  - `scan_chunk_interval`: Time between chunk existance and checksum verification (in hours)
  - `scan_chunk_batch_size`: Number of chunks to scan in a batch
  - `chunk_scan_sampling_policy`: Chunk scanning sampling policies
  - `chunk_scan_sampling_rate`: Chunk scanning sampling rate
- `data_distribution`: Data distribution
  - `policy`: Policy for distributing data to containers
  - `near_ip_range`: Space-separated ranges of agent IP addresses to consider as near (e.g., lower latency) to the proxy, e.g., 192.168.0.0/24 (leave blank if not needed)
- `background_write`: Write redundancy in background (alpha)
  - `ack_redundancy_in_background`: Whether to acknowledge write responses of redundancy in background
  - `write_redundancy_in_background`: Whether to write redundancy in background (note setting this to true will also set `ack_redundancy_in_background` to true)
  - `num_background_chunk_worker`: Number of background workers to handler chunk events in background
  - `background_task_check_interval`: Time between checks on background task status (in seconds)
- `misc`: Misc
  - `zmq_thread`: Number of threads in ZeroMQ context 
  - `repair_at_proxy`: Whether to perform data repair at the proxy (instead of an agent)
  - `overwrite_files`: Whether to remove old data chunks for overwrite
  - `reuse_data_connection`: Reuse data connections for chunk transfer
  - `liveness_cache_time`: Time to cache alive liveness status (in seconds)
  - `repair_using_car`: Whether to apply the improved repair technique
  - `agent_list`: list of agents to actively connect
- `zmq_interface`: ZeroMQ interface
  - `num_workers`: Number of workers request handling
  - `port`: Port number for ZeroMQ interface to listen on
- `immutable_mgt_apis`: RESTful APIs for immutable storage policy management
  - `enabled`: Whether to enable the APIs
  - `ip`: IP for the immutable policy management APIs to listen on
  - `port`: Port for the immutable policy management APIs to listen on
  - `num_workers`: Number of workers to handle requsts
  - `timeout`: Connection timeout in seconds
  - `ssl_cert`: Path to the SSL certificate file for HTTPS communication
  - `ssl_cert_key`: Path to the SSL certificate key file for HTTPS communication
  - `ssl_cert_password`: Path to the SSL certificate password file for HTTPS communication
  - `ssl_dh`: Path to the SSL DH parameter file for HTTPS communication
  - `jwt_private_key`: Path to the private key file for asymetric JWT token generation
  - `jwt_public_key`: Path to the public key file for asymetric JWT token generation
  - `jwt_secret_key`: Path to the secret key file for symetric JWT token generation
- `ldap_auth`: LDAP backed authentication (for authenticating administrators for immutable storage policy management)
  - `uri`: URI of the LDAP server
  - `user_organization`: User organization of the LDAP users
  - `dn_suffix`: DN suffix of the LDAP users
- `reporter_db`: Redis database for Reporter to store statistics
  - `ip`: IP for database (leave blank if reporter is not used)
  - `port`: Port of database
  - `record_buffer_size`: Maximum number of records to buffer 
- `staging`: Staging
  - `enabled`: Whether staging is enabled
  - `url`: File storage directory
  - `autoclean_policy`: Auto cleaning policy of staged file
  - `autoclean_num_days_expire`: Number of days a file has not been accessed before expiring it for auto-cleaning
  - `autoclean_scan_interval`: Auto-cleaning file scan interval (in seconds)
  - `bgwrite_policy`: Background write-back policy
  - `bgwrite_scan_interval`: Interval of checks for background write-back (in seconds)
  - `bgwrite_scheduled_time`: Scheduled time for daily background write in format 'hh:mm'

## Agent Configuration

In `agent.ini`,

- `agent`: Agent
  - `ip`: IP address
  - `port`: Port number for listening incoming chunk requests
  - `coord_port`: Port for listening incoming coordinator requests
  - `num_containers`: Number of managed containers
- `misc`: Misc
  - `num_workers`: Number of workers to handle chunk requests 
  - `zmq_thread`: Number of threads in ZeroMQ context 
  - `copy_block_size`: Block size for chunk copying (for containers on local file system)
  - `flush_on_close`: Whether to flush and sync data before file stream close for local file system containers
  - `register_to_proxy`: Whether to register to the list of proxies (in `general.ini`) on start 
- `container[00-99]`: Data containers
  - `type`: Container type; local file system: 'fs', Aliyun: 'alibaba', AWS S3: 'aws', Azure: 'azure', Generic S3: 'generic_s3'
  - `id`: Container id, must be *UNIQUE* among all containers of all agents
  - `url`: Location for chunk storage and access
    - Local file system: Directory path 
    - Aliyun, AWS S3, and generic S3: Bucket name
    - Azure: Storage account connection string
  - `region`: Region name for Aliyun, AWS S3, and generic S3, e.g. cn-hongkong, ap-east-1
  - `key_id`: Key ID for Aliyun, AWS S3, and generic S3
  - `key`: Secret key for Aliyun, AWS S3, and generic S3
  - `capacity`: Container capacity
  - `endpoint`: Endpoint (e.g., https://localhost:59002) for generic S3
  - `verify_ssl`: Whether to verify the SSL/TLS certificate for an HTTPS endpoint (e.g., https://localhost:59002) for generic S3

## Storage Class Configuration

In `storage_class.ini`, the section name should be a unique class name. Under each section (i.e., each class),

- `default`: Whether this class is a default
- `coding`: Coding scheme
- `n`: Coding parameter, n (or the total number of chunks)
- `k`: Coding parameter, k (or the number of data chunks)
- `f`: Minimum number of agent failures to tolerate
- `max_chunk_size`: Maximum size of a chunk
