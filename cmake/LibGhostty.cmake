# libghostty-vt integration for CodeWizard.
# Include this after project() and after your executable/library target exists.
#
# Requires CMake 3.19+ for string(JSON ...).

include_guard(GLOBAL)
include(FetchContent)

set(CODEWIZARD_GHOSTTY_GIT_TAG
    "53bd14fecfd68c6c0ab64d37b5943247299e2b40"
    CACHE STRING "Pinned Ghostty commit used by CodeWizard")

set(CODEWIZARD_GHOSTTY_ZIG_VERSION
    "0.15.2"
    CACHE STRING "Zig version used only to build libghostty-vt")

option(CODEWIZARD_GHOSTTY_DOWNLOAD_ZIG
       "Download a private Zig toolchain for libghostty-vt"
       ON)

set(CODEWIZARD_GHOSTTY_ZIG_EXECUTABLE
    ""
    CACHE FILEPATH
    "Optional explicit Zig executable for libghostty-vt")

option(CODEWIZARD_GHOSTTY_STATIC
       "Link libghostty-vt statically instead of using the shared library"
       OFF)

function(_codewizard_select_zig_platform out_key out_executable)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _processor)

    if(_processor MATCHES "^(amd64|x86_64|x64)$")
        set(_arch "x86_64")
    elseif(_processor MATCHES "^(aarch64|arm64)$")
        set(_arch "aarch64")
    else()
        message(FATAL_ERROR
            "Unsupported host architecture for automatic Zig download: "
            "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    if(CMAKE_HOST_WIN32)
        set(_os "windows")
        set(_exe "zig.exe")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_os "linux")
        set(_exe "zig")
    else()
        message(FATAL_ERROR
            "Automatic Zig download currently supports Windows and Linux only. "
            "Set CODEWIZARD_GHOSTTY_ZIG_EXECUTABLE manually on this platform.")
    endif()

    set(${out_key} "${_arch}-${_os}" PARENT_SCOPE)
    set(${out_executable} "${_exe}" PARENT_SCOPE)
endfunction()

