# Platform-appropriate tests. The Linux tests mirror linux/Makefile.linux;
# Windows tests remain driven by the root Makefile for now.

if(WB_BUILD_LINUX AND NOT WIN32)
    # These fixtures use assert(); keep checks active in Release builds too.
    add_compile_options(-UNDEBUG)

    foreach(policy_test battery_state hid_inventory logitech_idle)
        add_executable(${policy_test}_test ${policy_test}_test.cpp)
        target_link_libraries(${policy_test}_test PRIVATE wb_headers)
        target_compile_options(${policy_test}_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ${policy_test}_test COMMAND ${policy_test}_test)
    endforeach()

    add_executable(linux_hid_test linux_hid_test.cpp)
    target_link_libraries(linux_hid_test PRIVATE wb_headers wb_drivers)
    target_compile_options(linux_hid_test PRIVATE -Wall -Wno-missing-field-initializers)
    add_test(NAME linux_hid_test COMMAND linux_hid_test)

    add_executable(json_output_test json_output_test.cpp)
    target_link_libraries(json_output_test PRIVATE wb_headers)
    add_test(NAME json_output_test COMMAND json_output_test)
endif()
