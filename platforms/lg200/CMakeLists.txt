add_hal_lib(lg200)
set_target_properties(hallg200 PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE)

option(LG200_KCD_LEVEL2
       "Enable LG200 KCD queue deactivation for the OpenCL HAL." OFF)

if(LG200_KCD_LEVEL2)
    if(NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
        message(FATAL_ERROR "LG200_KCD_LEVEL2 is only supported on Linux")
    endif()
    if(NOT PLATFORM_OPENCL)
        message(FATAL_ERROR
            "LG200_KCD_LEVEL2 requires -DPLATFORM_OPENCL=ON")
    endif()

    target_sources(hallg200 PRIVATE
        level2/kcd_queue_control.cpp
        level2/lg200_queue.cpp
    )
    target_include_directories(hallg200 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/level2
    )
    target_link_libraries(hallg200
        PUBLIC halopencl
        PRIVATE ${CMAKE_DL_LIBS}
    )
    install(FILES hal/include/xsched/lg200/level2.h
            DESTINATION include/xsched/lg200)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(hallg200 PRIVATE stdc++fs)
endif()

add_executable(lg200_info tools/lg200_info.cpp)
target_include_directories(lg200_info PRIVATE
    ${XSCHED_INCLUDE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/hal/include
)
target_compile_options(lg200_info PRIVATE -Wall -Wextra -Werror)
target_link_libraries(lg200_info PRIVATE hallg200)
install(TARGETS lg200_info RUNTIME DESTINATION bin/lg200)
set_target_properties(lg200_info PROPERTIES
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

add_platform_test()
