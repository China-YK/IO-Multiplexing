cmake_minimum_required(VERSION 4.0)
project(IOReuse)

set(CMAKE_CXX_STANDARD 17)

add_executable(Select src/Select.cpp)

add_executable(Epoll src/Epoll.cpp)
