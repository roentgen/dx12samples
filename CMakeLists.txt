cmake_minimum_required(VERSION 2.8.11)

project(dx12test)

set(CMAKE_BUILD_TYPE_INIT Debug)
# list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")

get_filename_component(THIRDPARTY ./thirdparty/ ABSOLUTE)
get_filename_component(THIRDPARTY ../ ABSOLUTE)

message(STATUS "Thirdparty:" ${THIRDPARTY})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(APPLE)
    set(THIRDPARTY_LIBS_HINTS "/usr/local/Cellar/" "/opt/include" ${THIRDPARTY})
elseif(WIN32 OR UNIX)
    set(THIRDPARTY_LIBS_HINTS ${THIRDPARTY})
endif()

if(CMAKE_SIZEOF_VOIDP EQUAL 8)
    set(PLATFORM 64)
endif()

if(WIN32)
    message ("Windows")
    add_definitions(-DWIN -DUNICODE -D_UNICODE)
elseif(APPLE)
    message ("macOS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64 -std=c++11 -stdlib=libc++")
    add_definitions(-DOSX)
endif()

# check libraries

set (BASESOURCES src/dbgutils.cpp src/serializer.cpp src/win32appbase.cpp src/stdafx.cpp)
set (SOURCES1 src/simple.cpp ${BASESOURCES})
set (SOURCES2 src/resources.cpp ${BASESOURCES})
set (SOURCES3 src/shadow.cpp src/shadowscene.cpp src/loading.cpp ${BASESOURCES})
set (SOURCES4 src/stereo.cpp src/vrscene.cpp src/loading.cpp ${BASESOURCES})

add_executable (simple WIN32 ${SOURCES1})
add_executable (resources WIN32 ${SOURCES2})
add_executable (shadow WIN32 ${SOURCES3})

target_link_libraries (simple d3d12.lib dxgi.lib d3dcompiler.lib shlwapi.lib)
target_link_libraries (resources d3d12.lib dxgi.lib d3dcompiler.lib shlwapi.lib)
target_link_libraries (shadow d3d12.lib dxgi.lib d3dcompiler.lib shlwapi.lib)

find_package(OpenVR)
if (OPENVR_FOUND)
    include_directories(${OPENVR_INCLUDE_DIR})
    add_executable (stereo WIN32 ${SOURCES4})
    target_link_libraries (stereo d3d12.lib dxgi.lib d3dcompiler.lib shlwapi.lib ${OPENVR_LIBRARIES})
    get_filename_component(dlls ${OPENVR_DLL} ABSOLUTE)
    STRING(REGEX REPLACE "/" "\\\\" dlls ${dlls})
    add_custom_command(TARGET stereo POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${dlls} $<TARGET_FILE_DIR:resources>)

    message("VR library found")
else()
    message("VR library not found")
endif()

macro(cdr var junk)
  SET(${var} ${ARGN})
endmacro(cdr)

function(add_shader_file proj)
    message("lst:${ARGN}")
    foreach(src_file ${ARGN})
        get_filename_component(src_file_path "${src_file}" ABSOLUTE)
        STRING(REGEX REPLACE "/" "\\\\" src_file_path ${src_file_path})
	message("path:${src_file_path}")
        add_custom_command(TARGET ${proj} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${src_file_path} $<TARGET_FILE_DIR:simple>)
    endforeach()

endfunction()

set(shaderlist src/scene.hlsl src/texture.hlsl src/loading.hlsl)

foreach(src_file ${shaderlist})
    get_filename_component(src_file_path "${src_file}" ABSOLUTE)
    message("src:${src_file_path}")
    STRING(REGEX REPLACE "/" "\\\\" src_file_path ${src_file_path})
    list(APPEND shaderfile "${src_file_path}")
endforeach()

message("files:${shaderfile}")

get_filename_component(assets assets/ ABSOLUTE)
STRING(REGEX REPLACE "/" "\\\\" assets ${assets})

add_shader_file(simple src/texture.hlsl)
add_shader_file(resources src/loading.hlsl src/texture.hlsl)
add_shader_file(shadow src/loading.hlsl src/shadow.hlsl src/scene.hlsl)
add_shader_file(stereo src/loading.hlsl src/massive.hlsl)

#add_custom_command(TARGET simple POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${shaderfile} $<TARGET_FILE_DIR:simple>)
#$add_custom_command(TARGET resources POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${shaderfile} $<TARGET_FILE_DIR:resources>)
#add_custom_command(TARGET reuse POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${shaderfile} $<TARGET_FILE_DIR:resources>)

add_custom_command(TARGET resources POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${assets} $<TARGET_FILE_DIR:resources>)
add_custom_command(TARGET stereo POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${assets} $<TARGET_FILE_DIR:resources>)
add_custom_command(TARGET shadow POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${assets} $<TARGET_FILE_DIR:resources>)
