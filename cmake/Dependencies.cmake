# These lists are later turned into target properties on main dau_conv_impl library target
set(DAUConvNet_LINKER_LIBS "")
set(DAUConvNet_INCLUDE_DIRS "")
set(DAUConvNet_DEFINITIONS "")
set(DAUConvNet_COMPILE_OPTIONS "")

# we get strange error when DAUConvNet_DEFINITIONS is empty so we just fill it with gibrish definition to make compiler hapopy :)
list(APPEND DAUConvNet_DEFINITIONS PUBLIC -DDUMMYXYMMUD)

# ---[ CUDA
include(cmake/Cuda.cmake)
if(NOT HAVE_CUDA)
  if(CPU_ONLY)
    message(STATUS "-- CUDA is disabled. Building without it...")
  else()
    message(WARNING "-- CUDA is not detected by cmake. Building without it...")
  endif()

  list(APPEND DAUConvNet_DEFINITIONS PUBLIC -DCPU_ONLY)
endif()

# ---[ BLAS
if(NOT APPLE)
  set(BLAS "Atlas" CACHE STRING "Selected BLAS library")
  set_property(CACHE BLAS PROPERTY STRINGS "Atlas;Open;MKL")

  if(BLAS STREQUAL "Atlas" OR BLAS STREQUAL "atlas")
    find_package(Atlas REQUIRED)
    list(APPEND DAUConvNet_INCLUDE_DIRS PUBLIC ${Atlas_INCLUDE_DIR})
    list(APPEND DAUConvNet_LINKER_LIBS PUBLIC ${Atlas_LIBRARIES})
  elseif(BLAS STREQUAL "Open" OR BLAS STREQUAL "open")
    find_package(OpenBLAS REQUIRED)
    list(APPEND DAUConvNet_INCLUDE_DIRS PUBLIC ${OpenBLAS_INCLUDE_DIR})
    list(APPEND DAUConvNet_LINKER_LIBS PUBLIC ${OpenBLAS_LIB})
  elseif(BLAS STREQUAL "MKL" OR BLAS STREQUAL "mkl")
    find_package(MKL REQUIRED)
    list(APPEND DAUConvNet_INCLUDE_DIRS PUBLIC ${MKL_INCLUDE_DIR})
    list(APPEND DAUConvNet_LINKER_LIBS PUBLIC ${MKL_LIBRARIES})
    list(APPEND DAUConvNet_DEFINITIONS PUBLIC -DUSE_MKL)
  endif()
elseif(APPLE)
  find_package(vecLib REQUIRED)
  list(APPEND DAUConvNet_INCLUDE_DIRS PUBLIC ${vecLib_INCLUDE_DIR})
  list(APPEND DAUConvNet_LINKER_LIBS PUBLIC ${vecLib_LINKER_LIBS})

  if(VECLIB_FOUND)
    if(NOT vecLib_INCLUDE_DIR MATCHES "^/System/Library/Frameworks/vecLib.framework.*")
      list(APPEND DAUConvNet_DEFINITIONS PUBLIC -DUSE_ACCELERATE)
    endif()
  endif()
endif()
