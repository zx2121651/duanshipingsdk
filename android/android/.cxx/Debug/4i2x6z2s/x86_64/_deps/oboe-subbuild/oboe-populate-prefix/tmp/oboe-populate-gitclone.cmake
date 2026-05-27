
if(NOT "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-subbuild/oboe-populate-prefix/src/oboe-populate-stamp/oboe-populate-gitinfo.txt" IS_NEWER_THAN "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-subbuild/oboe-populate-prefix/src/oboe-populate-stamp/oboe-populate-gitclone-lastrun.txt")
  message(STATUS "Avoiding repeated git clone, stamp file is up to date: 'D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-subbuild/oboe-populate-prefix/src/oboe-populate-stamp/oboe-populate-gitclone-lastrun.txt'")
  return()
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-src"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to remove directory: 'D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-src'")
endif()

# try the clone 3 times in case there is an odd git clone issue
set(error_code 1)
set(number_of_tries 0)
while(error_code AND number_of_tries LESS 3)
  execute_process(
    COMMAND "C:/Program Files/Git/cmd/git.exe"  clone --no-checkout --config "advice.detachedHead=false" "https://github.com/google/oboe.git" "oboe-src"
    WORKING_DIRECTORY "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps"
    RESULT_VARIABLE error_code
    )
  math(EXPR number_of_tries "${number_of_tries} + 1")
endwhile()
if(number_of_tries GREATER 1)
  message(STATUS "Had to git clone more than once:
          ${number_of_tries} times.")
endif()
if(error_code)
  message(FATAL_ERROR "Failed to clone repository: 'https://github.com/google/oboe.git'")
endif()

execute_process(
  COMMAND "C:/Program Files/Git/cmd/git.exe"  checkout 1.8.0 --
  WORKING_DIRECTORY "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-src"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to checkout tag: '1.8.0'")
endif()

set(init_submodules TRUE)
if(init_submodules)
  execute_process(
    COMMAND "C:/Program Files/Git/cmd/git.exe"  submodule update --recursive --init 
    WORKING_DIRECTORY "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-src"
    RESULT_VARIABLE error_code
    )
endif()
if(error_code)
  message(FATAL_ERROR "Failed to update submodules in: 'D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-src'")
endif()

# Complete success, update the script-last-run stamp file:
#
execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy
    "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-subbuild/oboe-populate-prefix/src/oboe-populate-stamp/oboe-populate-gitinfo.txt"
    "D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-subbuild/oboe-populate-prefix/src/oboe-populate-stamp/oboe-populate-gitclone-lastrun.txt"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to copy script-last-run stamp file: 'D:/duanshipingsdk-main/android/android/.cxx/Debug/4i2x6z2s/x86_64/_deps/oboe-subbuild/oboe-populate-prefix/src/oboe-populate-stamp/oboe-populate-gitclone-lastrun.txt'")
endif()

