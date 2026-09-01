cmake_minimum_required(VERSION 3.20)

enable_testing()

# Tests are harnesses: relax strict warnings
add_compile_options(-Wno-deprecated-declarations -Wno-newline-eof -Wno-error)

add_executable(test_hash test_hash.c)
target_link_libraries(test_hash PRIVATE fastgit)
add_test(NAME test_hash COMMAND test_hash)

add_executable(test_object test_object.c)
target_link_libraries(test_object PRIVATE fastgit)
add_test(NAME test_object COMMAND test_object)

add_executable(test_odb test_odb.c)
target_link_libraries(test_odb PRIVATE fastgit)
add_test(NAME test_odb COMMAND test_odb)

add_executable(test_index test_index.c)
target_link_libraries(test_index PRIVATE fastgit)
add_test(NAME test_index COMMAND test_index)

add_executable(test_worktree test_worktree.c)
target_link_libraries(test_worktree PRIVATE fastgit)
add_test(NAME test_worktree COMMAND test_worktree)

add_executable(test_pack test_pack.c)
target_link_libraries(test_pack PRIVATE fastgit)
add_test(NAME test_pack COMMAND test_pack)

add_executable(test_merge test_merge.c)
target_link_libraries(test_merge PRIVATE fastgit)
add_test(NAME test_merge COMMAND test_merge)

add_executable(test_platform test_platform.c)
target_link_libraries(test_platform PRIVATE fastgit)
add_test(NAME test_platform COMMAND test_platform)

add_executable(test_cli test_cli.c)
target_link_libraries(test_cli PRIVATE fastgit)
add_test(NAME test_cli COMMAND test_cli)
