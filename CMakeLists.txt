cmake_minimum_required(VERSION 3.10)
project(ffmpeg_example C)

# Find FFmpeg packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(AVDEVICE REQUIRED libavdevice)

# Add executable
add_executable(ffmpeg_example main.c)

# Include directories
target_include_directories(ffmpeg_example PRIVATE
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${AVDEVICE_INCLUDE_DIRS}
)

# Link directories
target_link_directories(ffmpeg_example PRIVATE
    ${AVCODEC_LIBRARY_DIRS}
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
    ${AVDEVICE_LIBRARY_DIRS}
)

# Link libraries with full paths
target_link_libraries(ffmpeg_example PRIVATE
    ${AVCODEC_LDFLAGS}
    ${AVFORMAT_LDFLAGS}
    ${AVUTIL_LDFLAGS}
    ${AVDEVICE_LDFLAGS}
) 
