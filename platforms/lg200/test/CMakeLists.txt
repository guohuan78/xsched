if(NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
    return()
endif()

add_executable(lg200_resource_test lg200_resource_test.cpp)
target_compile_options(lg200_resource_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(lg200_resource_test PRIVATE hallg200)
add_test(NAME lg200_resource_test COMMAND lg200_resource_test)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(lg200_resource_test PRIVATE stdc++fs)
endif()

if(LG200_KCD_LEVEL2)
    add_executable(lg200_kcd_queue_control_test
        lg200_kcd_queue_control_test.cpp
        ../level2/kcd_queue_control.cpp
    )
    target_include_directories(lg200_kcd_queue_control_test PRIVATE
        ${XSCHED_INCLUDE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../hal/include
        ${CMAKE_CURRENT_LIST_DIR}/../level2
    )
    target_compile_definitions(lg200_kcd_queue_control_test PRIVATE
        XSCHED_LG200_KCD_TEST
    )
    target_compile_options(lg200_kcd_queue_control_test PRIVATE
        -Wall -Wextra -Werror
    )
    target_link_libraries(lg200_kcd_queue_control_test PRIVATE
        ${CMAKE_DL_LIBS}
    )
    add_test(NAME lg200_kcd_queue_control_test
             COMMAND lg200_kcd_queue_control_test)

    add_executable(lg200_level2_contract_test
        lg200_level2_contract_test.cpp
    )
    target_compile_options(lg200_level2_contract_test PRIVATE
        -Wall -Wextra -Werror
    )
    target_link_libraries(lg200_level2_contract_test PRIVATE
        preempt
        utils
    )
    add_test(NAME lg200_level2_contract_test
             COMMAND lg200_level2_contract_test)
endif()
