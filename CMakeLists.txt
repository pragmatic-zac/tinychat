cmake_minimum_required(VERSION 3.27)
project(tinychat C)

set(CMAKE_C_STANDARD 23)

add_executable(tinychat main.c
        chatlib.h
        chatlib.c
        chatlib.h)
