cmake_minimum_required(VERSION 3.16)
project(openvr_virtual_driver)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenVR
set(OPENVR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/openvr)
set(OPENVR_INCLUDE_DIR ${OPENVR_DIR}/headers)

# Driver output name
set(DRIVER_NAME "openvr_virtual_driver")

# Source files
add_library(driver_${DRIVER_NAME} SHARED
    src/hmd_driver_factory.cpp
    src/device_provider.cpp
    src/hmd_device_driver.cpp
    src/socket_manager.cpp
)

target_include_directories(driver_${DRIVER_NAME} PRIVATE
    ${OPENVR_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windows specific
if(WIN32)
    # Link D3D11 for texture handling, ws2_32 for TCP
    target_link_libraries(driver_${DRIVER_NAME} PRIVATE
        d3d11
        dxgi
        ws2_32
    )

    # Output to driver folder structure
    set_target_properties(driver_${DRIVER_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/ai_vopenvr_virtual_driver/bin/win64"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/openvr_virtual_driver/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/openvr_virtual_driver/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/openvr_virtual_driver/bin/win64"
    )
endif()

target_compile_definitions(driver_${DRIVER_NAME} PRIVATE
    -DNOMINMAX
    -DWIN32_LEAN_AND_MEAN
)
