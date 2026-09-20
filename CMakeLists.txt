cmake_minimum_required(VERSION 3.24)

project(Wayfarer VERSION 0.2.1 LANGUAGES CXX)
if(PROJECT_VERSION_PATCH STREQUAL "")
    set(PROJECT_VERSION_PATCH 0)
endif()

option(BUILD_SKYRIM "Build the Skyrim SE/AE plugin" ON)

if(NOT BUILD_SKYRIM)
    message(FATAL_ERROR "Wayfarer currently supports Skyrim SE/AE only.")
endif()

configure_file(cmake/Plugin.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Plugin.h @ONLY)
configure_file(cmake/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc @ONLY)

set(WAYFARER_COMMONLIB_SOURCE "" CACHE PATH "Optional modern CommonLibSSE-NG source tree for 1.7 runtime builds")
if(WAYFARER_COMMONLIB_SOURCE)
    set(BUILD_TESTS OFF CACHE BOOL "Build CommonLib's own tests" FORCE)
    add_subdirectory(${WAYFARER_COMMONLIB_SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/commonlib)
    get_target_property(commonlibImported CommonLibSSE IMPORTED)
    if(MSVC AND NOT commonlibImported)
        target_compile_options(CommonLibSSE PRIVATE /MP8)
    endif()
else()
    find_package(CommonLibSSE CONFIG REQUIRED)
endif()
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

set(WAYFARER_SOURCES
    src/formation_controller.cpp
    src/dialogue_management.cpp
    src/victory_celebration.cpp
    src/victory_animation.cpp
    src/tether_clasp.cpp
    third_party/OpenAnimationReplacer/OpenAnimationReplacerAPI-Conditions.cpp
    third_party/OpenAnimationReplacer/OpenAnimationReplacer-ConditionTypes.cpp
    src/formation_math.cpp
    src/travel_planner.cpp
    src/role_assignment.cpp
    src/party_behavior.cpp
    src/social_planner.cpp
    src/travel_navigation.cpp
    src/obstacle_avoidance.cpp
    src/travel_speed.cpp
    src/menu_ui.cpp
    src/order_visuals.cpp
    src/command_gesture.cpp
    src/gesture_head_tracking.cpp
    src/hand_holding_tether.cpp
    src/havok_compatibility.cpp
    src/input_handler.cpp
    src/papyrus_bridge.cpp
    src/config.cpp
    src/plugin.cpp
)

add_library(Wayfarer SHARED ${WAYFARER_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc)
target_compile_features(Wayfarer PRIVATE cxx_std_23)
target_compile_definitions(Wayfarer PRIVATE SKYRIM _UNICODE WAYFARER_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN)
if(WAYFARER_COMMONLIB_SOURCE)
    target_compile_definitions(Wayfarer PRIVATE WAYFARER_MODERN_COMMONLIB=1)
endif()
target_include_directories(Wayfarer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated ${SIMPLEINI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenAnimationReplacer
)
target_link_libraries(Wayfarer PRIVATE CommonLibSSE::CommonLibSSE)
target_precompile_headers(Wayfarer PRIVATE include/precompiled.h)

if(MSVC)
    target_compile_options(Wayfarer PRIVATE /MP /W4 /permissive- /Zc:__cplusplus)
    target_link_options(Wayfarer PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
endif()

set_target_properties(Wayfarer PROPERTIES
    OUTPUT_NAME "Wayfarer"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)

enable_testing()
add_executable(WayfarerTetherTests tests/tether_handoff_tests.cpp src/travel_planner.cpp src/formation_math.cpp)
target_compile_features(WayfarerTetherTests PRIVATE cxx_std_20)
target_include_directories(WayfarerTetherTests PRIVATE include)
add_test(NAME WayfarerTether COMMAND WayfarerTetherTests)
add_executable(WayfarerPackageControlTests tests/package_control_tests.cpp)
target_compile_features(WayfarerPackageControlTests PRIVATE cxx_std_20)
target_include_directories(WayfarerPackageControlTests PRIVATE include)
add_test(NAME WayfarerPackageControl COMMAND WayfarerPackageControlTests)
add_executable(WayfarerFollowerDialogueTests tests/follower_dialogue_tests.cpp)
target_compile_features(WayfarerFollowerDialogueTests PRIVATE cxx_std_20)
target_include_directories(WayfarerFollowerDialogueTests PRIVATE include)
add_test(NAME WayfarerFollowerDialogue COMMAND WayfarerFollowerDialogueTests)
add_executable(WayfarerObstacleTests tests/obstacle_avoidance_tests.cpp src/obstacle_avoidance.cpp src/formation_math.cpp)
target_compile_features(WayfarerObstacleTests PRIVATE cxx_std_20)
target_include_directories(WayfarerObstacleTests PRIVATE include)
add_test(NAME WayfarerObstacles COMMAND WayfarerObstacleTests)
add_executable(WayfarerCatchupTests tests/follower_catchup_tests.cpp)
target_compile_features(WayfarerCatchupTests PRIVATE cxx_std_20)
target_include_directories(WayfarerCatchupTests PRIVATE include)
add_test(NAME WayfarerCatchup COMMAND WayfarerCatchupTests)
add_executable(WayfarerKeyChordTests tests/key_chord_tests.cpp)
target_compile_features(WayfarerKeyChordTests PRIVATE cxx_std_20)
target_include_directories(WayfarerKeyChordTests PRIVATE include ${SIMPLEINI_INCLUDE_DIRS})
add_test(NAME WayfarerKeyChords COMMAND WayfarerKeyChordTests)
add_executable(WayfarerVictoryTests tests/victory_encounter_tests.cpp)
target_compile_features(WayfarerVictoryTests PRIVATE cxx_std_20)
target_include_directories(WayfarerVictoryTests PRIVATE include)
add_test(NAME WayfarerVictory COMMAND WayfarerVictoryTests)
add_executable(WayfarerHandHoldTests tests/hand_hold_tests.cpp src/travel_planner.cpp src/formation_math.cpp)
target_compile_features(WayfarerHandHoldTests PRIVATE cxx_std_20)
target_include_directories(WayfarerHandHoldTests PRIVATE include)
add_test(NAME WayfarerHandHolding COMMAND WayfarerHandHoldTests)
set(WAYFARER_IMGUI_TEST_SOURCE "" CACHE PATH "Optional Menu Framework ImGui source directory for mouse integration tests")
if(WAYFARER_IMGUI_TEST_SOURCE)
    add_executable(WayfarerWheelMouseTests tests/wheel_mouse_integration.cpp
        ${WAYFARER_IMGUI_TEST_SOURCE}/imgui.cpp ${WAYFARER_IMGUI_TEST_SOURCE}/imgui_widgets.cpp
        ${WAYFARER_IMGUI_TEST_SOURCE}/imgui_draw.cpp ${WAYFARER_IMGUI_TEST_SOURCE}/imgui_tables.cpp)
    target_compile_features(WayfarerWheelMouseTests PRIVATE cxx_std_20)
    target_include_directories(WayfarerWheelMouseTests PRIVATE include ${WAYFARER_IMGUI_TEST_SOURCE})
    add_test(NAME WayfarerWheelMouse COMMAND WayfarerWheelMouseTests)
endif()
add_executable(WayfarerBanterTests tests/travel_banter_tests.cpp src/role_assignment.cpp src/formation_math.cpp)
target_compile_features(WayfarerBanterTests PRIVATE cxx_std_20)
target_include_directories(WayfarerBanterTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerBanter COMMAND WayfarerBanterTests)
add_executable(WayfarerCustomFollowerTests tests/custom_follower_tests.cpp)
target_compile_features(WayfarerCustomFollowerTests PRIVATE cxx_std_20)
target_include_directories(WayfarerCustomFollowerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${SIMPLEINI_INCLUDE_DIRS})
add_test(NAME WayfarerCustomFollowers COMMAND WayfarerCustomFollowerTests)
add_executable(WayfarerRuntimeTests tests/runtime_compatibility_tests.cpp)
target_compile_features(WayfarerRuntimeTests PRIVATE cxx_std_20)
target_include_directories(WayfarerRuntimeTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerRuntimeCompatibility COMMAND WayfarerRuntimeTests)
add_executable(WayfarerOrderTests tests/order_wheel_tests.cpp)
target_compile_features(WayfarerOrderTests PRIVATE cxx_std_20)
target_include_directories(WayfarerOrderTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerOrders COMMAND WayfarerOrderTests)
add_executable(WayfarerGestureAwarenessTests tests/gesture_awareness_tests.cpp)
target_compile_features(WayfarerGestureAwarenessTests PRIVATE cxx_std_20)
target_include_directories(WayfarerGestureAwarenessTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerGestureAwareness COMMAND WayfarerGestureAwarenessTests)
add_executable(WayfarerMathTests tests/formation_math_tests.cpp src/formation_math.cpp)
target_compile_features(WayfarerMathTests PRIVATE cxx_std_20)
target_include_directories(WayfarerMathTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(WayfarerMathTests PRIVATE /W4 /permissive-)
endif()
add_test(NAME WayfarerMath COMMAND WayfarerMathTests)

add_executable(WayfarerTravelTests tests/travel_planner_tests.cpp src/travel_planner.cpp src/formation_math.cpp)
target_compile_features(WayfarerTravelTests PRIVATE cxx_std_20)
target_include_directories(WayfarerTravelTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerTravel COMMAND WayfarerTravelTests)

add_executable(WayfarerPartyTests tests/party_layout_tests.cpp src/travel_planner.cpp src/formation_math.cpp src/role_assignment.cpp)
target_compile_features(WayfarerPartyTests PRIVATE cxx_std_20)
target_include_directories(WayfarerPartyTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerParty COMMAND WayfarerPartyTests)

add_executable(WayfarerBehaviorTests tests/party_behavior_tests.cpp src/party_behavior.cpp src/social_planner.cpp src/travel_planner.cpp src/formation_math.cpp src/role_assignment.cpp)
target_compile_features(WayfarerBehaviorTests PRIVATE cxx_std_20)
target_include_directories(WayfarerBehaviorTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerBehavior COMMAND WayfarerBehaviorTests)

add_executable(WayfarerRestNavigationTests tests/rest_navigation_tests.cpp src/formation_math.cpp)
target_compile_features(WayfarerRestNavigationTests PRIVATE cxx_std_20)
target_include_directories(WayfarerRestNavigationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WayfarerRestNavigation COMMAND WayfarerRestNavigationTests)
