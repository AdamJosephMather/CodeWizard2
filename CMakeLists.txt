cmake_minimum_required(VERSION 3.15)
project(CodeWizard)

# debug...

# if (MSVC)
# 	message(STATUS "Building with MSVC AddressSanitizer enabled")
# 
# 	# Let CMake pick the correct /MDd or /MD (recommended for ASan on MSVC)
# 	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# 
# 	add_compile_options(
# 		/fsanitize=address
# 		/Zi
# 	)
# 
# 	add_link_options(
# 		/fsanitize=address
# 		/INCREMENTAL:NO
# 		/DEBUG:FULL
# 		# REMOVE this line:
# 		# /WHOLEARCHIVE:clang_rt.asan-x86_64.lib
# 	)
# endif()




set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# adress sanitization on linux?
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")


# Set the icon file (Windows only)
if(WIN32)
	set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc")
endif()

if (MSVC)
	add_compile_options(/wd4244 /wd4305 /wd4267)
endif()


include(FetchContent)

FetchContent_Declare(
	sioclient
	GIT_REPOSITORY https://github.com/socketio/socket.io-client-cpp.git
	GIT_TAG master # or a specific version like 3.1.0
	GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(sioclient)

# FetchContent_Declare(
# 	tokenizers_cpp
# 	GIT_REPOSITORY https://github.com/mlc-ai/tokenizers-cpp.git
# 	# Do NOT use v0.1.1 here (it predates the fix).
# 	# Pin to a newer commit on main:
# 	GIT_TAG        55d53aa
# )
# FetchContent_MakeAvailable(tokenizers_cpp)

set(SYNTECT_BRIDGE_DIR ${CMAKE_SOURCE_DIR}/third_party/syntect_bridge)

if(WIN32)
	set(SYNTECT_LIB ${SYNTECT_BRIDGE_DIR}/target/release/syntect_bridge.lib)
else()
	set(SYNTECT_LIB ${SYNTECT_BRIDGE_DIR}/target/release/libsyntect_bridge.a)
endif()

add_custom_command(
	OUTPUT ${SYNTECT_LIB}
	COMMAND cargo build --release
	WORKING_DIRECTORY ${SYNTECT_BRIDGE_DIR}
	COMMENT "Building syntect_bridge with Cargo"
)

add_custom_target(syntect_bridge_build DEPENDS ${SYNTECT_LIB})

add_library(syntect_bridge STATIC IMPORTED GLOBAL)
set_target_properties(syntect_bridge PROPERTIES
	IMPORTED_LOCATION ${SYNTECT_LIB}
	INTERFACE_INCLUDE_DIRECTORIES ${SYNTECT_BRIDGE_DIR}/include
)

add_dependencies(syntect_bridge syntect_bridge_build)

add_definitions(-DNOMINMAX)
if(WIN32)
	# Windows needs these for ConPTY APIs
	add_definitions(-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000008 -DUNICODE -D_UNICODE)
endif()


add_executable(${PROJECT_NAME} main.cpp ${APP_ICON_RESOURCE_WINDOWS} text_renderer.cpp application.cpp widget.cpp button.cpp titlebar.cpp panel_holder.cpp widgetchooser.cpp editor.cpp filetree.cpp filebackend.cpp sshfilebackend.cpp tabs.cpp settings.cpp codeedit.cpp linenumbers.cpp textedit.cpp tinyfiledialogs.c brokenstatemenu.cpp checkbox.cpp settingsmanager.cpp languageserverclient.cpp listbox.cpp imageview.cpp compare.cpp curler.cpp toast.cpp chat.cpp label.cpp myrect.cpp lspdebug.cpp terminal.cpp terminalwidget.cpp terminalwidgettabbed.cpp helpmenu.cpp scrollbar.cpp updatechecker.cpp contextmenu.cpp scrollnotify.cpp mathwindow.cpp asteroids.cpp graphwindow.cpp md4c.c EmojiRenderer.cpp hexeditor.cpp MonoString.cpp statusbar.cpp )
# modelxrunner.cpp 

include(cmake/LibGhostty.cmake)
codewizard_target_use_libghostty(CodeWizard)

# set(Torch_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libtorch/share/cmake/Torch" CACHE PATH "" FORCE)


# Use vcpkg-provided GLFW and ICU
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ICU   REQUIRED COMPONENTS uc i18n data)  # must list each component :contentReference[oaicite:0]{index=0}
find_package(Freetype REQUIRED)
find_package(CURL CONFIG REQUIRED)
# find_package(Torch REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	glfw
	OpenGL::GL
	ICU::uc            # core Unicode (formerly icuuc) :contentReference[oaicite:1]{index=1}
	ICU::i18n          # internationalization (formerly icui18n)
	ICU::data          # ICU data blobs (formerly icudata)
	Freetype::Freetype
	CURL::libcurl
	sioclient
	OpenSSL::Crypto
	syntect_bridge
	"${CMAKE_SOURCE_DIR}/grapheme.lib"
)

