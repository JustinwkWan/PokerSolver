# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-src")
  file(MAKE_DIRECTORY "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-src")
endif()
file(MAKE_DIRECTORY
  "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-build"
  "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix"
  "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix/tmp"
  "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix/src/ompeval-populate-stamp"
  "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix/src"
  "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix/src/ompeval-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix/src/ompeval-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/justinwan/Documents/Vscode Projects/PokerSolver/build/_deps/ompeval-subbuild/ompeval-populate-prefix/src/ompeval-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
