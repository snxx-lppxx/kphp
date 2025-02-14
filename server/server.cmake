prepend(KPHP_SERVER_SOURCES ${BASE_DIR}/server/
        cluster-name.cpp
        confdata-binlog-replay.cpp
        confdata-stats.cpp
        json-logger.cpp
        lease-config-parser.cpp
        lease-rpc-client.cpp
        php-engine-vars.cpp
        php-engine.cpp
        php-lease.cpp
        php-master.cpp
        php-master-restart.cpp
        php-master-tl-handlers.cpp
        php-mc-connections.cpp
        php-queries.cpp
        php-queries-types.cpp
        php-query-data.cpp
        php-runner.cpp
        php-script.cpp
        php-sql-connections.cpp
        php-worker.cpp
        server-log.cpp
        server-stats.cpp
        slot-ids-factory.cpp
        workers-control.cpp
        )

prepend(KPHP_JOB_WORKERS_SOURCES ${BASE_DIR}/server/job-workers/
        job-stats.cpp
        job-worker-server.cpp
        job-worker-client.cpp
        job-workers-context.cpp
        pipe-io.cpp
        shared-memory-manager.cpp)

set(KPHP_SERVER_ALL_SOURCES
    ${KPHP_SERVER_SOURCES}
    ${KPHP_JOB_WORKERS_SOURCES})

allow_deprecated_declarations_for_apple(${BASE_DIR}/server/php-runner.cpp)
vk_add_library(kphp_server OBJECT ${KPHP_SERVER_ALL_SOURCES})
