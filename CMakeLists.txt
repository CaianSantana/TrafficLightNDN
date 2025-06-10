cmake_minimum_required(VERSION 3.14)
project(TCC_Semaforo_Inteligente LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Usa pkg-config para encontrar o ndn-cxx
find_package(PkgConfig REQUIRED)
pkg_check_modules(NDN REQUIRED libndn-cxx)

include_directories(${NDN_INCLUDE_DIRS})
link_directories(${NDN_LIBRARY_DIRS})
add_definitions(${NDN_CFLAGS_OTHER})

# Para baixar o yaml-cpp
include(FetchContent)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
)
FetchContent_MakeAvailable(yaml-cpp)

# Adiciona o executável e vincula bibliotecas
add_executable(meu_tcc
    main.cpp
    TrafficControlCenter.cpp
)

target_link_libraries(meu_tcc
    ${NDN_LIBRARIES}  # linka com o ndn-cxx via pkg-config
    yaml-cpp
)
