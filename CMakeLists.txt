cmake_minimum_required(VERSION 3.10)
project(CodeWizard)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the icon file
set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc")

add_executable(${PROJECT_NAME} main.cpp ${APP_ICON_RESOURCE_WINDOWS} text_renderer.cpp application.cpp widget.cpp button.cpp titlebar.cpp panel_holder.cpp widgetchooser.cpp editor.cpp filetree.cpp tabs.cpp settings.cpp codeedit.cpp linenumbers.cpp textedit.cpp highlighter.cpp tinyfiledialogs.c brokenstatemenu.cpp checkbox.cpp settingsmanager.cpp languageserverclient.cpp listbox.cpp imageview.cpp compare.cpp curler.cpp toast.cpp )

# Use vcpkg-provided GLFW and ICU
find_package(glfw3 CONFIG REQUIRED)
find_package(ICU   REQUIRED COMPONENTS uc i18n data)  # must list each component :contentReference[oaicite:0]{index=0}
find_package(Freetype REQUIRED)
find_package(CURL CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	glfw
	opengl32
	ICU::uc            # core Unicode (formerly icuuc) :contentReference[oaicite:1]{index=1}
	ICU::i18n          # internationalization (formerly icui18n)
	ICU::data          # ICU data blobs (formerly icudata)
	Freetype::Freetype
	CURL::libcurl
)

find_library(ONIG_LIB
	NAMES onigstatic onig
	HINTS ${VCPKG_INSTALLED_DIR}/installed/${VCPKG_TARGET_TRIPLET}/lib
)
if (NOT ONIG_LIB)
	message(FATAL_ERROR
	"Could not find Oniguruma library (onigstatic.lib or onig.lib) in vcpkg tree")
endif()

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	${ONIG_LIB}
	
	ole32
	shell32
	strsafe
	dwmapi
)

if (MSVC)
	set_target_properties(${PROJECT_NAME} PROPERTIES
	WIN32_EXECUTABLE TRUE
	)
endif()