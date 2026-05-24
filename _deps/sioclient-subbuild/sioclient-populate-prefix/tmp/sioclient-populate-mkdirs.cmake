# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-src")
  file(MAKE_DIRECTORY "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-src")
endif()
file(MAKE_DIRECTORY
  "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-build"
  "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix"
  "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix/tmp"
  "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix/src/sioclient-populate-stamp"
  "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix/src"
  "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix/src/sioclient-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix/src/sioclient-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/adamj/Documents/C/CodeWizard2/_deps/sioclient-subbuild/sioclient-populate-prefix/src/sioclient-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
