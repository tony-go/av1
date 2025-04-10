cmake_minimum_required(VERSION 3.10)
project(capture_av1 C)

add_executable(capture_av1 main.c)

target_include_directories(capture_av1 PRIVATE /opt/homebrew/include)
target_link_directories(capture_av1 PRIVATE /opt/homebrew/lib)
target_link_libraries(capture_av1 PRIVATE
  avdevice
  avformat
  avcodec
  avutil
  swresample
  swscale
  sdl2
)
