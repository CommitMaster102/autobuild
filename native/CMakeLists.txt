cmake_minimum_required(VERSION 3.16)

project(autobuild_native
  VERSION 2.0
  LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable UTF-8 source encoding to handle FontAwesome Unicode characters
if(MSVC)
  add_compile_options(/utf-8)
endif()

option(OVERRIDE_ABORT_FUNCTION "Override abort() function to prevent crashes from assertion failures" OFF)

include(GNUInstallDirs)

# On Windows/MSVC, arrange to install the VC++ runtime (vcruntime/msvcp)
if(MSVC)
  include(InstallRequiredSystemLibraries)
endif()


# GUI (SDL2)
find_package(SDL2 QUIET)
if(SDL2_FOUND)

  # Modern ImGui GUI (full-featured)
  set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
  set(FONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")
  if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    # Find OpenGL
    find_package(OpenGL REQUIRED)

    # OpenGL Animation executable (main entry point)
    add_executable(autobuild_gui
      apps/opengl_animation.cpp
      apps/mesh.cpp
      apps/loadShader.cpp
      glad/src/glad.c
    )
    target_include_directories(autobuild_gui PRIVATE glad/include glm)
    target_link_libraries(autobuild_gui PRIVATE SDL2::SDL2 ${OPENGL_LIBRARIES})
    target_compile_definitions(autobuild_gui PRIVATE SDL_MAIN_HANDLED)

    # Hide console window for OpenGL animation on all platforms
    if(WIN32)
      set_target_properties(autobuild_gui PROPERTIES WIN32_EXECUTABLE TRUE)
      target_sources(autobuild_gui PRIVATE apps/win32_entry.cpp)
    elseif(APPLE)
      set_target_properties(autobuild_gui PROPERTIES MACOSX_BUNDLE TRUE)
      set_target_properties(autobuild_gui PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.autobuild.gui")
    endif()

    # Main GUI executable (without OpenGL)
    add_executable(autobuild_main
      apps/autobuild_gui.cpp
      ${IMGUI_DIR}/imgui.cpp
      ${IMGUI_DIR}/imgui_draw.cpp
      ${IMGUI_DIR}/imgui_tables.cpp
      ${IMGUI_DIR}/imgui_widgets.cpp
      ${IMGUI_DIR}/imgui_demo.cpp
      ${IMGUI_DIR}/imgui_impl_sdl2.cpp
      ${IMGUI_DIR}/imgui_impl_sdlrenderer2.cpp
    )
    target_include_directories(autobuild_main PRIVATE ${IMGUI_DIR} ${FONTS_DIR})
    target_link_libraries(autobuild_main PRIVATE SDL2::SDL2)
    target_compile_definitions(autobuild_main PRIVATE SDL_MAIN_HANDLED)

    # Hide console window for main GUI on all platforms
    if(WIN32)
      set_target_properties(autobuild_main PROPERTIES WIN32_EXECUTABLE TRUE)
      target_sources(autobuild_main PRIVATE apps/win32_entry.cpp)
    elseif(APPLE)
      set_target_properties(autobuild_main PROPERTIES MACOSX_BUNDLE TRUE)
      set_target_properties(autobuild_main PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.autobuild.main")
    endif()

    # Bundle dependencies with macOS app bundles for self-contained distribution
    if(APPLE AND SDL2_FOUND)
      # Find SDL2 library path
      get_target_property(SDL2_LIBRARY SDL2::SDL2 LOCATION)
      if(SDL2_LIBRARY)
        # Copy SDL2 to app bundle Frameworks
        add_custom_command(TARGET autobuild_gui POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Frameworks"
          COMMAND ${CMAKE_COMMAND} -E copy
          "${SDL2_LIBRARY}"
          "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Frameworks/"
          COMMENT "Bundling SDL2 with autobuild_gui app")
        
        # Copy shader files to app bundle Resources
        add_custom_command(TARGET autobuild_gui POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Resources"
          COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/apps/vertex.glsl"
          "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Resources/"
          COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/apps/fragment.glsl"
          "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Resources/"
          COMMENT "Bundling shader files with autobuild_gui app")
        
        add_custom_command(TARGET autobuild_main POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/Frameworks"
          COMMAND ${CMAKE_COMMAND} -E copy
          "${SDL2_LIBRARY}"
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/Frameworks/"
          COMMENT "Bundling SDL2 with autobuild_main app")
        
        # Update library paths to use bundled SDL2
        add_custom_command(TARGET autobuild_gui POST_BUILD
          COMMAND install_name_tool -change
          "${SDL2_LIBRARY}"
          "@executable_path/../Frameworks/libSDL2.dylib"
          "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/MacOS/autobuild_gui"
          COMMENT "Updating SDL2 library path in autobuild_gui")
        
        add_custom_command(TARGET autobuild_main POST_BUILD
          COMMAND install_name_tool -change
          "${SDL2_LIBRARY}"
          "@executable_path/../Frameworks/libSDL2.dylib"
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/MacOS/autobuild_main"
          COMMENT "Updating SDL2 library path in autobuild_main")
      endif()
      
      # Bundle OpenGL frameworks (macOS system frameworks)
      add_custom_command(TARGET autobuild_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "/System/Library/Frameworks/OpenGL.framework"
        "$<TARGET_BUNDLE_DIR:autobuild_gui>/Contents/Frameworks/OpenGL.framework"
        COMMENT "Linking OpenGL framework for autobuild_gui")
    endif()

    # Create default prompts.json file if it doesn't exist
    if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/prompts.json")
      file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/prompts.json" "{
  \"prompt1\": \"**Task:** \\n\\n1.  Read the user request from the \\`prompt\\` file and execute the specified tasks. \\n2.  Use the \\`verify.sh\\` script to test your solution. \\n\\n**Analysis of \\`verify.sh\\`:** \\n\\nUpon completion of the task, provide a concise analysis of the \\`verify.sh\\` script's effectiveness. Your summary should address the following: \\n\\n*   **Sufficiency:** Does the script contain adequate tests to confirm a successful task completion? \\n*   **Over-testing:** Does the script make rigid assumptions about the solution's implementation that might incorrectly fail a valid approach? \\n*   **Scope:** Does the script test for requirements not explicitly stated in \\`prompt\\`? \\n\\n---\\nBelow is the content of prompt.txt for this task. Treat it as the user request:\\n---\",
  \"prompt2\": \"**Hypothetical Scenario:** \\n\\nIf the \\`verify.sh\\` script had not been provided, could you have successfully completed the task as defined in \\`prompt.txt\\`? \\n\\n**Prompt and Verification Analysis:** \\n\\nIdentify any ambiguities or under-specified elements in either the \\`prompt.txt\\` or the \\`verify.sh\\` script that could have led to a failed test.\",
  \"audit_prompt\": \"# Minimal Audit Prompt (for Gemini CLI)\\n\\nContext (read-only):\\n- _context/prompt.txt   = task\\n- _context/verify/      = verifier (entire folder)\\n- _context/Dockerfile   = environment contract (initial state)\\n\\nWorking environment:\\n- You are executing inside the environment created by the Dockerfile.\\n- All files/subfolders in the current working directory (except \\`_context/\\`) are the live environment.\\n- \\`_context/\\` is reference-only and read-only.\\n\\nTask:\\nAnalyze only. Determine if the verifier is valid for the task, if the task is clear enough to verify, and whether the verifier would also accept other valid implementations (within constraints). Do not implement or propose fixes.\\n\\nOutput in EXACTLY this format:\\n<VERIFY_VALID>Yes/No</VERIFY_VALID>\\n<VERIFY_REASON>[1–2 sentences. Explicitly address: behavior vs implementation, over-constraint vs prompt/Dockerfile invariants, environment/path assumptions, functional coverage, hardcoded/irrelevant data, and prompt–verify alignment.]</VERIFY_REASON>\\n<PROMPT_CLEAR>Yes/No</PROMPT_CLEAR>\\n<PROMPT_REASON>[1–2 sentences on whether the task is clear enough to verify and why.]</PROMPT_REASON>\\n<OTHER_VALID_SOLUTIONS_OK>Yes/No</OTHER_VALID_SOLUTIONS_OK>\\n<OTHER_SOLUTIONS_REASON>[1–2 sentences on whether the verifier would pass other valid solutions under the constraints and why.]</OTHER_SOLUTIONS_REASON>\\n\\nConstraints:\\n- Treat Dockerfile-defined paths, names, and platform as environment invariants (valid hardcoding).\\n- Do NOT invent requirements beyond prompt.txt or implied by the Dockerfile.\\n- Do NOT suggest modifying or implementing anything; audit only.\\n- Do NOT modify _context/prompt.txt, _context/verify/*, or _context/Dockerfile.\\n- Keep each reason to 1–2 sentences.\"
}
")
    endif()

    # Copy Font Awesome fonts to build directory for main GUI
    if(EXISTS "${FONTS_DIR}/fa-solid-900.ttf")
      add_custom_command(TARGET autobuild_main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FONTS_DIR}/fa-solid-900.ttf"
        "${CMAKE_CURRENT_BINARY_DIR}/resources/fonts/fa-solid-900.ttf"
        COMMENT "Copying Font Awesome Solid font")
      
      # Also copy to macOS app bundle if building bundle
      if(APPLE)
        add_custom_command(TARGET autobuild_main POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/Resources"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${FONTS_DIR}/fa-solid-900.ttf"
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/Resources/fa-solid-900.ttf"
          COMMENT "Copying Font Awesome Solid font to app bundle")
      endif()
    endif()

    if(EXISTS "${FONTS_DIR}/fa-regular-400.ttf")
      add_custom_command(TARGET autobuild_main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FONTS_DIR}/fa-regular-400.ttf"
        "${CMAKE_CURRENT_BINARY_DIR}/resources/fonts/fa-regular-400.ttf"
        COMMENT "Copying Font Awesome Regular font")
      
      # Also copy to macOS app bundle if building bundle
      if(APPLE)
        add_custom_command(TARGET autobuild_main POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/Resources"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${FONTS_DIR}/fa-regular-400.ttf"
          "$<TARGET_BUNDLE_DIR:autobuild_main>/Contents/Resources/fa-regular-400.ttf"
          COMMENT "Copying Font Awesome Regular font to app bundle")
      endif()
    endif()

    # Install the main GUI application
    install(TARGETS autobuild_main
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      BUNDLE DESTINATION .
    )

    # Copy shader files to build directory for OpenGL animation
    add_custom_command(TARGET autobuild_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/apps/vertex.glsl"
      "${CMAKE_CURRENT_BINARY_DIR}/vertex.glsl"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/apps/fragment.glsl"
      "${CMAKE_CURRENT_BINARY_DIR}/fragment.glsl"
      COMMENT "Copying shader files for OpenGL animation")
    if(OVERRIDE_ABORT_FUNCTION)
      target_compile_definitions(autobuild_gui PRIVATE OVERRIDE_ABORT_FUNCTION)
    endif()

    # Override IM_ASSERT to prevent crashes from ID stack issues
    # This is done via a header file that gets included before ImGui headers
    target_compile_definitions(autobuild_gui PRIVATE IMGUI_ASSERT_OVERRIDE)

    message(STATUS "Building modern ImGui-based GUI: autobuild_gui")
  else()
    message(STATUS "ImGui not found. Run 'bash native/fetch_imgui.sh' to download ImGui for the modern GUI.")
  endif()
else()
  message(STATUS "SDL2 not found; GUI applications will not be built. Install SDL2 (e.g. brew install sdl2 or pacman -S mingw-w64-x86_64-SDL2)")
endif()

enable_testing()

# Cross-platform runtime linkage settings
# - Link pthreads on all platforms that require it
# - Link dl on Linux for dynamic loading where needed
find_package(Threads REQUIRED)
target_link_libraries(autobuild_main PRIVATE Threads::Threads)
target_link_libraries(autobuild_gui PRIVATE Threads::Threads)
if(UNIX AND NOT APPLE)
  target_link_libraries(autobuild_main PRIVATE dl)
  target_link_libraries(autobuild_gui PRIVATE dl)
  # Prefer relative RPATHs for installed binaries on Linux
  set(CMAKE_INSTALL_RPATH "\$ORIGIN")
  set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
endif()



# Install rules for executables


if(TARGET autobuild_gui)
  # Make a proper macOS bundle
  set_target_properties(autobuild_gui PROPERTIES MACOSX_BUNDLE TRUE)
  install(TARGETS autobuild_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
  )
  
  # Set shortcut properties for autobuild_gui
  set_property(
      INSTALL
          ${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:autobuild_gui>
      PROPERTY
          CPACK_START_MENU_SHORTCUTS
              "Autobuild"
  )
  set_property(
      INSTALL
          ${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:autobuild_gui>
      PROPERTY
          CPACK_DESKTOP_SHORTCUTS
              "Autobuild"
  )
endif()

if(WIN32)
  # Hide console window for GUI app on Windows
  set_target_properties(autobuild_gui PROPERTIES WIN32_EXECUTABLE TRUE)
  target_sources(autobuild_gui PRIVATE apps/win32_entry.cpp)
  # Optionally embed a Windows icon if present at resources/app_icon.ico
  set(_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.ico")
  if(EXISTS "${_APP_ICON}")
    set(_APP_RC_GUI "${CMAKE_CURRENT_BINARY_DIR}/autobuild_gui_icon.rc")
    file(WRITE "${_APP_RC_GUI}" "IDI_APPICON ICON \"${_APP_ICON}\"\n")
    target_sources(autobuild_gui PRIVATE "${_APP_RC_GUI}")
    
    # Add version information for better Task Manager display
    target_sources(autobuild_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/version_gui.rc")
    
    set(_APP_RC_MAIN "${CMAKE_CURRENT_BINARY_DIR}/autobuild_main_icon.rc")
    file(WRITE "${_APP_RC_MAIN}" "IDI_APPICON ICON \"${_APP_ICON}\"\n")
    target_sources(autobuild_main PRIVATE "${_APP_RC_MAIN}")
    
    # Add version information for better Task Manager display
    target_sources(autobuild_main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/version.rc")
  endif()
endif()

# macOS: bundle icon if an .icns is provided
if(APPLE)
  set(_MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns")
  if(EXISTS "${_MAC_ICON}")
    set_target_properties(autobuild_gui PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.autobuild.gui"
      MACOSX_BUNDLE_BUNDLE_NAME "Autobuild Animation"
      MACOSX_BUNDLE_BUNDLE_VERSION "2.0.0"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "2.0.0")
    # Put the .icns into the bundle Resources
    set_source_files_properties("${_MAC_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(autobuild_gui PRIVATE "${_MAC_ICON}")
    
    set_target_properties(autobuild_main PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.autobuild.main"
      MACOSX_BUNDLE_BUNDLE_NAME "Autobuild"
      MACOSX_BUNDLE_BUNDLE_VERSION "2.0.0"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "2.0.0")
    target_sources(autobuild_main PRIVATE "${_MAC_ICON}")
  endif()
endif()

# Linux: optionally install icons into hicolor theme if provided
if(UNIX AND NOT APPLE)
  set(_ICON_BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons")
  foreach(_sz IN ITEMS 16 24 32 48 64 128 256 512)
    set(_png "${_ICON_BASE}/${_sz}x${_sz}/autobuild_gui.png")
    if(EXISTS "${_png}")
      install(FILES "${_png}" DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_sz}x${_sz}/apps)
    endif()
  endforeach()
endif()

# Copy dependent DLLs/.dylib/.so into the install tree for each executable
# This makes Windows MSI/macOS DMG/Linux TGZ self-contained for runtime deps like SDL2
set(_FIXUP_SEARCH_DIRS "${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR}")

# If building with vcpkg, add the triplet bin dir so fixup_bundle can find DLLs
if(DEFINED VCPKG_TARGET_TRIPLET)
  set(_VCPKG_BIN "C:/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/bin")
  if(EXISTS "${_VCPKG_BIN}")
    set(_FIXUP_SEARCH_DIRS "${_FIXUP_SEARCH_DIRS};${_VCPKG_BIN}")
  endif()
endif()
install(CODE "
  include(BundleUtilities)
  set(BU_CHMOD_BUNDLE_ITEMS ON)
  set(FIXUP_BUNDLE_SEARCH_DIRS \"${_FIXUP_SEARCH_DIRS}\")
  function(_try_fix targetName)
    set(exe \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/\${targetName}${CMAKE_EXECUTABLE_SUFFIX}\")
    if(EXISTS \"\${exe}\")
      message(STATUS \"Fixing bundle: \${exe}\")
      fixup_bundle(\"\${exe}\" \"\" \"\${FIXUP_BUNDLE_SEARCH_DIRS}\")
    endif()
  endfunction()
  _try_fix(autobuild_gui)
  _try_fix(autobuild_main)
")

# Install MSVC runtime DLLs into bin on Windows
if(MSVC AND CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
  install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Explicitly include SDL2.dll from vcpkg if present
if(WIN32 AND DEFINED _VCPKG_BIN)
  if(EXISTS "${_VCPKG_BIN}/SDL2.dll")
    install(FILES "${_VCPKG_BIN}/SDL2.dll" DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

# Install shader files for OpenGL animation
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/apps/vertex.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/apps/fragment.glsl"
  DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install LICENSE file
install(FILES 
  "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
  DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install FontAwesome fonts
install(DIRECTORY 
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/"
  DESTINATION ${CMAKE_INSTALL_BINDIR}/resources/fonts
  FILES_MATCHING PATTERN "*.ttf")

# Install configuration files
install(FILES 
  "${CMAKE_CURRENT_BINARY_DIR}/autobuild_gui.json"
  "${CMAKE_CURRENT_BINARY_DIR}/imgui.ini"
  "${CMAKE_CURRENT_BINARY_DIR}/prompts.json"
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  OPTIONAL)

# CPack packaging configuration
set(CPACK_PACKAGE_NAME "Autobuild")
set(CPACK_PACKAGE_VENDOR "Autobuild")
set(CPACK_PACKAGE_VERSION "2.0.0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Autobuild")

if(WIN32)
  # Produce an MSI (requires WiX Toolset in PATH)
  set(CPACK_GENERATOR "WIX")
  # Stable GUIDs for upgrades (generate once and keep)
  set(CPACK_WIX_PRODUCT_GUID "7f7c2b77-7d3e-4a8d-9b35-7d8a6c134c5b")
  set(CPACK_WIX_UPGRADE_GUID "9b7a0da2-03f4-4f8a-9d5b-2b01c4c34b32")
  # Set the license file for the installer (WiX requires RTF format)
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.rtf")
  
  # Set installer icon
  set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/installer_icon.ico")
  set(CPACK_WIX_UPGRADE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/installer_icon.ico")
  
  # Enable WiX features for shortcuts
  set(CPACK_WIX_FEATURE_DESKTOP_SHORTCUT "ON")
  set(CPACK_WIX_FEATURE_START_MENU_SHORTCUT "ON")
  
  # Alternative: NSIS packaging (uncomment to use instead of WiX)
  # set(CPACK_GENERATOR "NSIS")
  # set(CPACK_NSIS_DISPLAY_NAME "Autobuild")
  # set(CPACK_NSIS_PACKAGE_NAME "Autobuild")
  # set(CPACK_NSIS_CONTACT "gabriel2077.professional@gmail.com")
  # set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/your-repo/autobuild")
  # set(CPACK_NSIS_MODIFY_PATH ON)
  # 
  # # NSIS shortcuts with parameters (if needed)
  # set(CPACK_NSIS_CREATE_ICONS_EXTRA
  #   "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Autobuild Animation.lnk' '$INSTDIR\\\\autobuild_gui.exe' ''"
  #   "CreateShortCut '$DESKTOP\\\\Autobuild Animation.lnk' '$INSTDIR\\\\autobuild_gui.exe' ''"
  # )
  # set(CPACK_NSIS_DELETE_ICONS_EXTRA
  #   "Delete '$SMPROGRAMS\\\\$START_MENU\\\\Autobuild Animation.lnk'"
  #   "Delete '$DESKTOP\\\\Autobuild Animation.lnk'"
  # )
elseif(APPLE)
  # Create a DMG with the app bundle
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_BUNDLE_NAME "Autobuild")
  # Configure DMG with Applications symlink
  set(CPACK_DMG_VOLUME_NAME "Autobuild")
  set(CPACK_DMG_FORMAT "UDZO")
  set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/resources/dmg_background.png")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/resources/setup_dmg.sh")
else()
  # Linux packages; choose what fits your distro
  set(CPACK_GENERATOR "DEB;RPM;TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gabriel2077.professional@gmail.com")
  
  # Configure DEB package
  set(CPACK_DEBIAN_PACKAGE_SECTION "Development")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libsdl2-2.0-0")
  
  # Configure RPM package
  set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
  set(CPACK_RPM_PACKAGE_LICENSE "MIT")
  set(CPACK_RPM_PACKAGE_REQUIRES "glibc, libstdc++, SDL2")
endif()

include(CPack)

# Linux desktop integration: install launchers that don't open a terminal
if(UNIX AND NOT APPLE)
  install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/autobuild_gui.desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/autobuild_animation.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
endif()

# Install shell orchestrator so the GUI can call it from an installed MSI
# Place it under <prefix>/<bindir>/autobuild/scripts/autobuild.sh
set(_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(EXISTS "${_REPO_ROOT}/autobuild/scripts/autobuild.sh")
  if(APPLE)
    # For macOS app bundles, install to Resources directory
    install(FILES "${_REPO_ROOT}/autobuild/scripts/autobuild.sh"
      DESTINATION ${CMAKE_INSTALL_BINDIR}/../Resources/autobuild/scripts
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  else()
    # For other platforms, install to bin directory
    install(FILES "${_REPO_ROOT}/autobuild/scripts/autobuild.sh"
      DESTINATION ${CMAKE_INSTALL_BINDIR}/autobuild/scripts
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  endif()
endif()
