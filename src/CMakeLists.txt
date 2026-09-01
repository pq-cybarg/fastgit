cmake_minimum_required(VERSION 3.20)

set(FASTGIT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/hash/hash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/hash/sha256.c
    ${CMAKE_CURRENT_SOURCE_DIR}/hash/sha384.c
    ${CMAKE_CURRENT_SOURCE_DIR}/hash/sha3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/object/object.c
    ${CMAKE_CURRENT_SOURCE_DIR}/object/blob.c
    ${CMAKE_CURRENT_SOURCE_DIR}/object/tree.c
    ${CMAKE_CURRENT_SOURCE_DIR}/object/commit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/object/tag.c
    ${CMAKE_CURRENT_SOURCE_DIR}/odb/odb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/odb/loose.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pack/packfile.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pack/delta.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pack/midx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/index/index.c
    ${CMAKE_CURRENT_SOURCE_DIR}/index/entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/worktree/worktree.c
    ${CMAKE_CURRENT_SOURCE_DIR}/worktree/checkout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/worktree/status.c
    ${CMAKE_CURRENT_SOURCE_DIR}/worktree/diff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/network/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/network/smart_http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/network/ssh.c
    ${CMAKE_CURRENT_SOURCE_DIR}/network/pqc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/merge/merge.c
    ${CMAKE_CURRENT_SOURCE_DIR}/merge/rebase.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/io_uring.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/iocp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/kqueue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/simd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cli/cli.c
    ${CMAKE_CURRENT_SOURCE_DIR}/repository.c
)

if(FASTGIT_BUILD_SHARED)
    add_library(fastgit SHARED ${FASTGIT_SOURCES})
    set_target_properties(fastgit PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 0
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(fastgit PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    )
endif()

if(FASTGIT_BUILD_STATIC)
    add_library(fastgit_static STATIC ${FASTGIT_SOURCES})
    set_target_properties(fastgit_static PROPERTIES
        OUTPUT_NAME fastgit
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(fastgit_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    )
endif()

if(FASTGIT_USE_OPENSSL)
    target_link_libraries(fastgit PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()
if(CURL_FOUND)
    target_include_directories(fastgit PUBLIC ${CURL_INCLUDE_DIRS})
    target_link_libraries(fastgit PUBLIC ${CURL_LIBRARIES})
endif()
if(LIBSSH_FOUND)
    target_include_directories(fastgit PUBLIC ${LIBSSH_INCLUDE_DIRS})
    target_link_directories(fastgit PUBLIC ${LIBSSH_LIBRARY_DIRS})
    target_link_libraries(fastgit PUBLIC ${LIBSSH_LIBRARIES})
endif()

target_link_libraries(fastgit PUBLIC Threads::Threads ZLIB::ZLIB)

if(TARGET fastgit_static)
    target_link_libraries(fastgit_static PUBLIC Threads::Threads ZLIB::ZLIB)
    if(FASTGIT_USE_OPENSSL)
        target_link_libraries(fastgit_static PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
    if(CURL_FOUND)
        target_include_directories(fastgit_static PUBLIC ${CURL_INCLUDE_DIRS})
        target_link_libraries(fastgit_static PUBLIC ${CURL_LIBRARIES})
    endif()
    if(LIBSSH_FOUND)
        target_include_directories(fastgit_static PUBLIC ${LIBSSH_INCLUDE_DIRS})
        target_link_directories(fastgit_static PUBLIC ${LIBSSH_LIBRARY_DIRS})
        target_link_libraries(fastgit_static PUBLIC ${LIBSSH_LIBRARIES})
    endif()
endif()

add_executable(fastgit-cli cli/main.c)
target_link_libraries(fastgit-cli PRIVATE fastgit)