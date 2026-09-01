cmake_minimum_required(VERSION 3.20)

add_executable(bench_hash bench_hash.c)
target_link_libraries(bench_hash PRIVATE fastgit)

add_executable(bench_odb bench_odb.c)
target_link_libraries(bench_odb PRIVATE fastgit)

add_executable(bench_index bench_index.c)
target_link_libraries(bench_index PRIVATE fastgit)

add_executable(bench_diff bench_diff.c)
target_link_libraries(bench_diff PRIVATE fastgit)

add_executable(bench_pack bench_pack.c)
target_link_libraries(bench_pack PRIVATE fastgit)

add_executable(bench_full bench_full.c)
target_link_libraries(bench_full PRIVATE fastgit)