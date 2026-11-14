cmake_minimum_required(VERSION 3.16)
project(arca VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opcionais de compilacao
option(ARCA_BUILD_TESTS "Compilar os executaveis de teste de bancada" ON)
option(ARCA_OPTIMIZE_NATIVE "Habilitar -march=native para maxima performance na maquina local" OFF)

if(ARCA_OPTIMIZE_NATIVE)
    add_compile_options(-march=native)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O2
        -pipe
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

# Definicoes globais
add_definitions(-DNANOVG_GLES2)

# Includes principais (permite #include "Base/...", "Modelo/...", "nanovg/...")
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/nanovg
)

# Dependencias via PkgConfig e FindPackage
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(X11 REQUIRED)

pkg_check_modules(FLAC REQUIRED flac)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(SANE REQUIRED sane-backends)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV2 REQUIRED glesv2)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswresample libavutil)

# Procura nlohmann_json no sistema; se nao achar, usa fallback ou cabecalho do sistema
find_package(nlohmann_json QUIET)

# Fontes do NanoVG
set(NANOVG_SOURCES
    nanovg/nanovg_gles2.cpp
)

# Fontes dos modulos do ARCA
file(GLOB_RECURSE BASE_SOURCES "Base/*.cpp")
file(GLOB_RECURSE MODELO_SOURCES "Modelo/*.cpp")
file(GLOB_RECURSE INTEROP_SOURCES "Interop/*.cpp")
file(GLOB_RECURSE VISTAS_SOURCES "Vistas/*.cpp")

set(ARCA_LIB_SOURCES
    ${NANOVG_SOURCES}
    ${BASE_SOURCES}
    ${MODELO_SOURCES}
    ${INTEROP_SOURCES}
    ${VISTAS_SOURCES}
)

# Biblioteca estatica compartilhada entre a aplicacao e os testes
add_library(arca_core STATIC ${ARCA_LIB_SOURCES})

target_include_directories(arca_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FLAC_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${SANE_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)

target_link_libraries(arca_core PUBLIC
    Threads::Threads
    OpenSSL::Crypto
    JPEG::JPEG
    ZLIB::ZLIB
    ${X11_LIBRARIES}
    ${FLAC_LIBRARIES}
    ${CURL_LIBRARIES}
    ${SANE_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
    ${FFMPEG_LIBRARIES}
)

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(arca_core PUBLIC nlohmann_json::nlohmann_json)
endif()

# Executavel Principal do ARCA
add_executable(arca xos-arca.cpp)
target_link_libraries(arca PRIVATE arca_core)

# Executaveis de Teste
if(ARCA_BUILD_TESTS)
    file(GLOB TEST_SOURCES "Testes/*.cpp")
    foreach(TEST_SRC ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} PRIVATE arca_core)
    endforeach()
endif()

# Regras de instalacao
install(TARGETS arca DESTINATION bin)
install(FILES arca.ico DESTINATION share/arca)
