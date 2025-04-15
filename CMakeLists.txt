cmake_minimum_required(VERSION 3.10)
project(capture C)

add_executable(capture main.c)

target_include_directories(capture PRIVATE /opt/homebrew/include)
target_link_directories(capture PRIVATE /opt/homebrew/lib)
target_link_libraries(capture PRIVATE
  avdevice
  avformat
  avcodec
  avutil
  swresample
  swscale
  sdl2
)