function(_codewizard_prepare_zig out_var)
    if(CODEWIZARD_GHOSTTY_ZIG_EXECUTABLE)
        set(_zig "${CODEWIZARD_GHOSTTY_ZIG_EXECUTABLE}")
        if(NOT EXISTS "${_zig}")
            message(FATAL_ERROR
                "CODEWIZARD_GHOSTTY_ZIG_EXECUTABLE does not exist: ${_zig}")
        endif()
    elseif(CODEWIZARD_GHOSTTY_DOWNLOAD_ZIG)
        _codewizard_select_zig_platform(_platform_key _zig_filename)

        set(_download_root "${CMAKE_BINARY_DIR}/_downloads")
        set(_tool_root "${CMAKE_BINARY_DIR}/_tools/zig-${CODEWIZARD_GHOSTTY_ZIG_VERSION}")
        set(_index_file "${_download_root}/zig-download-index.json")

        file(MAKE_DIRECTORY "${_download_root}")
        file(MAKE_DIRECTORY "${_tool_root}")

        file(DOWNLOAD
            "https://ziglang.org/download/index.json"
            "${_index_file}"
            TLS_VERIFY ON
            STATUS _index_status
            LOG _index_log)

        list(GET _index_status 0 _index_code)
        list(GET _index_status 1 _index_message)
        if(NOT _index_code EQUAL 0)
            message(FATAL_ERROR
                "Failed to download Zig release manifest: ${_index_message}\n"
                "${_index_log}")
        endif()

        file(READ "${_index_file}" _zig_index)

        string(JSON _release ERROR_VARIABLE _release_error
               GET "${_zig_index}" "${CODEWIZARD_GHOSTTY_ZIG_VERSION}")
        if(_release_error)
            message(FATAL_ERROR
                "Zig ${CODEWIZARD_GHOSTTY_ZIG_VERSION} was not found in "
                "https://ziglang.org/download/index.json: ${_release_error}")
        endif()

        string(JSON _package ERROR_VARIABLE _package_error
               GET "${_release}" "${_platform_key}")
        if(_package_error)
            message(FATAL_ERROR
                "No Zig ${CODEWIZARD_GHOSTTY_ZIG_VERSION} package exists for "
                "${_platform_key}: ${_package_error}")
        endif()

        string(JSON _archive_url GET "${_package}" "tarball")
        string(JSON _archive_hash GET "${_package}" "shasum")

        get_filename_component(_archive_name "${_archive_url}" NAME)
        set(_archive_path "${_download_root}/${_archive_name}")
        set(_stamp_path
            "${_tool_root}/.codewizard-zig-${CODEWIZARD_GHOSTTY_ZIG_VERSION}-ready")

        if(NOT EXISTS "${_stamp_path}")
            message(STATUS
                "Downloading private Zig ${CODEWIZARD_GHOSTTY_ZIG_VERSION} "
                "for libghostty-vt (${_platform_key})")

            file(DOWNLOAD
                "${_archive_url}"
                "${_archive_path}"
                EXPECTED_HASH "SHA256=${_archive_hash}"
                TLS_VERIFY ON
                SHOW_PROGRESS
                STATUS _archive_status
                LOG _archive_log)

            list(GET _archive_status 0 _archive_code)
            list(GET _archive_status 1 _archive_message)
            if(NOT _archive_code EQUAL 0)
                message(FATAL_ERROR
                    "Failed to download Zig: ${_archive_message}\n"
                    "${_archive_log}")
            endif()

            file(GLOB _old_tool_contents "${_tool_root}/*")
            if(_old_tool_contents)
                file(REMOVE_RECURSE ${_old_tool_contents})
            endif()

            file(ARCHIVE_EXTRACT
                INPUT "${_archive_path}"
                DESTINATION "${_tool_root}")

            file(WRITE "${_stamp_path}" "ready\n")
        endif()

        file(GLOB_RECURSE _zig_candidates
            LIST_DIRECTORIES FALSE
            "${_tool_root}/${_zig_filename}")

        list(LENGTH _zig_candidates _zig_candidate_count)
        if(_zig_candidate_count EQUAL 0)
            message(FATAL_ERROR
                "Zig was extracted, but ${_zig_filename} was not found under "
                "${_tool_root}")
        endif()

        list(GET _zig_candidates 0 _zig)
    else()
        find_program(_zig NAMES zig REQUIRED)
    endif()

    file(TO_CMAKE_PATH "${_zig}" _zig)

    execute_process(
        COMMAND "${_zig}" version
        RESULT_VARIABLE _version_result
        OUTPUT_VARIABLE _version_output
        ERROR_VARIABLE _version_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)

    if(NOT _version_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to run Zig at ${_zig}:\n${_version_error}")
    endif()

    if(NOT _version_output STREQUAL CODEWIZARD_GHOSTTY_ZIG_VERSION)
        message(FATAL_ERROR
            "libghostty requires Zig ${CODEWIZARD_GHOSTTY_ZIG_VERSION}, but "
            "${_zig} reports ${_version_output}")
    endif()

    message(STATUS
        "Using Zig ${_version_output} for libghostty-vt: ${_zig}")

    set(${out_var} "${_zig}" PARENT_SCOPE)
endfunction()

_codewizard_prepare_zig(_codewizard_zig)

set(ZIG_EXECUTABLE "${_codewizard_zig}"
    CACHE FILEPATH "Zig used by Ghostty" FORCE)

get_filename_component(_codewizard_zig_dir
    "${_codewizard_zig}" DIRECTORY)

if(WIN32)
    set(ENV{PATH} "${_codewizard_zig_dir};$ENV{PATH}")
else()
    set(ENV{PATH} "${_codewizard_zig_dir}:$ENV{PATH}")
endif()

set(_codewizard_ghostty_flags "${GHOSTTY_ZIG_BUILD_FLAGS}")

# Force the Zig global cache onto the same drive as the build tree so that
# Run-step path conversions never cross drive letters (Zig 0.15.2 bug
# ziglang/zig#25805).
set(_codewizard_global_cache "${CMAKE_BINARY_DIR}/_zig-global-cache")
file(MAKE_DIRECTORY "${_codewizard_global_cache}")
list(APPEND _codewizard_ghostty_flags
    "--global-cache-dir" "${_codewizard_global_cache}")

if(WIN32 AND CODEWIZARD_GHOSTTY_STATIC)
    if(NOT _codewizard_ghostty_flags MATCHES "(^|[ ;])-Dsimd=false($|[ ;])")
        list(APPEND _codewizard_ghostty_flags "-Dsimd=false")
    endif()
endif()

set(GHOSTTY_ZIG_BUILD_FLAGS "${_codewizard_ghostty_flags}"
    CACHE STRING "Additional flags passed to Ghostty's Zig build" FORCE)
unset(_codewizard_ghostty_flags)

FetchContent_Declare(
    ghostty
    GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
    GIT_TAG "${CODEWIZARD_GHOSTTY_GIT_TAG}"
    GIT_SHALLOW FALSE
)

FetchContent_MakeAvailable(ghostty)

function(codewizard_target_use_libghostty target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "codewizard_target_use_libghostty: target '${target_name}' does not exist")
    endif()

    if(CODEWIZARD_GHOSTTY_STATIC)
        target_link_libraries("${target_name}" PRIVATE ghostty-vt-static)
    else()
        target_link_libraries("${target_name}" PRIVATE ghostty-vt)

        if(WIN32)
            add_custom_command(TARGET "${target_name}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "$<TARGET_FILE:ghostty-vt>"
                        "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM)
        endif()
    endif()
endfunction()