if(WIN32)
	target_link_libraries(${PROJECT_NAME}
		PRIVATE
		
		# ${TORCH_LIBRARIES}
		# tokenizers_cpp
		
		ole32
		shell32
		strsafe
		dwmapi
		userenv
		ntdll
	)
endif()

if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()

configure_file(${CMAKE_SOURCE_DIR}/app.png
			   ${CMAKE_BINARY_DIR}/app.png
			   COPYONLY)

configure_file(${CMAKE_SOURCE_DIR}/fileIcon.png
			   ${CMAKE_BINARY_DIR}/fileIcon.png
			   COPYONLY)

configure_file(${CMAKE_SOURCE_DIR}/folderIcon.png
			   ${CMAKE_BINARY_DIR}/folderIcon.png
			   COPYONLY)

# if (WIN32)
# 	# Copy libtorch runtime DLLs next to the executable
# 	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
# 		COMMAND ${CMAKE_COMMAND} -E copy_directory
# 						"${CMAKE_CURRENT_SOURCE_DIR}/libtorch/lib"
# 						"$<TARGET_FILE_DIR:${PROJECT_NAME}>"
# 		COMMENT "Copying libtorch DLLs to output folder"
# 		VERBATIM
# 	)
# endif()

set(SPLASHSCREEN "${CMAKE_SOURCE_DIR}/build_needs/splashscreen.png")

if(NOT EXISTS "${SPLASHSCREEN}")
	message(FATAL_ERROR "splashscreen not found at: ${SPLASHSCREEN}")
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${SPLASHSCREEN}"
					"$<TARGET_FILE_DIR:${PROJECT_NAME}>/splashscreen.png"
	COMMENT "Copying SPLASHSCREEN to output folder"
	VERBATIM
)

if(MSVC AND WIN32)
	set(VC_REDIST_EXE "${CMAKE_SOURCE_DIR}/build_needs/vc_redist.x64.exe")

	if(NOT EXISTS "${VC_REDIST_EXE}")
		message(FATAL_ERROR "vc_redist not found at: ${VC_REDIST_EXE}")
	endif()

	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
						"${VC_REDIST_EXE}"
						"$<TARGET_FILE_DIR:${PROJECT_NAME}>/vc_redist.x64.exe"
		COMMENT "Copying VC++ Redistributable to output folder"
		VERBATIM
	)
endif()

# Define your source and destination paths
set(CASCADIA_SRC_DIR "${CMAKE_SOURCE_DIR}/cascadia")
set(CASCADIA_BUILD_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/cascadia")

# Optional: Verify the source directory actually exists before proceeding
if(NOT EXISTS "${CASCADIA_SRC_DIR}")
	message(FATAL_ERROR "Cascadia directory not found at: ${CASCADIA_SRC_DIR}")
endif()

# Post-build command to copy the directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CASCADIA_SRC_DIR}"
			"${CASCADIA_BUILD_DIR}"
	COMMENT "Copying Cascadia assets to build output directory"
	VERBATIM
)

# message(STATUS "Torch_DIR=${Torch_DIR}")
# message(STATUS "TORCH_LIBRARIES=${TORCH_LIBRARIES}")


# if(MSVC AND WIN32)
# 	target_link_libraries(${PROJECT_NAME} PRIVATE delayimp)
# 	target_link_options(${PROJECT_NAME} PRIVATE
# 		"/DELAYLOAD:torch_cpu.dll"
# 	)
# endif()







if (MSVC)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		WIN32_EXECUTABLE TRUE
	)
endif()
