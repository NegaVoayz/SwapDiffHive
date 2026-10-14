add_executable(free_chain_test free_chain_test.cc)
target_link_libraries(free_chain_test PRIVATE SwapDiffHive)
add_test(NAME free_chain COMMAND free_chain_test)

add_executable(hive_test swap_diff_hive_test.cc)
target_link_libraries(hive_test PRIVATE SwapDiffHive)
target_compile_definitions(hive_test PRIVATE SWAP_DIFF_HIVE_TEST)
add_test(NAME hive_protocol COMMAND hive_test)
