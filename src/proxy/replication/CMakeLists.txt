#####################
## Proxy Replication ##
#####################

# 只包含已经实现的sentinel_client.cc文件
file( GLOB replication_source sentinel/sentinel_client.cc )
add_library( ncloud_replication STATIC EXCLUDE_FROM_ALL ${replication_source} )
add_dependencies( ncloud_replication hiredis-cli google-log )
target_link_libraries( ncloud_replication ncloud_common hiredis hiredis_ssl event OpenSSL::SSL OpenSSL::Crypto glog pthread ) 