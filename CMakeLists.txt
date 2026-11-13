cmake_minimum_required(VERSION 3.22)

# Muss vor project() stehen
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimale macOS-Version")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AudioPlugins VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Jedes Plugin bekommt einen eigenen Ordner
add_subdirectory(Earrection)
