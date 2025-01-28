if(APPLE)
  FILE(GLOB CSources *.h *.c *.m)
else()
  FILE(GLOB CSources *.h *.c)
endif()

target_sources(${PROJECT_NAME} 
  PRIVATE
  ${CSources}
)