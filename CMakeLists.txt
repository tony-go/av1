cmake_minimum_required(VERSION 3.10)
project(ffmpeg_example C)

add_executable(ffmpeg_example main.c)

# Force Homebrew paths (Apple Silicon)
target_include_directories(ffmpeg_example PRIVATE /opt/homebrew/include)
target_link_directories(ffmpeg_example PRIVATE /opt/homebrew/lib)

target_link_libraries(ffmpeg_example PRIVATE
  avdevice
  avformat
  avcodec
  avutil
  swresample
  swscale
  sdl2
)
