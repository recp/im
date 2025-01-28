FILE(GLOB CSources *.h *.c)
target_sources(${PROJECT_NAME} 
  PRIVATE
  ${CSources}
)

add_subdirectory(png)
add_subdirectory(bmp)
add_subdirectory(ppm)
add_subdirectory(qoi)
add_subdirectory(tga)

add_subdirectory(jpg)
add_subdirectory(jp2)
add_subdirectory(jxl)
add_subdirectory(heic)
add_subdirectory(apple)

if(MSVC OR MSYS OR MINGW)
  add_subdirectory(win)
endif()
