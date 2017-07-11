cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(dx12)

# version number
set(tracer_MAJOR_VERSION 0)
set(tracer_MINOR_VERSION 1)

###
# Set preprocessor flags for certain configurations
###
set(RELESE_FLAGS "-DDXP_NDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${RELESE_FLAGS}")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -stdlib=libc++")
elseif (WIN32 AND NOT MSYS AND NOT CYGWIN AND NOT MINGW)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
endif()


find_library(D3D12_LIB d3d12 "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.15063.0/um/x64")
find_library(DXGI_LIB dxgi "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.15063.0/um/x64")
find_library(D3DCOMPILER_LIB d3dcompiler "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.15063.0/um/x64")


set(MAIN_DIR ${PROJECT_SOURCE_DIR}/src)
set(MAIN_SRCS
	${MAIN_DIR}/d3dx12.h
	${MAIN_DIR}/dx.h
	${MAIN_DIR}/dx.cpp
	${MAIN_DIR}/main.cpp
)

set(PROJECT_SRC ${PROJECT_SOURCE_DIR})
configure_file(${MAIN_DIR}/config.h.in ${MAIN_DIR}/config.h)

include_directories(${MAIN_DIR}
	${PROJECT_SOURCE_DIR}/external/directxmath/Inc
)

# ###
# # DEPENDENCIES
# ###
# set(DEPENDENCIES_DIR ${PROJECT_SOURCE_DIR}/dependencies)
# # if ("${CMAKE_SYSTEM}" MATCHES "Linux")	
# 	# find_library(LIB_PNG		libpng16.a ${DEPENDENCIES_DIR}/libpng/lib)
# 	# find_library(LIB_Z			z ${DEPENDENCIES_DIR}/zlib/lib)
# 	# find_library(LIB_ASSIMP_D	assimp ${DEPENDENCIES_DIR}/assimp/lib)
# 	# find_library(LIB_ASSIMP		assimp ${DEPENDENCIES_DIR}/assimp/lib)
# # else()
# 	find_library(LIB_PNG		libpng16 ${DEPENDENCIES_DIR}/libpng/lib)
# 	find_library(LIB_Z			zlib ${DEPENDENCIES_DIR}/zlib/lib)
# # endif()

# if ("${CMAKE_SYSTEM}" MATCHES "Linux")
# 	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread")
# endif()

# include_directories(
# 	${DEPENDENCIES_DIR}/assimp/include
# 	${DEPENDENCIES_DIR}/libpng/include
# 	${PROJECT_SOURCE_DIR}/include
# )

###
# COMPILING
###
add_executable(dx12 WIN32 ${MAIN_SRCS})

target_link_libraries(dx12 ${D3D12_LIB} ${DXGI_LIB} ${D3DCOMPILER_LIB})

# ADD_CUSTOM_COMMAND(
# 	TARGET tracer
# 	POST_BUILD
# 	COMMAND copy ARGS ${DEPENDENCIES_DIR}/assimp/lib/assimpl.dll
# 	${PROJECT_BINARY_DIR}
# )

# source_group("include\\libcore" FILES ${LIBCORE_HDRS})
# source_group("include\\librender" FILES ${LIBRENDER_HDRS})

source_group("src" FILES ${MAIN_SRCS})
