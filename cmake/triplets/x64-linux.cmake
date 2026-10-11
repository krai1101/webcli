set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
# Keep LGPL libraries replaceable in distributed binaries.
if(PORT STREQUAL "libidn2" OR PORT STREQUAL "libunistring")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
