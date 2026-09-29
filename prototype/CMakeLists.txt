cmake_minimum_required(VERSION 3.22)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" ICEPUNK_VERSION LIMIT_COUNT 1)
project(ICEPUNKPrototype VERSION ${ICEPUNK_VERSION} LANGUAGES C CXX)
option(ICEPUNK_DEVELOPMENT_ENGINE "Allow source Python bridge in development builds" ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(ICEPUNK_DEVELOPMENT_ENGINE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 91ad83ae34a81e0833b1a2b0866f54846370ae53)
FetchContent_MakeAvailable(JUCE)
juce_add_gui_app(ICEPUNK PRODUCT_NAME "ICEPUNK" COMPANY_NAME "ICEPUNK"
    BUNDLE_ID "audio.icepunk.midigenerator" VERSION ${PROJECT_VERSION})
juce_generate_juce_header(ICEPUNK)
if(ICEPUNK_DEVELOPMENT_ENGINE)
    set_target_properties(ICEPUNK PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../app")
else()
    set_target_properties(ICEPUNK PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/release-app")
endif()
if(MSVC)
    set_property(TARGET ICEPUNK PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
target_sources(ICEPUNK PRIVATE src/Main.cpp src/GeneratorEngine.cpp src/MidiPreview.cpp src/MainComponent.cpp src/RuntimePaths.cpp)
target_compile_definitions(ICEPUNK PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ICEPUNK,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    ICEPUNK_DEVELOPMENT_ENGINE=$<BOOL:${ICEPUNK_DEVELOPMENT_ENGINE}>)
target_link_libraries(ICEPUNK PRIVATE juce::juce_gui_basics juce::juce_audio_basics
    juce::juce_recommended_config_flags)
configure_file(AppConfig.h.in generated/AppConfig.h @ONLY)
target_include_directories(ICEPUNK PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
enable_testing()
if(ICEPUNK_DEVELOPMENT_ENGINE)
    add_test(NAME PythonIntegration COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py" -v)
endif()
