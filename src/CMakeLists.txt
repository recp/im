FILE(GLOB CSources *.h *.c)
target_sources(${PROJECT_NAME} 
  PRIVATE
  ${CSources}
)

add_subdirectory(arch)
add_subdirectory(interop/swift)
add_subdirectory(io)
add_subdirectory(mm)
add_subdirectory(posix)
add_subdirectory(pp)
add_subdirectory(thread)

if(MSVC OR MSYS OR MINGW)
  add_subdirectory(win)
endif()
