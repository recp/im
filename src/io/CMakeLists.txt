FILE(GLOB CSources *.h *.c)
target_sources(${PROJECT_NAME} 
  PRIVATE
  ${CSources}
)

# the formats asked for (IM_FORMATS); Apple's fallback always
foreach(f png bmp ppm qoi tga jpg jp2 ccitt jbig2 jxl heic)
  if(f IN_LIST IM_ON)
    add_subdirectory(${f})
  endif()
endforeach()
add_subdirectory(apple)

if(MSVC OR MSYS OR MINGW)
  add_subdirectory(win)
endif()
