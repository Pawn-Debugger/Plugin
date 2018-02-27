include(AMXConfig)
include(AddSAMPPlugin)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/amx
  ${CMAKE_CURRENT_BINARY_DIR}
)

if(WIN32)
  add_definitions(-D_WIN32_WINNT=_WIN32_WINNT_WINXP
                  -D_CRT_SECURE_NO_WARNINGS
                  -D_WINSOCK_DEPRECATED_NO_WARNINGS
                  -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
endif()

add_definitions(-DASIO_STANDALONE)

set(DEBUG_PLUGIN_SOURCES
  amxcallstack.cpp
  amxcallstack.h
  amxdebuginfo.cpp
  amxdebuginfo.h
  amxerror.cpp
  amxerror.h
  amxopcode.cpp
  amxopcode.h
  amxpathfinder.cpp
  amxpathfinder.h
  amxscript.cpp
  amxscript.h
  amxservice.h
  amxstacktrace.cpp
  amxstacktrace.h
  amxexecutor.h
  amxexecutor.cpp
  debugplugin.cpp
  debugplugin.h
  fileutils.cpp
  fileutils.h
  log.cpp
  log.h
  logprintf.cpp
  logprintf.h
  natives.cpp
  natives.h
  network.cpp
  network.h
  networkconnection.cpp
  networkconnection.h
  os.h
  plugin.cpp
  plugin.def
  plugincommon.h
  regexp.cpp
  regexp.h
  safequeue.h
  stacktrace.cpp
  stacktrace.h
)

configure_file(plugin.rc.in plugin.rc @ONLY)
configure_file(pluginversion.h.in pluginversion.h @ONLY)

list(APPEND DEBUG_PLUGIN_SOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/plugin.rc
  ${CMAKE_CURRENT_BINARY_DIR}/pluginversion.h
)

if(WIN32 OR CYGWIN)
  list(APPEND DEBUG_PLUGIN_SOURCES
    fileutils-win32.cpp
    os-win32.cpp
    stacktrace-win32.cpp
  )
else()
  list(APPEND DEBUG_PLUGIN_SOURCES
    fileutils-unix.cpp
    os-unix.cpp
    stacktrace-unix.cpp
  )
endif()

add_samp_plugin(debug-plugin ${DEBUG_PLUGIN_SOURCES})

add_subdirectory(amx)
add_subdirectory(proto)
target_link_Libraries(debug-plugin messages amx configreader pcre subhook)

if(WIN32)
  target_link_libraries(debug-plugin DbgHelp)
endif()

install(TARGETS debug-plugin LIBRARY DESTINATION ".")

if(MSVC_IDE AND MSVC_VERSION GREATER 1200)
  set(config \${CMAKE_INSTALL_CONFIG_NAME})
  set(pdb_name debug-plugin.pdb)
  set(pdb_path ${CMAKE_CURRENT_BINARY_DIR}/${config}/${pdb_name})
  install(FILES ${pdb_path} DESTINATION ".")
endif()

set(CMAKE_CXX_STANDARD 17)
if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")

  # Already fixed on probuf repo, should not be needed after update to >3.5.1 
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4146")
endif(MSVC)