cmake_minimum_required(VERSION 3.10)
project(CodeWizard)

# debug...

# if(MSVC)
# 		message(STATUS "Building with MSVC AddressSanitizer enabled")
# 		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # Remove DLL
# 		add_compile_options(
# 			/fsanitize=address
# 			/Zi
# 			/MT$<$<CONFIG:Debug>:d>  # Static runtime
# 		)
# 		add_link_options(
# 			/fsanitize=address
# 			/INCREMENTAL:NO
# 			/DEBUG:FULL
# 			/WHOLEARCHIVE:clang_rt.asan-x86_64.lib  # Static ASan lib
# 		)
# endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the icon file
set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc")

if (MSVC)
	add_compile_options(/wd4244 /wd4305 /wd4267)
endif()


include(FetchContent)
FetchContent_Declare(
	libvterm_src
	GIT_REPOSITORY https://github.com/neovim/libvterm.git
	GIT_TAG        nvim
)
FetchContent_MakeAvailable(libvterm_src)

FetchContent_Declare(
	tokenizers_cpp
	GIT_REPOSITORY https://github.com/mlc-ai/tokenizers-cpp.git
	# Do NOT use v0.1.1 here (it predates the fix).
	# Pin to a newer commit on main:
	GIT_TAG        55d53aa
)
FetchContent_MakeAvailable(tokenizers_cpp)

set(LIBVTERM_DIR ${libvterm_src_SOURCE_DIR})
add_library(libvterm STATIC
	${LIBVTERM_DIR}/src/encoding.c
	${LIBVTERM_DIR}/src/keyboard.c
	${LIBVTERM_DIR}/src/mouse.c
	${LIBVTERM_DIR}/src/parser.c
	${LIBVTERM_DIR}/src/pen.c
	${LIBVTERM_DIR}/src/screen.c
	${LIBVTERM_DIR}/src/state.c
	${LIBVTERM_DIR}/src/unicode.c
	${LIBVTERM_DIR}/src/vterm.c
)

set_property(TARGET libvterm PROPERTY C_STANDARD 99)
target_include_directories(libvterm PUBLIC
	${LIBVTERM_DIR}/include
	${LIBVTERM_DIR}/src           # headers live here too (vterm_internal.h, utf8.h, rect.h)
)

add_definitions(-DNOMINMAX)
# Windows needs these for ConPTY APIs
add_definitions(-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000008 -DUNICODE -D_UNICODE)


add_executable(${PROJECT_NAME} main.cpp ${APP_ICON_RESOURCE_WINDOWS} text_renderer.cpp application.cpp widget.cpp button.cpp titlebar.cpp panel_holder.cpp widgetchooser.cpp editor.cpp filetree.cpp tabs.cpp settings.cpp codeedit.cpp linenumbers.cpp textedit.cpp highlighter.cpp tinyfiledialogs.c brokenstatemenu.cpp checkbox.cpp settingsmanager.cpp languageserverclient.cpp listbox.cpp imageview.cpp compare.cpp curler.cpp toast.cpp chat.cpp label.cpp myrect.cpp lspdebug.cpp terminal.cpp terminalwidget.cpp terminalwidgettabbed.cpp helpmenu.cpp scrollbar.cpp updatechecker.cpp contextmenu.cpp scrollnotify.cpp modelxrunner.cpp )


set(Torch_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libtorch/share/cmake/Torch" CACHE PATH "" FORCE)


# Use vcpkg-provided GLFW and ICU
find_package(glfw3 CONFIG REQUIRED)
find_package(ICU   REQUIRED COMPONENTS uc i18n data)  # must list each component :contentReference[oaicite:0]{index=0}
find_package(Freetype REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(Torch REQUIRED)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	glfw
	opengl32
	ICU::uc            # core Unicode (formerly icuuc) :contentReference[oaicite:1]{index=1}
	ICU::i18n          # internationalization (formerly icui18n)
	ICU::data          # ICU data blobs (formerly icudata)
	Freetype::Freetype
	CURL::libcurl
	libvterm
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	
	${TORCH_LIBRARIES}
	tokenizers_cpp
	
	ole32
	shell32
	strsafe
	dwmapi
)

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

if (WIN32)
	# Copy libtorch runtime DLLs next to the executable
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
						"${CMAKE_CURRENT_SOURCE_DIR}/libtorch/lib"
						"$<TARGET_FILE_DIR:${PROJECT_NAME}>"
		COMMENT "Copying libtorch DLLs to output folder"
		VERBATIM
	)
endif()

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

message(STATUS "Torch_DIR=${Torch_DIR}")
message(STATUS "TORCH_LIBRARIES=${TORCH_LIBRARIES}")


if(MSVC AND WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE delayimp)
	target_link_options(${PROJECT_NAME} PRIVATE
		"/DELAYLOAD:torch_cpu.dll"
	)
endif()







# if (MSVC)
# 	set_target_properties(${PROJECT_NAME} PROPERTIES
# 		WIN32_EXECUTABLE TRUE
# 	)
# endif()