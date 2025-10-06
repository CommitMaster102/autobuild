// Autobuild GUI - Modern cross-platform interface
// Uses SDL2 + Dear ImGui for a beautiful, functional UI

// Note: Do not override assert macros globally.
// Assertion behavior is controlled at runtime via g_imgui_disable_asserts
// in imgui_assert_override.h and optional CRT hooks when --no-assert is used.

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "fontawesome_icons.h"
#include <SDL.h>

// Global Font Awesome fonts
ImFont* g_font_awesome_solid = nullptr;
ImFont* g_font_awesome_regular = nullptr;
bool g_fonts_loaded = false;


#include <SDL_syswm.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cmath>
#include <map>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <io.h>
#include <direct.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <libproc.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#endif

// IM_ASSERT override is handled in imgui.h when IMGUI_ASSERT_OVERRIDE is
// defined

// Animation system
struct Animation {
    float start_time = 0.0f;
    float duration = 1.0f;
    float current_time = 0.0f;
    bool is_playing = false;
    bool loop = false;
    float start_value = 0.0f;
    float end_value = 1.0f;
    
    float GetProgress() const {
        if (duration <= 0.0f) return 1.0f;
        return std::min(current_time / duration, 1.0f);
    }
    
    float GetValue() const {
        float t = GetProgress();
        // Ease out cubic for smooth animations
        t = 1.0f - std::pow(1.0f - t, 3.0f);
        return start_value + (end_value - start_value) * t;
    }
    
    void Update(float delta_time) {
        if (is_playing) {
            current_time += delta_time;
            if (current_time >= duration) {
                if (loop) {
                    current_time = 0.0f;
                } else {
                    is_playing = false;
                    current_time = duration;
                }
            }
        }
    }
    
    void Start(float dur = 1.0f, bool should_loop = false) {
        duration = dur;
        loop = should_loop;
        current_time = 0.0f;
        is_playing = true;
    }
    
    void Stop() {
        is_playing = false;
        current_time = 0.0f;
    }
    
    void Reset() {
        current_time = 0.0f;
        is_playing = false;
    }
};

// Animation manager
struct AnimationManager {
    std::map<std::string, Animation> animations;
    float delta_time = 0.0f;
    std::chrono::high_resolution_clock::time_point last_frame_time;
    
    void Update() {
        auto now = std::chrono::high_resolution_clock::now();
        if (last_frame_time.time_since_epoch().count() > 0) {
            delta_time = std::chrono::duration<float>(now - last_frame_time).count();
        }
        last_frame_time = now;
        
        for (auto& [name, anim] : animations) {
            anim.Update(delta_time);
        }
    }
    
    Animation& GetAnimation(const std::string& name) {
        return animations[name];
    }
    
    void StartAnimation(const std::string& name, float duration = 1.0f, bool loop = false) {
        animations[name].Start(duration, loop);
    }
    
    void StopAnimation(const std::string& name) {
        if (animations.find(name) != animations.end()) {
            animations[name].Stop();
        }
    }
    
    bool IsAnimationPlaying(const std::string& name) const {
        auto it = animations.find(name);
        return it != animations.end() && it->second.is_playing;
    }
};

// Global animation manager
AnimationManager g_animation_manager;

// Compatibility: projects built without Docking support won't have this flag.
// Define it to 0 so code using it compiles and has no effect.
#ifndef ImGuiWindowFlags_NoDocking
#define ImGuiWindowFlags_NoDocking 0
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h> // For process tree enumeration
#define popen _popen
#define pclose _pclose

// Disable Windows assertion dialogs
#include <crtdbg.h>
// For creating absolute paths on Windows
#include <direct.h>
#include <errno.h>
#include <stdlib.h>
#endif

// Forward declaration
static std::vector<std::string> RunShellLines(const std::string &sh);
// Forward declare AppState for dev logging helper
struct AppState;

// Global runtime switch for ImGui assertion handling
bool g_imgui_disable_asserts = false;

// Helper to append a dev log message
// Define DevLog after AppState is defined

// Helper function to join vector of strings into a single string
static std::string JoinShellOutput(const std::vector<std::string> &lines) {
  if (lines.empty())
    return "";
  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0)
      result += "\n";
    result += lines[i];
  }
  return result;
}

// ---------------------------------
// Custom borderless title bar state (cross-platform)
struct TitleBarState {
  bool enabled = false;                  // set true to use custom ImGui title bar
  float height = 40.0f;                  // height in pixels
  // Dark theme to match main window
  ImVec4 bg_color = ImVec4(0.15f, 0.18f, 0.22f, 1.0f); // background color
  bool dragging = false;                 // internal drag tracking
  ImVec2 drag_start_mouse = ImVec2(0, 0); // mouse position when drag started
  ImVec2 drag_start_window = ImVec2(0, 0); // window position when drag started
};

// Forward declaration for the renderer implemented later in the file
static bool RenderCustomTitleBarSimple(SDL_Window *window, TitleBarState &tb);


// Helper function to check if a Docker image exists
static bool DockerImageExists(const std::string &image_name) {
  std::string cmd =
      "docker images --format '{{.Repository}}:{{.Tag}}' | grep -x \"" +
      image_name + "\"";
  auto result = RunShellLines(cmd);
  return !result.empty();
}

// Helper function to generate a unique image name by adding a timestamp suffix
static std::string GenerateUniqueImageName(const std::string &base_name) {
  std::string unique_name = base_name;

  // If image doesn't exist, return as-is
  if (!DockerImageExists(unique_name)) {
    return unique_name;
  }

  // Generate high-precision timestamp suffix with microseconds for better uniqueness
  auto now = std::chrono::high_resolution_clock::now();
  auto system_now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(system_now);
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()) %
            1000000;

  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
  ss << "_" << std::setfill('0') << std::setw(6) << us.count();

  std::string timestamp = ss.str();

  // Try different suffixes until we find a unique name
  for (int attempt = 1; attempt <= 100; ++attempt) {
    if (attempt == 1) {
      unique_name = base_name + ":" + timestamp;
    } else {
      unique_name = base_name + ":" + timestamp + "_" + std::to_string(attempt);
    }

    if (!DockerImageExists(unique_name)) {
      return unique_name;
    }
  }

  // Fallback: use random number if timestamp approach fails
  srand(static_cast<unsigned int>(time(nullptr)));
  unique_name =
      base_name + ":" + timestamp + "_" + std::to_string(rand() % 10000);

  return unique_name;
}

#ifdef _WIN32
// Utility: UTF-16 conversion (Windows only)
static std::wstring Widen(const std::string &narrow) {
  if (narrow.empty())
    return std::wstring();
  int len = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), (int)narrow.size(),
                                NULL, 0);
  std::wstring wide(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), (int)narrow.size(), &wide[0],
                      len);
  return wide;
}

// Utility: check file existence (Windows only)
static bool FileExistsWin(const std::string &path) {
  DWORD attrs = GetFileAttributesA(path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES) &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}
#endif

// Utility: strip ANSI escape sequences from strings
static std::string stripAnsiCodes(const std::string &str) {
  std::string result;
  result.reserve(str.size());

  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\033' && i + 1 < str.size() && str[i + 1] == '[') {
      // Found ANSI escape sequence, skip until terminator
      size_t j = i + 2;
      while (j < str.size()) {
        char c = str[j];
        // ANSI escape sequence terminators
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '@' ||
            c == '`') {
          i = j; // Skip the entire escape sequence including the terminator
          break;
        }
        ++j;
      }
      if (j >= str.size()) {
        break; // Incomplete escape sequence at end of string
      }
    } else {
      result += str[i];
    }
  }

  return result;
}

// Docker error handling utilities
static bool IsImageInUse(const std::string &image_id) {
  // Check if any containers (running or stopped) are using this image
  std::string cmd = "docker ps -a --filter \"ancestor=" + image_id +
                    "\" --format '{{.ID}}'";
  std::string output = JoinShellOutput(RunShellLines(cmd));
  return !output.empty() && output.find("Error") == std::string::npos;
}

static std::vector<std::string>
GetContainersUsingImage(const std::string &image_id) {
  std::vector<std::string> containers;
  std::string cmd = "docker ps -a --filter \"ancestor=" + image_id +
                    "\" --format '{{.ID}}|{{.Names}}|{{.Status}}'";
  std::string output = JoinShellOutput(RunShellLines(cmd));

  if (output.empty() || output.find("Error") != std::string::npos) {
    return containers;
  }

  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty()) {
      containers.push_back(line);
    }
  }

  return containers;
}

static bool SafeDeleteImage(const std::string &image_id,
                            std::string &error_message) {
  // First check if image is in use
  if (IsImageInUse(image_id)) {
    auto containers = GetContainersUsingImage(image_id);
    error_message = "Cannot delete image " + image_id +
                    " - it is being used by " +
                    std::to_string(containers.size()) + " container(s).\n\n";
    error_message += "Containers using this image:\n";
    for (const auto &container : containers) {
      // Parse the container data (ID|Names|Status)
      std::istringstream ss(container);
      std::string id, name, status;
      std::getline(ss, id, '|');
      std::getline(ss, name, '|');
      std::getline(ss, status, '|');
      
      if (!name.empty()) {
        error_message += "  " + std::string(ICON_FA_CUBE) + " " + name + " (" + id.substr(0, 12) + ") - " + status + "\n";
      } else {
        error_message += "  " + std::string(ICON_FA_CUBE) + " " + id.substr(0, 12) + " - " + status + "\n";
      }
    }
    error_message += "\nPlease stop and remove these containers first.";
    return false;
  }

  // Try to delete the image
  std::string cmd = "docker rmi \"" + image_id + "\" 2>&1";
  std::string output = JoinShellOutput(RunShellLines(cmd));

  if (output.find("Error") != std::string::npos ||
      output.find("conflict") != std::string::npos ||
      output.find("unable to remove") != std::string::npos) {
    error_message = "Failed to delete image " + image_id + ":\n" + output;
    return false;
  }

  return true;
}

// Find bash.exe: PATH, Git for Windows, MSYS2 typical locations
// Cache the result to avoid repeated file system operations
static std::string g_cached_bash_path;
static bool g_bash_path_cached = false;

#ifdef _WIN32
// Windows version
static std::string FindBash() {
  // Return cached result if available
  if (g_bash_path_cached) {
    return g_cached_bash_path;
  }
  
  // Prefer Git for Windows / MSYS2 locations first
  char *pf = getenv("ProgramFiles");
  char *pf86 = getenv("ProgramFiles(x86)");
  const char *candidates[] = {"%PF%/Git/bin/bash.exe",
                              "%PF%/Git/usr/bin/bash.exe",
                              "%PF86%/Git/bin/bash.exe",
                              "%PF86%/Git/usr/bin/bash.exe",
                              "C:/msys64/usr/bin/bash.exe",
                              "C:/Program Files/Git/bin/bash.exe",
                              "C:/Program Files/Git/usr/bin/bash.exe",
                              "C:/Program Files (x86)/Git/bin/bash.exe",
                              "C:/Program Files (x86)/Git/usr/bin/bash.exe"};
  for (auto cand : candidates) {
    std::string p(cand);
    if (pf) {
      size_t pos = p.find("%PF%");
      if (pos != std::string::npos)
        p.replace(pos, 4, pf);
    }
    if (pf86) {
      size_t pos = p.find("%PF86%");
      if (pos != std::string::npos)
        p.replace(pos, 6, pf86);
    }
    for (char &c : p)
      if (c == '\\')
        c = '/';
    if (FileExistsWin(p)) {
      g_cached_bash_path = p;
      g_bash_path_cached = true;
      return p;
    }
  }
  // As a fallback, try PATH but skip the WSL stub in System32
  char buf[MAX_PATH] = {0};
  if (SearchPathA(NULL, "bash.exe", NULL, MAX_PATH, buf, NULL)) {
    std::string found(buf);
    std::string lower = found;
    for (char &c : lower)
      c = (char)tolower(c);
    if (lower.find("windows/system32/bash.exe") == std::string::npos &&
        FileExistsWin(found)) {
      g_cached_bash_path = found;
      g_bash_path_cached = true;
      return found;
    }
  }
  
  // Cache empty result to avoid repeated searches
  g_cached_bash_path = std::string();
  g_bash_path_cached = true;
  return std::string();
}
#else
// macOS/Linux version
static std::string FindBash() {
  // Return cached result if available
  if (g_bash_path_cached) {
    return g_cached_bash_path;
  }
  
  // Common bash locations on macOS/Linux
  const char *candidates[] = {
    "/bin/bash",
    "/usr/bin/bash",
    "/usr/local/bin/bash",
    "/opt/homebrew/bin/bash",
    "/usr/local/opt/bash/bin/bash"
  };
  
  for (auto cand : candidates) {
    if (access(cand, X_OK) == 0) {
      g_cached_bash_path = std::string(cand);
      g_bash_path_cached = true;
      return g_cached_bash_path;
    }
  }
  
  // Try which command as fallback
  FILE* pipe = popen("which bash", "r");
  if (pipe) {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string path(buffer);
      // Remove trailing newline
      if (!path.empty() && path.back() == '\n') {
        path.pop_back();
      }
      if (access(path.c_str(), X_OK) == 0) {
        g_cached_bash_path = path;
        g_bash_path_cached = true;
        pclose(pipe);
        return g_cached_bash_path;
      }
    }
    pclose(pipe);
  }
  
  // Cache empty result to avoid repeated searches
  g_cached_bash_path = std::string();
  g_bash_path_cached = true;
  return std::string();
}
#endif

// Run a command hidden (no console) and capture stdout/stderr into lines
#ifdef _WIN32
static bool RunHiddenCapture(const std::string &command,
                             std::vector<std::string> &out_lines,
                             DWORD &out_exit_code) {
  out_lines.clear();
  out_exit_code = 0;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;
  HANDLE hRead = NULL, hWrite = NULL;
  if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    return false;
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;

  PROCESS_INFORMATION pi{};

  std::wstring fullCmd = L"cmd.exe /C " + Widen(command);
  BOOL ok = CreateProcessW(NULL, &fullCmd[0], NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!ok) {
    CloseHandle(hRead);
    CloseHandle(hWrite);
    return false;
  }
  CloseHandle(hWrite);

  std::string buffer;
  buffer.reserve(4096);
  char chunk[512];
  DWORD bytes = 0;
  for (;;) {
    BOOL r = ReadFile(hRead, chunk, sizeof(chunk), &bytes, NULL);
    if (!r || bytes == 0)
      break;
    buffer.append(chunk, chunk + bytes);
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos)
        out_lines.emplace_back(buffer.substr(pos, nl - pos));
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos > 0)
      buffer.erase(0, pos);
  }
  if (!buffer.empty())
    out_lines.emplace_back(buffer);

  // Wait for process with timeout to prevent GUI freezing
  DWORD wait_result = WaitForSingleObject(pi.hProcess, 5000); // 5 second timeout
  if (wait_result == WAIT_TIMEOUT) {
    // Process is taking too long, terminate it
    TerminateProcess(pi.hProcess, 1);
    out_exit_code = 1;
  } else {
    GetExitCodeProcess(pi.hProcess, &out_exit_code);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(hRead);
  return true;
}
#else
// macOS/Linux version - proper implementation
static bool RunHiddenCapture(const std::string &command,
                             std::vector<std::string> &out_lines,
                             int &out_exit_code) {
  out_lines.clear();
  out_exit_code = 0;

  // Create pipes for stdout/stderr
  int pipe_stdout[2], pipe_stderr[2];
  if (pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
    return false;
  }

  // Fork the process
  pid_t pid = fork();
  if (pid == -1) {
    close(pipe_stdout[0]);
    close(pipe_stdout[1]);
    close(pipe_stderr[0]);
    close(pipe_stderr[1]);
    return false;
  }

  if (pid == 0) {
    // Child process
    close(pipe_stdout[0]); // Close read end
    close(pipe_stderr[0]); // Close read end
    
    // Redirect stdout and stderr to pipes
    dup2(pipe_stdout[1], STDOUT_FILENO);
    dup2(pipe_stderr[1], STDERR_FILENO);
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    // Execute the command using shell
    execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
    exit(1); // If execl fails
  } else {
    // Parent process
    close(pipe_stdout[1]); // Close write end
    close(pipe_stderr[1]); // Close write end

    std::string buffer;
    buffer.reserve(4096);
    char chunk[512];
    int bytes = 0;
    int status;
    
    // Read from both stdout and stderr
    while (true) {
      // Check if process is still running
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == pid) {
        // Process exited
        break;
      }
      
      // Read from stdout
      bytes = read(pipe_stdout[0], chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer.append(chunk, bytes);
      }
      
      // Read from stderr
      bytes = read(pipe_stderr[0], chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer.append(chunk, bytes);
      }
      
      // Process complete lines
      size_t pos = 0;
      size_t nl;
      while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
        if (nl > pos) {
          out_lines.emplace_back(buffer.substr(pos, nl - pos));
        }
        size_t next = nl + 1;
        if (next < buffer.size() &&
            ((buffer[nl] == '\r' && buffer[next] == '\n') ||
             (buffer[nl] == '\n' && buffer[next] == '\r')))
          next++;
        pos = next;
      }
      if (pos > 0)
        buffer.erase(0, pos);
        
      // Small delay to prevent busy waiting
      usleep(10000); // 10ms
    }

    // Read remaining output
    while (true) {
      bytes = read(pipe_stdout[0], chunk, sizeof(chunk));
      if (bytes <= 0) break;
      buffer.append(chunk, bytes);
    }
    
    while (true) {
      bytes = read(pipe_stderr[0], chunk, sizeof(chunk));
      if (bytes <= 0) break;
      buffer.append(chunk, bytes);
    }
    
    // Process any remaining lines
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos) {
        out_lines.emplace_back(buffer.substr(pos, nl - pos));
      }
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos < buffer.size()) {
      out_lines.emplace_back(buffer.substr(pos));
    }

    // Wait for process with timeout
    int timeout_count = 0;
    while (timeout_count < 500) { // 5 second timeout (500 * 10ms)
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == pid) {
        // Process finished
        if (WIFEXITED(status)) {
          out_exit_code = WEXITSTATUS(status);
        } else {
          out_exit_code = 1;
        }
        break;
      }
      usleep(10000); // 10ms
      timeout_count++;
    }
    
    if (timeout_count >= 500) {
      // Timeout - kill the process
      kill(pid, SIGKILL);
      out_exit_code = 1;
    }

    close(pipe_stdout[0]);
    close(pipe_stderr[0]);
    return true;
  }
}
#endif

// Run specific executable with args hidden; avoids cmd.exe quoting pitfalls
#ifdef _WIN32
static bool RunHiddenCaptureExe(const std::string &exe, const std::string &args,
                                std::vector<std::string> &out_lines,
                                DWORD &out_exit_code) {
  out_lines.clear();
  out_exit_code = 0;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;
  HANDLE hRead = NULL, hWrite = NULL;
  if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    return false;
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;

  PROCESS_INFORMATION pi{};

  std::wstring wExe = Widen(exe);
  std::wstring wCmdLine = Widen(std::string("\"") + exe + "\" " + args);
  BOOL ok = CreateProcessW(wExe.c_str(), &wCmdLine[0], NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!ok) {
    CloseHandle(hRead);
    CloseHandle(hWrite);
    return false;
  }
  CloseHandle(hWrite);

  std::string buffer;
  buffer.reserve(4096);
  char chunk[512];
  DWORD bytes = 0;
  for (;;) {
    BOOL r = ReadFile(hRead, chunk, sizeof(chunk), &bytes, NULL);
    if (!r || bytes == 0)
      break;
    buffer.append(chunk, chunk + bytes);
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos)
        out_lines.emplace_back(buffer.substr(pos, nl - pos));
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos > 0)
      buffer.erase(0, pos);
  }
  if (!buffer.empty())
    out_lines.emplace_back(buffer);

  // Wait for process with timeout to prevent GUI freezing
  DWORD wait_result = WaitForSingleObject(pi.hProcess, 5000); // 5 second timeout
  if (wait_result == WAIT_TIMEOUT) {
    // Process is taking too long, terminate it
    TerminateProcess(pi.hProcess, 1);
    out_exit_code = 1;
  } else {
    GetExitCodeProcess(pi.hProcess, &out_exit_code);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(hRead);
  return true;
}
#else
// macOS/Linux version - proper implementation
static bool RunHiddenCaptureExe(const std::string &exe, const std::string &args,
                                std::vector<std::string> &out_lines,
                                int &out_exit_code) {
  out_lines.clear();
  out_exit_code = 0;

  // Create pipes for stdout/stderr
  int pipe_stdout[2], pipe_stderr[2];
  if (pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
    return false;
  }

  // Fork the process
  pid_t pid = fork();
  if (pid == -1) {
    close(pipe_stdout[0]);
    close(pipe_stdout[1]);
    close(pipe_stderr[0]);
    close(pipe_stderr[1]);
    return false;
  }

  if (pid == 0) {
    // Child process
    close(pipe_stdout[0]); // Close read end
    close(pipe_stderr[0]); // Close read end
    
    // Redirect stdout and stderr to pipes
    dup2(pipe_stdout[1], STDOUT_FILENO);
    dup2(pipe_stderr[1], STDERR_FILENO);
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    // Build command array
    std::string full_command = exe + " " + args;
    std::vector<std::string> tokens;
    std::stringstream ss(full_command);
    std::string token;
    while (ss >> token) {
      tokens.push_back(token);
    }
    
    // Convert to char* array
    std::vector<char*> argv;
    for (auto& t : tokens) {
      argv.push_back(const_cast<char*>(t.c_str()));
    }
    argv.push_back(nullptr);

    // Execute the command
    execvp(exe.c_str(), argv.data());
    exit(1); // If execvp fails
  } else {
    // Parent process
    close(pipe_stdout[1]); // Close write end
    close(pipe_stderr[1]); // Close write end

    std::string buffer;
    buffer.reserve(4096);
    char chunk[512];
    int bytes = 0;
    int status;
    
    // Read from both stdout and stderr
    while (true) {
      // Check if process is still running
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == pid) {
        // Process exited
        break;
      }
      
      // Read from stdout
      bytes = read(pipe_stdout[0], chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer.append(chunk, bytes);
      }
      
      // Read from stderr
      bytes = read(pipe_stderr[0], chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer.append(chunk, bytes);
      }
      
      // Process complete lines
      size_t pos = 0;
      size_t nl;
      while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
        if (nl > pos) {
          out_lines.emplace_back(buffer.substr(pos, nl - pos));
        }
        size_t next = nl + 1;
        if (next < buffer.size() &&
            ((buffer[nl] == '\r' && buffer[next] == '\n') ||
             (buffer[nl] == '\n' && buffer[next] == '\r')))
          next++;
        pos = next;
      }
      if (pos > 0)
        buffer.erase(0, pos);
        
      // Small delay to prevent busy waiting
      usleep(10000); // 10ms
    }

    // Read remaining output
    while (true) {
      bytes = read(pipe_stdout[0], chunk, sizeof(chunk));
      if (bytes <= 0) break;
      buffer.append(chunk, bytes);
    }
    
    while (true) {
      bytes = read(pipe_stderr[0], chunk, sizeof(chunk));
      if (bytes <= 0) break;
      buffer.append(chunk, bytes);
    }
    
    // Process any remaining lines
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos) {
        out_lines.emplace_back(buffer.substr(pos, nl - pos));
      }
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos < buffer.size()) {
      out_lines.emplace_back(buffer.substr(pos));
    }

    // Wait for process with timeout
    int timeout_count = 0;
    while (timeout_count < 500) { // 5 second timeout (500 * 10ms)
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == pid) {
        // Process finished
        if (WIFEXITED(status)) {
          out_exit_code = WEXITSTATUS(status);
        } else {
          out_exit_code = 1;
        }
        break;
      }
      usleep(10000); // 10ms
      timeout_count++;
    }
    
    if (timeout_count >= 500) {
      // Timeout - kill the process
      kill(pid, SIGKILL);
      out_exit_code = 1;
    }

    close(pipe_stdout[0]);
    close(pipe_stderr[0]);
    return true;
  }
}
#endif

// Stream variant: emits each line via callback as soon as it's available
#ifdef _WIN32
static bool
RunHiddenStreamExe(const std::string &exe, const std::string &args,
                   const std::function<void(const std::string &)> &onLine,
                   DWORD &out_exit_code) {
  out_exit_code = 0;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;
  HANDLE hRead = NULL, hWrite = NULL;
  if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    return false;
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;

  PROCESS_INFORMATION pi{};

  std::wstring wExe = Widen(exe);
  std::wstring wCmdLine = Widen(std::string("\"") + exe + "\" " + args);
  BOOL ok = CreateProcessW(wExe.c_str(), &wCmdLine[0], NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!ok) {
    CloseHandle(hRead);
    CloseHandle(hWrite);
    return false;
  }
  CloseHandle(hWrite);

  std::string buffer;
  buffer.reserve(4096);
  char chunk[512];
  DWORD bytes = 0;
  for (;;) {
    BOOL r = ReadFile(hRead, chunk, sizeof(chunk), &bytes, NULL);
    if (!r || bytes == 0)
      break;
    buffer.append(chunk, chunk + bytes);
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos)
        onLine(buffer.substr(pos, nl - pos));
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos > 0)
      buffer.erase(0, pos);
  }
  if (!buffer.empty())
    onLine(buffer);

  // Wait for process with timeout to prevent GUI freezing
  DWORD wait_result = WaitForSingleObject(pi.hProcess, 5000); // 5 second timeout
  if (wait_result == WAIT_TIMEOUT) {
    // Process is taking too long, terminate it
    TerminateProcess(pi.hProcess, 1);
    out_exit_code = 1;
  } else {
    GetExitCodeProcess(pi.hProcess, &out_exit_code);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(hRead);
  return true;
}
#else
// macOS/Linux version
static bool
RunHiddenStreamExe(const std::string &exe, const std::string &args,
                   const std::function<void(const std::string &)> &onLine,
                   int &out_exit_code) {
  out_exit_code = 0;
  
  std::string command = exe + " " + args;
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) return false;
  
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    std::string line(buffer);
    if (!line.empty() && line.back() == '\n') {
      line.pop_back();
    }
    onLine(line);
  }
  
  out_exit_code = pclose(pipe);
  return true;
}
#endif

#ifdef _WIN32
// Convert Windows path to MSYS2/Unix path format
std::string ConvertToUnixPath(const std::string &winPath) {
  std::string unixPath = winPath;

  // Replace backslashes with forward slashes
  for (size_t i = 0; i < unixPath.length(); i++) {
    if (unixPath[i] == '\\') {
      unixPath[i] = '/';
    }
  }

  // Convert C: to /c/
  if (unixPath.length() >= 2 && unixPath[1] == ':') {
    char drive = tolower(unixPath[0]);
    unixPath = "/" + std::string(1, drive) + "/" + unixPath.substr(3);
  }

  return unixPath;
}
#endif

// Return directory of the current executable
static std::string GetExecutableDir() {
#ifdef _WIN32
  char path[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (len == 0 || len == MAX_PATH)
    return ".";
  std::string p(path, len);
  size_t pos = p.find_last_of("\\/");
  return (pos != std::string::npos) ? p.substr(0, pos) : std::string(".");
#elif defined(__APPLE__)
  char path[1024];
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) == 0) {
    std::string p(path);
    size_t pos = p.find_last_of('/');
    return (pos != std::string::npos) ? p.substr(0, pos) : std::string(".");
  }
  return ".";
#else
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    std::string p(buf);
    size_t pos = p.find_last_of('/');
    return (pos != std::string::npos) ? p.substr(0, pos) : std::string(".");
  }
  return ".";
#endif
}

// Convert a possibly relative path to an absolute path
static std::string ToAbsolutePath(const std::string &path) {
#ifdef _WIN32
  char full[_MAX_PATH];
  if (_fullpath(full, path.c_str(), _MAX_PATH))
    return std::string(full);
  return path;
#else
  // Use realpath for absolute path resolution
  char* resolved = realpath(path.c_str(), nullptr);
  if (resolved) {
    std::string result(resolved);
    free(resolved);
    return result;
  }
  // If realpath fails (e.g., path doesn't exist yet), return as-is
  return path;
#endif
}


// Compute the default logs directory as an absolute path to project
// autobuild/logs
static std::string ResolveDefaultLogsPath() {
  std::string exe_dir = GetExecutableDir();
  
#ifdef _WIN32
  // Check if we're running from a Program Files installation (MSI)
  if (exe_dir.find("Program Files") != std::string::npos) {
    // For MSI installations, use %APPDATA%\Autobuild\logs
    const char* appdata = getenv("APPDATA");
    if (appdata != nullptr) {
      std::string logs_path = std::string(appdata) + "\\Autobuild\\logs";
      return ToAbsolutePath(logs_path);
    }
    // Fallback to Documents if APPDATA fails
    const char* documents = getenv("USERPROFILE");
    if (documents != nullptr) {
      std::string logs_path = std::string(documents) + "\\Documents\\Autobuild\\logs";
      return ToAbsolutePath(logs_path);
    }
  }
#elif defined(__APPLE__)
  // Check if we're running from an installed app bundle
  if (exe_dir.find(".app/Contents/MacOS") != std::string::npos) {
    // For installed app bundles, use ~/Library/Application Support/Autobuild/logs
    const char* home = getenv("HOME");
    if (home) {
      std::string logs_path = std::string(home) + "/Library/Application Support/Autobuild/logs";
      return ToAbsolutePath(logs_path);
    }
    // Fallback to ~/.autobuild/logs
    const char* home_fallback = getenv("HOME");
    if (home_fallback) {
      std::string logs_path = std::string(home_fallback) + "/.autobuild/logs";
      return ToAbsolutePath(logs_path);
    }
  }
#elif defined(__linux__)
  // Check if we're running from a system installation
  if (exe_dir.find("/usr/bin") != std::string::npos || 
      exe_dir.find("/usr/local/bin") != std::string::npos ||
      exe_dir.find("/opt") != std::string::npos) {
    // For system installations, use ~/.local/share/autobuild/logs
    const char* home = getenv("HOME");
    if (home) {
      std::string logs_path = std::string(home) + "/.local/share/autobuild/logs";
      return ToAbsolutePath(logs_path);
    }
    // Fallback to ~/.autobuild/logs
    const char* home_fallback = getenv("HOME");
    if (home_fallback) {
      std::string logs_path = std::string(home_fallback) + "/.autobuild/logs";
      return ToAbsolutePath(logs_path);
    }
  }
#endif
  
  // Development layout: native/build-gui/autobuild_gui.exe -> repo_root =
  // exe_dir/../..
  std::string candidate = exe_dir + "/../../autobuild/logs";
  return ToAbsolutePath(candidate);
}

enum class DropTarget {
  None,
  TaskDirectory,
  OutputDirectory,
  WorkingDirectory,
  BuildDirectory,
  NewLogPath
};

struct TaskValidation {
  bool has_env_dir = false;
  bool has_dockerfile = false;
  bool has_verify_dir = false;
  bool has_verify_sh = false;
  bool has_prompt = false;
  std::string prompt_location;
  std::vector<std::string> missing_items;
  std::vector<std::string> found_items;
};

// Task instance representing one running audit/build
struct TaskInstance {
  int id;
  std::string name;
  std::string command;
  std::vector<std::string> log_output;
  std::mutex log_mutex;
  std::thread worker_thread;
  std::atomic<bool> is_running{false};
  std::atomic<bool> should_stop{false};
  std::atomic<bool> container_created{
      false};                   // Track if Docker container has been created
#ifdef _WIN32
  HANDLE process_handle = NULL; // Windows process handle for termination
#else
  int process_handle = 0; // Unix process handle for termination
#endif
  std::string log_search_filter;

  TaskInstance(int task_id, const std::string &task_name,
               const std::string &cmd)
      : id(task_id), name(task_name), command(cmd) {}
};

struct AppState {
  std::string task_directory;
  std::string api_key;
  std::string image_tag;
  std::string container_name;
  std::string workdir;
  std::string output_dir;
  std::string build_dir = "native/build";
  std::vector<std::string> log_folder_paths; // Multiple log folder paths
  int selected_log_folder = 0;               // Currently selected log folder
  std::string new_log_path_input; // Input buffer for adding new paths
  int selected_mode = 0;          // 0=feedback, 1=verify, 2=both, 3=audit
  std::vector<std::string>
      log_output; // Legacy single log output (kept for compatibility)
  std::atomic<bool> is_running{false}; // Legacy single running flag
  bool show_logs = false;
  std::string pending_drop_file;
  DropTarget drop_target = DropTarget::None;
  bool is_hovering_drop_zone = false;
  TaskValidation validation;
  std::string log_search_filter; // Search filter for logs
  bool show_api_key = false;     // Toggle to show/hide API key
  bool auto_lowercase_names =
      true; // Automatically convert image/container names to lowercase
  bool should_clear_focus = false; // Clear input focus when drag begins
  bool switch_to_logs_tab = false; // Request to switch to Logs tab
  bool switch_to_manage_tab = false; // Request to switch to Manage tab
  std::mutex log_mutex;            // Protect log_output from concurrent access
  std::thread command_thread;      // Background thread for command execution
  // Docker management state
  struct DockerContainer {
    std::string id;
    std::string name;
    std::string image;
    std::string status;
    std::string created;
    std::string log_path;
  };
  struct DockerImage {
    std::string repo_tag;
    std::string id;
    std::string size;
  };
  std::vector<DockerContainer> containers;
  std::vector<DockerImage> images;
  bool docker_loaded = false;
  bool docker_unavailable = false; // Set to true when Docker is not running/accessible
  // Manage Logs state
  int selected_task_index = -1;
  int selected_run_index = -1;
  bool show_confirm_delete = false;
  std::string pending_delete_path;

  // NEW: Multi-task support
  std::vector<std::shared_ptr<TaskInstance>> tasks;
  std::mutex tasks_mutex;
  int next_task_id = 1;
  int max_concurrent_tasks = 3; // Configurable limit
  int run_multiple_count =
      1; // How many tasks to run when "Run Multiple" is clicked
  bool use_docker_no_cache = true; // Always use --no-cache for Docker builds
  int selected_task_tab = 0;       // Currently selected task tab in logs view

  // Developer diagnostics
  bool dev_mode = false;             // Toggle dev diagnostic UI
  std::vector<std::string> dev_logs; // Recent diagnostic messages
  std::mutex dev_logs_mutex;

  // Additional debugging features
  bool show_debug_console = false; // Show debug console window
  bool show_style_editor = false;  // Show ImGui style editor
  bool show_metrics = false;       // Show ImGui metrics window
  bool show_demo = false;          // Show ImGui demo window
  // Bring-to-front toggles for dev windows (set true when opened)
  bool bring_front_metrics = false;
  bool bring_front_style = false;
  bool bring_front_demo = false;
  
  // Popup flags
  bool show_cannot_close_popup = false;

  // NEW: Individual task type counters
  int feedback_count = 1;
  int verify_count = 1;
  int both_count = 1;
  int audit_count = 1;

  // Docker error handling
  std::string image_delete_error;
};

const char *modes[] = {"Feedback", "Verify", "Both", "Audit"};

// Forward declarations
// Optional override lets callers specify a mode without mutating state
std::string BuildCommand(const AppState &state,
                         const std::string &unique_suffix = "",
                         int selected_mode_override = -1);

#include <dirent.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

bool FileExists(const std::string &path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

bool IsDirectory(const std::string &path) {
  struct stat buffer;
  if (stat(path.c_str(), &buffer) != 0)
    return false;
  return S_ISDIR(buffer.st_mode);
}

bool DirectoryExists(const std::string &path) {
  if (path.empty())
    return false;
  return IsDirectory(path);
}

// Create directory recursively if it doesn't exist
static bool CreateDirectoryRecursive(const std::string &path) {
#ifdef _WIN32
  // Create all intermediate directories
  std::string current_path;
  size_t pos = 0;
  while ((pos = path.find_first_of("\\/", pos)) != std::string::npos) {
    current_path = path.substr(0, pos);
    if (!current_path.empty() && !DirectoryExists(current_path)) {
      if (!CreateDirectoryA(current_path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
          return false;
        }
      }
    }
    pos++;
  }
  // Create the final directory
  if (!DirectoryExists(path)) {
    return CreateDirectoryA(path.c_str(), NULL) != 0;
  }
  return true;
#else
  // Use system mkdir -p
  std::string cmd = "mkdir -p \"" + path + "\"";
  return system(cmd.c_str()) == 0;
#endif
}

// Global debug flag for console output
static bool g_show_debug_console = false;
// Height of custom title bar so content can be offset
static float g_titlebar_height = 0.0f;

// Console output helper for critical messages
static void ConsoleLog(const std::string &msg) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  char time_buffer[32];
  std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", std::localtime(&time_t));
  printf("[%s.%03d] %s\n", time_buffer, (int)ms.count(), msg.c_str());
  fflush(stdout);
}

// Override abort function to prevent program termination (only if enabled via
// CMake)
#ifdef OVERRIDE_ABORT_FUNCTION
extern "C" void abort() {
  printf("=== ABORT OVERRIDE CALLED ===\n");
  ConsoleLog("=== ABORT INTERCEPTED ===");
  ConsoleLog("  This is likely an assertion failure from ImGui");
  ConsoleLog("  Continuing execution...");
  ConsoleLog("=========================");
  // Don't actually abort, just return (this will cause undefined behavior but
  // prevents crash)
  return;
}
#endif

// Custom assertion handler for C runtime
#ifdef _WIN32
static int CustomCrtAssertHandler(int reportType, char *message,
                                  int *returnValue) {
  if (reportType == _CRT_ASSERT) {
    ConsoleLog("=== CRT ASSERTION FAILED ===");
    ConsoleLog("  Message: " + std::string(message));
    ConsoleLog("  This is likely an ImGui ID stack issue");
    ConsoleLog("  Continuing execution...");
    ConsoleLog("=============================");
    *returnValue = 0; // Don't show dialog
    return 1;         // Handled
  }
  return 0; // Use default handler for other types
}
#endif

// Signal handler for assertion failures
static void AssertionSignalHandler(int sig) {
  ConsoleLog("=== SIGNAL CAUGHT ===");
  ConsoleLog("  Signal: " + std::to_string(sig));
  ConsoleLog("  This might be an assertion failure");
  ConsoleLog("  Attempting to continue execution...");
  ConsoleLog("=====================");

  // For SIGABRT, try to prevent termination by ignoring the signal
  if (sig == SIGABRT) {
    // Re-register the signal handler to catch future signals
    signal(SIGABRT, AssertionSignalHandler);
    // Try to continue execution
    return;
  }
}

// Custom abort handler
static void CustomAbortHandler() {
  ConsoleLog("=== ABORT CALLED ===");
  ConsoleLog("  This is likely an assertion failure from ImGui");
  ConsoleLog("  Continuing execution...");
  ConsoleLog("=====================");
  // Don't actually abort, just return
}

// Disable Windows assertion dialogs
static void DisableWindowsAssertDialogs() {
#ifdef _WIN32
  // Set up custom assertion handler
  _CrtSetReportHook(CustomCrtAssertHandler);

  // Disable all CRT assertion dialogs
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);

  // Disable Windows assertion dialogs
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);

  // Disable message boxes
  _set_error_mode(_OUT_TO_STDERR);

  // Set up signal handlers
  signal(SIGABRT, AssertionSignalHandler);
  signal(SIGTERM, AssertionSignalHandler);

  // Override abort function
  std::set_terminate(CustomAbortHandler);
#endif
}

// Custom assertion handler for ImGui
static void CustomAssertHandler(const char *file, int line,
                                const char *function, const char *assertion) {
  // Always log assertion failures, not just in debug mode
  ConsoleLog("=== ASSERTION FAILED ===");
  ConsoleLog("  File: " + std::string(file));
  ConsoleLog("  Line: " + std::to_string(line));
  ConsoleLog("  Function: " + std::string(function ? function : "unknown"));
  ConsoleLog("  Assertion: " + std::string(assertion));
  ConsoleLog("  This indicates an ImGui ID stack mismatch or similar issue");
  ConsoleLog("  Continuing execution...");
  ConsoleLog("========================");
  // Don't call abort() - just log and continue
}

// DevLog helper (defined after AppState)
static void DevLog(AppState &state, const std::string &msg) {
  std::lock_guard<std::mutex> lock(state.dev_logs_mutex);
  state.dev_logs.push_back(msg);
  if (state.dev_logs.size() > 200)
    state.dev_logs.erase(state.dev_logs.begin());

  // Also output to console if debug mode is enabled
  if (g_show_debug_console) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    char time_buffer[32];
    std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", std::localtime(&time_t));
    printf("[%s.%03d] %s\n", time_buffer, (int)ms.count(), msg.c_str());
    fflush(stdout);
  }
}

// Validate ImGui state and log any issues
static void ValidateImGuiState(AppState &state) {
  if (!state.dev_mode)
    return;

  ImGuiContext &g = *GImGui;
  ImGuiWindow *w = g.CurrentWindow;

  // Check for common ID stack issues
  if (w && w->IDStack.Size < 1) {
    DevLog(state,
           "WARNING: IDStack.Size < 1, this may cause assertion failures");
  }

  // Check for stack imbalances
  if (g.ColorStack.Size < 0) {
    DevLog(state,
           "ERROR: ColorStack.Size < 0, PushStyleColor/PopStyleColor mismatch");
  }
  if (g.StyleVarStack.Size < 0) {
    DevLog(state,
           "ERROR: StyleVarStack.Size < 0, PushStyleVar/PopStyleVar mismatch");
  }
  if (g.FontStack.Size < 0) {
    DevLog(state, "ERROR: FontStack.Size < 0, PushFont/PopFont mismatch");
  }

  // Check for window issues
  if (g.Windows.Size == 0) {
    DevLog(state, "WARNING: No windows in context");
  }

  // Additional ID stack debugging
  if (w) {
    DevLog(state,
           "IDStack debug: Size=" + std::to_string(w->IDStack.Size) +
               ", ColorStack=" + std::to_string(g.ColorStack.Size) +
               ", StyleVarStack=" + std::to_string(g.StyleVarStack.Size) +
               ", FontStack=" + std::to_string(g.FontStack.Size));
  }
}

// Forward declaration for helper used by overlay buttons
static void FixImGuiIDStack(AppState &state);

// Render Dev diagnostics overlay (drawn last so it stays on top)
static void RenderDevOverlay(AppState &state, ImGuiIO &io) {
  if (!state.dev_mode || !GImGui || !GImGui->CurrentWindow)
    return;
  try {
    ImGui::SetNextWindowBgAlpha(0.35f);
    // Allow dragging by not resetting pos every frame. Default below
    // header/tabs to avoid blocking them.
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f, 70.0f),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    // Do not steal focus every frame; z-order is handled by draw order
    // Enable collapse (dropdown) and remove close button only
    ImGuiWindowFlags dev_flags = ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoSavedSettings;

    bool dev_begin_ok =
        ImGui::Begin("Dev Diagnostics##overlay", nullptr, dev_flags);
    if (dev_begin_ok) {
      // If restored position would overlap main tabs/header, gently nudge it
      // down
      if (ImGui::IsWindowAppearing()) {
        ImVec2 pos = ImGui::GetWindowPos();
        if (pos.y < 60.0f)
          ImGui::SetWindowPos(ImVec2(pos.x, 70.0f));
      }
    }
    if (dev_begin_ok) {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Debug Overlay");
      ImGui::Separator();

      ImGuiContext &g = *GImGui;
      ImGuiWindow *w = g.CurrentWindow;
      ImGui::Text("IDStack: %d", w->IDStack.Size);
      ImGui::Text("ColorStack: %d", g.ColorStack.Size);
      ImGui::Text("StyleVarStack: %d", g.StyleVarStack.Size);
      ImGui::Text("FontStack: %d", g.FontStack.Size);
      ImGui::Text("Windows: %d", g.Windows.Size);
      ImGui::Text("ActiveID: 0x%08X", g.ActiveId);
      ImGui::Text("HoveredID: 0x%08X", g.HoveredId);
      ImGui::Separator();

      {
        std::lock_guard<std::mutex> lock(state.dev_logs_mutex);
        for (int i = (int)state.dev_logs.size() - 1;
             i >= 0 && i >= (int)state.dev_logs.size() - 10; --i) {
          ImGui::TextWrapped("%s", state.dev_logs[i].c_str());
        }
      }

      ImGui::Separator();
      if (ImGui::Button("Clear Logs")) {
        std::lock_guard<std::mutex> lock(state.dev_logs_mutex);
        state.dev_logs.clear();
      }
      ImGui::SameLine();
      if (ImGui::Button("Force ID Stack Check")) {
        ValidateImGuiState(state);
        FixImGuiIDStack(state);
      }

      ImGui::Separator();
      if (ImGui::Button(state.show_metrics ? "Hide Metrics" : "Show Metrics")) {
        state.show_metrics = !state.show_metrics;
        if (state.show_metrics)
          state.bring_front_metrics = true;
        DevLog(state, state.show_metrics ? "Metrics window opened"
                                         : "Metrics window closed");
      }
      ImGui::SameLine();
      if (ImGui::Button(state.show_style_editor ? "Hide Style Editor"
                                                : "Show Style Editor")) {
        state.show_style_editor = !state.show_style_editor;
        if (state.show_style_editor)
          state.bring_front_style = true;
        DevLog(state, state.show_style_editor ? "Style editor opened"
                                              : "Style editor closed");
      }
      ImGui::SameLine();
      if (ImGui::Button(state.show_demo ? "Hide Demo" : "Show Demo")) {
        state.show_demo = !state.show_demo;
        if (state.show_demo)
          state.bring_front_demo = true;
        DevLog(state,
               state.show_demo ? "Demo window opened" : "Demo window closed");
      }

      ImGui::TextDisabled(
          "(Window is draggable - Use Ctrl+D to toggle dev mode)");
    }
    ImGui::End();

    // No close button: overlay is controlled only by Ctrl+D
  } catch (const std::exception &e) {
    std::string error_msg = "EXCEPTION in dev window: " + std::string(e.what());
    DevLog(state, error_msg);
    if (g_show_debug_console)
      ConsoleLog("ERROR: " + error_msg);
  } catch (...) {
    std::string error_msg = "UNKNOWN EXCEPTION in dev window";
    DevLog(state, error_msg);
    if (g_show_debug_console)
      ConsoleLog("ERROR: " + error_msg);
  }
}

// Fix ID stack issues by ensuring proper state
static void FixImGuiIDStack(AppState &state) {
  if (!state.dev_mode)
    return;
  // Do not mutate ImGui internals. Only log context for debugging.
  ImGuiContext &g = *GImGui;
  ImGuiWindow *w = g.CurrentWindow;
  if (w) {
    if (w->IDStack.Size <= 1) {
      DevLog(state,
             "FIXING: IDStack.Size <= 1 observed (no mutation performed)");
      DevLog(state, "DEBUG: Current window: " +
                        std::string(w->Name ? w->Name : "NULL"));
      DevLog(state, "DEBUG: Window flags: " + std::to_string(w->Flags));
      DevLog(state, "DEBUG: Window ID: " + std::to_string(w->ID));
    }
  }
}

// Clean up dummy IDs from the ID stack (only when safe)
static void CleanupImGuiIDStack(AppState &state) {
  if (!state.dev_mode)
    return;
  // No-op: avoid modifying ImGui internal ID stack to prevent assertions.
  // Retained for compatibility with existing call sites.
  (void)state;
}

// Track ID stack changes to identify the root cause
static int last_id_stack_size = -1;
static void TrackIDStackChanges(AppState &state) {
  if (!state.dev_mode)
    return;

  ImGuiContext &g = *GImGui;
  ImGuiWindow *w = g.CurrentWindow;

  if (w) {
    int current_size = w->IDStack.Size;
    if (last_id_stack_size != -1 && current_size != last_id_stack_size) {
      DevLog(state, "ID STACK CHANGE: " + std::to_string(last_id_stack_size) +
                        " -> " + std::to_string(current_size));
      if (current_size < last_id_stack_size) {
        DevLog(state,
               "WARNING: ID stack decreased - possible PopID without PushID");
      } else {
        DevLog(state, "INFO: ID stack increased - PushID called");
      }
    }
    last_id_stack_size = current_size;
  }
}

// Safe wrapper for ImGui operations that might cause ID stack issues
struct ImGuiStateTracker {
  AppState &state;
  int initial_id_stack_size;
  int initial_color_stack_size;
  int initial_style_var_stack_size;
  int initial_font_stack_size;

  ImGuiStateTracker(AppState &s) : state(s) {
    if (state.dev_mode && GImGui) {
      ImGuiContext &g = *GImGui;
      ImGuiWindow *w = g.CurrentWindow;
      initial_id_stack_size = w ? w->IDStack.Size : 0;
      initial_color_stack_size = g.ColorStack.Size;
      initial_style_var_stack_size = g.StyleVarStack.Size;
      initial_font_stack_size = g.FontStack.Size;
    }
  }

  ~ImGuiStateTracker() {
    if (state.dev_mode && GImGui) {
      ImGuiContext &g = *GImGui;
      ImGuiWindow *w = g.CurrentWindow;

      // Check for stack imbalances
      if (w && w->IDStack.Size != initial_id_stack_size) {
        DevLog(state, "WARNING: IDStack size changed from " +
                          std::to_string(initial_id_stack_size) + " to " +
                          std::to_string(w->IDStack.Size));
      }
      if (g.ColorStack.Size != initial_color_stack_size) {
        DevLog(state, "WARNING: ColorStack size changed from " +
                          std::to_string(initial_color_stack_size) + " to " +
                          std::to_string(g.ColorStack.Size));
      }
      if (g.StyleVarStack.Size != initial_style_var_stack_size) {
        DevLog(state, "WARNING: StyleVarStack size changed from " +
                          std::to_string(initial_style_var_stack_size) +
                          " to " + std::to_string(g.StyleVarStack.Size));
      }
      if (g.FontStack.Size != initial_font_stack_size) {
        DevLog(state, "WARNING: FontStack size changed from " +
                          std::to_string(initial_font_stack_size) + " to " +
                          std::to_string(g.FontStack.Size));
      }
    }
  }
};

// RAII helper to always pair ImGui::Begin/ImGui::End
struct ImGuiWindowScope {
  bool visible;
  ImGuiWindowScope(const char *name, bool *p_open, ImGuiWindowFlags flags)
      : visible(false) {
    visible = ImGui::Begin(name, p_open, flags);
  }
  ~ImGuiWindowScope() {
    // Per ImGui contract, End() must be called even if Begin() returned false
    ImGui::End();
  }
  operator bool() const { return visible; }
};

// RAII for Child windows
struct ImGuiChildScope {
  ImGuiChildScope(const char *str_id, const ImVec2 &size, bool border,
                  ImGuiWindowFlags flags = 0) {
    ImGui::BeginChild(str_id, size, border, flags);
  }
  ~ImGuiChildScope() { ImGui::EndChild(); }
};

// RAII for TabBar
struct ImGuiTabBarScope {
  bool began{false};
  ImGuiTabBarScope(const char *str_id, ImGuiTabBarFlags flags = 0) {
    began = ImGui::BeginTabBar(str_id, flags);
  }
  ~ImGuiTabBarScope() {
    if (began)
      ImGui::EndTabBar();
  }
  operator bool() const { return began; }
};

// RAII for TabItem
struct ImGuiTabItemScope {
  bool began{false};
  ImGuiTabItemScope(const char *label, bool *p_open = nullptr,
                    ImGuiTabItemFlags flags = 0) {
    began = ImGui::BeginTabItem(label, p_open, flags);
  }
  ~ImGuiTabItemScope() {
    if (began)
      ImGui::EndTabItem();
  }
  operator bool() const { return began; }
};

// RAII for Popup modal
struct ImGuiPopupModalScope {
  bool began{false};
  ImGuiPopupModalScope(const char *name, bool *p_open = nullptr,
                       ImGuiWindowFlags flags = 0) {
    began = ImGui::BeginPopupModal(name, p_open, flags);
  }
  ~ImGuiPopupModalScope() {
    if (began)
      ImGui::EndPopup();
  }
  operator bool() const { return began; }
};

// RAII for Disabled scope
struct ImGuiDisabledScope {
  bool active{false};
  explicit ImGuiDisabledScope(bool disabled = true) {
    if (disabled) {
      ImGui::BeginDisabled();
      active = true;
    }
  }
  ~ImGuiDisabledScope() {
    if (active)
      ImGui::EndDisabled();
  }
};

// RAII for style color
struct ImGuiStyleColorScope {
  int count{0};
  ImGuiStyleColorScope(ImGuiCol idx, const ImVec4 &col) {
    ImGui::PushStyleColor(idx, col);
    count = 1;
  }
  ImGuiStyleColorScope() = default;
  ~ImGuiStyleColorScope() {
    if (count > 0)
      ImGui::PopStyleColor(count);
  }
  // Non-copyable
  ImGuiStyleColorScope(const ImGuiStyleColorScope &) = delete;
  ImGuiStyleColorScope &operator=(const ImGuiStyleColorScope &) = delete;
};

// RAII for style var (float / ImVec2)
struct ImGuiStyleVarScope {
  bool pushed{false};
  ImGuiStyleVarScope(ImGuiStyleVar idx, float v) {
    ImGui::PushStyleVar(idx, v);
    pushed = true;
  }
  ImGuiStyleVarScope(ImGuiStyleVar idx, const ImVec2 &v) {
    ImGui::PushStyleVar(idx, v);
    pushed = true;
  }
  ~ImGuiStyleVarScope() {
    if (pushed)
      ImGui::PopStyleVar();
  }
};

// RAII for text wrap
struct ImGuiTextWrapScope {
  bool pushed{false};
  explicit ImGuiTextWrapScope(float wrap_pos_x) {
    ImGui::PushTextWrapPos(wrap_pos_x);
    pushed = true;
  }
  ~ImGuiTextWrapScope() {
    if (pushed)
      ImGui::PopTextWrapPos();
  }
};

// Animation helper functions
bool AnimatedButton(const char* label, const ImVec2& size = ImVec2(0, 0), const char* animation_id = nullptr) {
    if (!animation_id) animation_id = label;
    
    // Get hover animation value
    float hover_scale = 1.0f;
    if (g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_hover")) {
        auto& anim = g_animation_manager.GetAnimation(std::string(animation_id) + "_hover");
        anim.start_value = 1.0f;
        anim.end_value = 1.05f;
        hover_scale = anim.GetValue();
    }
    
    // Apply scaling
    ImVec2 original_pos = ImGui::GetCursorPos();
    ImVec2 scaled_size = ImVec2(size.x * hover_scale, size.y * hover_scale);
    
    // Center the scaled button
    ImVec2 offset = ImVec2((scaled_size.x - size.x) * 0.5f, (scaled_size.y - size.y) * 0.5f);
    ImGui::SetCursorPos(ImVec2(original_pos.x - offset.x, original_pos.y - offset.y));
    
    // Draw the button
    bool clicked = ImGui::Button(label, scaled_size);
    bool hovered = ImGui::IsItemHovered();
    
    // Start hover animation
    if (hovered && !g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_hover")) {
        g_animation_manager.StartAnimation(std::string(animation_id) + "_hover", 0.2f);
    } else if (!hovered && g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_hover")) {
        g_animation_manager.StopAnimation(std::string(animation_id) + "_hover");
    }
    
    // Start click animation
    if (clicked && !g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_click")) {
        g_animation_manager.StartAnimation(std::string(animation_id) + "_click", 0.1f);
    }
    
    return clicked;
}

// Helper function for animated progress bars
void AnimatedProgressBar(float fraction, const ImVec2& size = ImVec2(-1, 0), const char* overlay = nullptr, const char* animation_id = nullptr) {
    if (!animation_id) animation_id = "progress";
    
    // Start pulsing animation for active progress
    if (fraction > 0.0f && fraction < 1.0f) {
        if (!g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_pulse")) {
            g_animation_manager.StartAnimation(std::string(animation_id) + "_pulse", 1.0f, true);
        }
        if (!g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_text")) {
            g_animation_manager.StartAnimation(std::string(animation_id) + "_text", 2.0f, true);
        }
    } else {
        g_animation_manager.StopAnimation(std::string(animation_id) + "_pulse");
        g_animation_manager.StopAnimation(std::string(animation_id) + "_text");
    }
    
    // Get pulse animation value
    float pulse_alpha = 1.0f;
    if (g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_pulse")) {
        auto& anim = g_animation_manager.GetAnimation(std::string(animation_id) + "_pulse");
        anim.start_value = 0.7f;
        anim.end_value = 1.0f;
        pulse_alpha = anim.GetValue();
    }
    
    // Get text animation value for moving dots
    std::string animated_overlay = overlay ? overlay : "";
    if (g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_text") && overlay) {
        auto& text_anim = g_animation_manager.GetAnimation(std::string(animation_id) + "_text");
        text_anim.start_value = 0.0f;
        text_anim.end_value = 1.0f;
        float text_progress = text_anim.GetValue();
        
        // Create moving dots effect
        int dot_count = (int)(text_progress * 4) % 4; // 0-3 dots
        animated_overlay = std::string(overlay);
        for (int i = 0; i < dot_count; i++) {
            animated_overlay += ".";
        }
    }
    
    // Draw custom progress bar with fixed text position
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;
    if (canvas_size.x < 0) canvas_size.x = ImGui::GetContentRegionAvail().x;
    if (canvas_size.y < 0) canvas_size.y = ImGui::GetFrameHeight();
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Draw background (darker frame background)
    ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w = 1.0f; // Ensure full opacity
    draw_list->AddRectFilled(pos, ImVec2(pos.x + canvas_size.x, pos.y + canvas_size.y), 
                            ImGui::ColorConvertFloat4ToU32(bg_color));
    
    // Draw progress fill (brighter color)
    ImVec4 fill_color = ImGui::GetStyle().Colors[ImGuiCol_PlotHistogram];
    
    // Use green color for complete progress (fraction = 1.0f)
    if (fraction >= 1.0f) {
        fill_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green color for complete
    }
    
    fill_color.w = 1.0f; // Ensure full opacity
    fill_color.w *= pulse_alpha; // Apply pulsing effect
    float fill_width = canvas_size.x * fraction;
    
    // Always show some progress bar fill for visibility
    if (fill_width > 0) {
        // Ensure minimum visible width
        if (fill_width < 2.0f) fill_width = 2.0f;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + fill_width, pos.y + canvas_size.y), 
                                ImGui::ColorConvertFloat4ToU32(fill_color));
    } else if (fraction > 0.0f) {
        // Show minimal progress even for very small fractions
        draw_list->AddRectFilled(pos, ImVec2(pos.x + 2.0f, pos.y + canvas_size.y), 
                                ImGui::ColorConvertFloat4ToU32(fill_color));
    }
    
    // Draw border
    ImVec4 border_color = ImGui::GetStyle().Colors[ImGuiCol_Border];
    border_color.w = 1.0f; // Ensure full opacity
    draw_list->AddRect(pos, ImVec2(pos.x + canvas_size.x, pos.y + canvas_size.y), 
                      ImGui::ColorConvertFloat4ToU32(border_color));
    
    // Draw centered text (fixed position)
    if (overlay && strlen(overlay) > 0) {
        ImVec2 text_size = ImGui::CalcTextSize(animated_overlay.c_str());
        ImVec2 text_pos = ImVec2(
            pos.x + (canvas_size.x - text_size.x) * 0.5f,
            pos.y + (canvas_size.y - text_size.y) * 0.5f
        );
        
        // Draw text with shadow for better visibility
        ImVec4 shadow_color = ImVec4(0, 0, 0, 0.8f);
        draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y + 1), 
                          ImGui::ColorConvertFloat4ToU32(shadow_color), 
                          animated_overlay.c_str());
        
        // Use green text color for complete progress
        ImVec4 text_color = (fraction >= 1.0f) ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(1, 1, 1, 1);
        draw_list->AddText(text_pos, ImGui::ColorConvertFloat4ToU32(text_color), 
                          animated_overlay.c_str());
    }
    
    // Advance cursor
    ImGui::Dummy(canvas_size);
}

// Helper function for animated status indicators
void AnimatedStatusIndicator(const char* text, ImVec4 color, bool is_active = false, const char* animation_id = nullptr) {
    if (!animation_id) animation_id = "status";
    
    ImVec4 display_color = color;
    
    if (is_active) {
        // Start pulsing animation for active status
        if (!g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_pulse")) {
            g_animation_manager.StartAnimation(std::string(animation_id) + "_pulse", 1.5f, true);
        }
        
        // Get pulse animation value
        auto& anim = g_animation_manager.GetAnimation(std::string(animation_id) + "_pulse");
        anim.start_value = 0.6f;
        anim.end_value = 1.0f;
        float pulse_alpha = anim.GetValue();
        
        display_color = ImVec4(color.x, color.y, color.z, color.w * pulse_alpha);
    } else {
        g_animation_manager.StopAnimation(std::string(animation_id) + "_pulse");
    }
    
    ImGui::TextColored(display_color, "%s", text);
}

// Helper function for spinning loading indicator
void AnimatedLoadingSpinner(const char* label = "Loading...", float radius = 8.0f, const char* animation_id = nullptr, float speed = 2.0f) {
    if (!animation_id) animation_id = "spinner";
    
    // Start spinning animation with custom speed
    if (!g_animation_manager.IsAnimationPlaying(std::string(animation_id) + "_spin")) {
        g_animation_manager.StartAnimation(std::string(animation_id) + "_spin", speed, true);
    }
    
    // Get rotation value
    auto& anim = g_animation_manager.GetAnimation(std::string(animation_id) + "_spin");
    anim.start_value = 0.0f;
    anim.end_value = 360.0f;
    float rotation = anim.GetValue();
    
    // Draw spinning circle
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 center = ImGui::GetCursorScreenPos();
    center.x += radius;
    center.y += radius;
    
    // Draw spinner segments
    for (int i = 0; i < 8; ++i) {
        float angle = (rotation + i * 45.0f) * 3.14159f / 180.0f;
        float alpha = 1.0f - (i / 8.0f);
        ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, alpha);
        
        ImVec2 start = ImVec2(
            center.x + std::cos(angle) * radius * 0.5f,
            center.y + std::sin(angle) * radius * 0.5f
        );
        ImVec2 end = ImVec2(
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        );
        
        draw_list->AddLine(start, end, ImGui::ColorConvertFloat4ToU32(color), 2.0f);
    }
    
    ImGui::Dummy(ImVec2(radius * 2, radius * 2));
    ImGui::SameLine();
    ImGui::Text("%s", label);
}

// Recursively delete a directory and its contents
static bool RemoveDirectoryRecursive(const std::string &path) {
  if (!DirectoryExists(path))
    return false;
  DIR *dir = opendir(path.c_str());
  if (!dir)
    return false;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (!ent->d_name || ent->d_name[0] == '.')
      continue;
    std::string child = path + "/" + ent->d_name;
    if (IsDirectory(child)) {
      RemoveDirectoryRecursive(child);
    } else {
      remove(child.c_str());
    }
  }
  closedir(dir);
  // Remove the now-empty directory
  int rc = rmdir(path.c_str());
  return rc == 0;
}

// Config file management with JSON format
const char *CONFIG_FILE = "autobuild_gui.json";

// Simple JSON escape function
std::string JsonEscape(const std::string &str) {
  std::string escaped;
  for (char c : str) {
    if (c == '"')
      escaped += "\\\"";
    else if (c == '\\')
      escaped += "\\\\";
    else if (c == '\n')
      escaped += "\\n";
    else if (c == '\r')
      escaped += "\\r";
    else if (c == '\t')
      escaped += "\\t";
    else
      escaped += c;
  }
  return escaped;
}

// Simple JSON unescape function
std::string JsonUnescape(const std::string &str) {
  std::string unescaped;
  bool escaping = false;
  for (char c : str) {
    if (escaping) {
      if (c == 'n')
        unescaped += '\n';
      else if (c == 'r')
        unescaped += '\r';
      else if (c == 't')
        unescaped += '\t';
      else
        unescaped += c;
      escaping = false;
    } else if (c == '\\') {
      escaping = true;
    } else {
      unescaped += c;
    }
  }
  return unescaped;
}

void SaveConfig(const AppState &state) {
  std::ofstream file(CONFIG_FILE);
  if (file.is_open()) {
    file << "{\n";

    // Save log folder paths as JSON array
    file << "  \"log_folder_paths\": [";
    for (size_t i = 0; i < state.log_folder_paths.size(); i++) {
      file << "\"" << JsonEscape(state.log_folder_paths[i]) << "\"";
      if (i < state.log_folder_paths.size() - 1)
        file << ", ";
    }
    file << "],\n";

    file << "  \"selected_log_folder\": " << state.selected_log_folder << ",\n";
    file << "  \"task_directory\": \"" << JsonEscape(state.task_directory)
         << "\",\n";
    file << "  \"build_dir\": \"" << JsonEscape(state.build_dir) << "\",\n";
    file << "  \"api_key\": \"" << JsonEscape(state.api_key) << "\",\n";
    file << "  \"auto_lowercase_names\": "
         << (state.auto_lowercase_names ? "true" : "false") << ",\n";
    file << "  \"max_concurrent_tasks\": " << state.max_concurrent_tasks
         << ",\n";
    file << "  \"use_docker_no_cache\": "
         << (state.use_docker_no_cache ? "true" : "false") << ",\n";
    file << "  \"feedback_count\": " << state.feedback_count << ",\n";
    file << "  \"verify_count\": " << state.verify_count << ",\n";
    file << "  \"both_count\": " << state.both_count << ",\n";
    file << "  \"audit_count\": " << state.audit_count << "\n";
    file << "}\n";
    file.close();
  }
}

void LoadConfig(AppState &state) {
  std::ifstream file(CONFIG_FILE);
  if (file.is_open()) {
    std::string line;
    bool in_array = false;
    while (std::getline(file, line)) {
      // Handle log_folder_paths array
      if (line.find("\"log_folder_paths\"") != std::string::npos) {
        in_array = true;
        state.log_folder_paths.clear();

        // Parse array on same line or next lines
        size_t bracket_start = line.find('[');
        size_t bracket_end = line.find(']');
        if (bracket_start != std::string::npos &&
            bracket_end != std::string::npos) {
          std::string array_content =
              line.substr(bracket_start + 1, bracket_end - bracket_start - 1);
          size_t pos = 0;
          while (pos < array_content.length()) {
            size_t start = array_content.find('"', pos);
            if (start == std::string::npos)
              break;
            size_t end = array_content.find('"', start + 1);
            if (end == std::string::npos)
              break;
            std::string path = array_content.substr(start + 1, end - start - 1);
            state.log_folder_paths.push_back(JsonUnescape(path));
            pos = end + 1;
          }
          in_array = false;
        }
        continue;
      }

      // Simple JSON parsing (key-value pairs)
      size_t colon_pos = line.find(':');
      if (colon_pos != std::string::npos) {
        // Extract key
        size_t key_start = line.find('"');
        size_t key_end = line.find('"', key_start + 1);
        if (key_start == std::string::npos || key_end == std::string::npos)
          continue;
        std::string key = line.substr(key_start + 1, key_end - key_start - 1);

        if (key == "selected_log_folder" || key == "max_concurrent_tasks" ||
            key == "feedback_count" || key == "verify_count" ||
            key == "both_count" || key == "audit_count") {
          // Extract numeric value
          size_t num_start = line.find(':', colon_pos) + 1;
          std::string num_str = line.substr(num_start);
          int value = std::atoi(num_str.c_str());
          if (key == "selected_log_folder") {
            state.selected_log_folder = value;
          } else if (key == "max_concurrent_tasks") {
            state.max_concurrent_tasks = value;
            if (state.max_concurrent_tasks < 1)
              state.max_concurrent_tasks = 1;
            if (state.max_concurrent_tasks > 20)
              state.max_concurrent_tasks = 20;
          } else if (key == "feedback_count") {
            state.feedback_count = value;
            if (state.feedback_count < 1)
              state.feedback_count = 1;
          } else if (key == "verify_count") {
            state.verify_count = value;
            if (state.verify_count < 1)
              state.verify_count = 1;
          } else if (key == "both_count") {
            state.both_count = value;
            if (state.both_count < 1)
              state.both_count = 1;
          } else if (key == "audit_count") {
            state.audit_count = value;
            if (state.audit_count < 1)
              state.audit_count = 1;
          }
        } else if (key == "auto_lowercase_names" ||
                   key == "use_docker_no_cache") {
          // Extract boolean value
          bool bool_value = (line.find("true") != std::string::npos);
          if (key == "auto_lowercase_names") {
            state.auto_lowercase_names = bool_value;
          } else if (key == "use_docker_no_cache") {
            state.use_docker_no_cache = bool_value;
          }
        } else {
          // Extract string value
          size_t val_start = line.find('"', colon_pos);
          size_t val_end = line.rfind('"');
          if (val_start == std::string::npos || val_end == std::string::npos ||
              val_start >= val_end)
            continue;
          std::string value =
              line.substr(val_start + 1, val_end - val_start - 1);
          value = JsonUnescape(value);

          if (key == "task_directory") {
            // Never load task_directory from cache - always start fresh
            // state.task_directory = value;  // Commented out
          } else if (key == "build_dir") {
            state.build_dir = value;
          } else if (key == "api_key") {
            state.api_key = value;
          }
        }
      }
    }
    file.close();

    // If no log paths loaded, add absolute default
    if (state.log_folder_paths.empty()) {
      std::string default_logs_path = ResolveDefaultLogsPath();
      state.log_folder_paths.push_back(default_logs_path);
      // Ensure the default logs directory exists
      CreateDirectoryRecursive(default_logs_path);
    }

    // Ensure selected index is valid
    if (state.selected_log_folder >= (int)state.log_folder_paths.size()) {
      state.selected_log_folder = 0;
    }
  } else {
    // Default log path if no config file
    std::string default_logs_path = ResolveDefaultLogsPath();
    state.log_folder_paths.push_back(default_logs_path);
    // Ensure the default logs directory exists
    CreateDirectoryRecursive(default_logs_path);
  }
}

// Validate Dockerfile name (lowercase, proper conventions)
bool ValidateDockerfileName(const std::string &filename) {
  // Check if filename contains uppercase letters
  for (char c : filename) {
    if (c >= 'A' && c <= 'Z') {
      return false; // Uppercase not allowed
    }
  }

  // Valid Dockerfile names: Dockerfile, dockerfile, Dockerfile.something
  if (filename == "Dockerfile" || filename == "dockerfile") {
    return true;
  }

  // Check for Dockerfile with suffix (Dockerfile.dev, Dockerfile.prod, etc.)
  if (filename.find("Dockerfile.") == 0 || filename.find("dockerfile.") == 0) {
    return true;
  }

  return false;
}

TaskValidation ValidateTaskDirectory(const std::string &task_dir) {
  TaskValidation val;
  val.missing_items.clear();
  val.found_items.clear();

  if (task_dir.empty())
    return val;

  // Check env/ directory
  std::string env_dir = task_dir + "/env";
  if (IsDirectory(env_dir)) {
    val.has_env_dir = true;
    val.found_items.push_back("[OK] env/ directory");

    // Check for Dockerfile in env/ (accept both cases)
    std::string dockerfile_upper = env_dir + "/Dockerfile";
    std::string dockerfile_lower = env_dir + "/dockerfile";

    if (FileExists(dockerfile_upper)) {
      val.has_dockerfile = true;
      val.found_items.push_back("[OK] env/Dockerfile");
    } else if (FileExists(dockerfile_lower)) {
      val.has_dockerfile = true;
      val.found_items.push_back("[OK] env/dockerfile");
    } else {
      val.missing_items.push_back("[X] env/Dockerfile or env/dockerfile");
    }
  } else {
    val.missing_items.push_back("[X] env/ directory");
    val.missing_items.push_back("[X] env/Dockerfile");
  }

  // Check verify/ directory
  std::string verify_dir = task_dir + "/verify";
  if (IsDirectory(verify_dir)) {
    val.has_verify_dir = true;
    val.found_items.push_back("[OK] verify/ directory");

    // Check for verify.sh
    std::string verify_sh = verify_dir + "/verify.sh";
    if (FileExists(verify_sh)) {
      val.has_verify_sh = true;
      val.found_items.push_back("[OK] verify/verify.sh");
    } else {
      val.missing_items.push_back("[X] verify/verify.sh");
    }

    // Check for verification_command (optional)
    std::string verify_cmd = verify_dir + "/verification_command";
    if (FileExists(verify_cmd)) {
      val.found_items.push_back("[OK] verify/verification_command (optional)");
    }

    // List additional files in verify/ (exclude verify.sh and command files)
    DIR *vdir = opendir(verify_dir.c_str());
    if (vdir) {
      struct dirent *entry;
      while ((entry = readdir(vdir)) != NULL) {
        const char *name = entry->d_name;
        if (!name || name[0] == '.')
          continue;
        std::string base(name);
        if (base == "verify.sh" || base == "command" ||
            base == "verification_command")
          continue;
        std::string full_path = verify_dir + "/" + base;
        struct stat st{};
        if (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
          val.found_items.push_back(std::string("[OK] verify/") + base +
                                    " (extra)");
        }
      }
      closedir(vdir);
    }
  } else {
    val.missing_items.push_back("[X] verify/ directory");
    val.missing_items.push_back("[X] verify/verify.sh");
  }

  // Check for prompt (file or directory)
  std::string prompt_file = task_dir + "/prompt";
  std::string prompt_txt = task_dir + "/prompt.txt";
  std::string prompt_dir = task_dir + "/prompt";

  if (FileExists(prompt_file) && !IsDirectory(prompt_file)) {
    val.has_prompt = true;
    val.prompt_location = "prompt (file)";
    val.found_items.push_back("[OK] prompt (file)");
  } else if (FileExists(prompt_txt)) {
    val.has_prompt = true;
    val.prompt_location = "prompt.txt";
    val.found_items.push_back("[OK] prompt.txt");
  } else if (IsDirectory(prompt_dir)) {
    val.has_prompt = true;
    val.prompt_location = "prompt/ (directory)";
    val.found_items.push_back("[OK] prompt/ directory");

    // Check if prompt/ contains at least one file
    DIR *dir = opendir(prompt_dir.c_str());
    bool has_files = false;
    if (dir) {
      struct dirent *entry;
      while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
          has_files = true;
          break;
        }
      }
      closedir(dir);
    }
    if (!has_files) {
      val.missing_items.push_back("[!] prompt/ directory is empty");
    }
  } else {
    val.missing_items.push_back("[X] prompt or prompt.txt or prompt/");
  }

  return val;
}

void SetModernStyle() {
  ImGuiStyle &style = ImGui::GetStyle();

  // Colors - Modern dark theme with accent colors
  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
  colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
  colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
  colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
  colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

  // Style adjustments
  style.FrameRounding = 4.0f;
  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FramePadding = ImVec2(8, 4);
  style.ItemSpacing = ImVec2(8, 8);
  style.ItemInnerSpacing = ImVec2(6, 6);
  style.IndentSpacing = 20.0f;
  style.ScrollbarSize = 14.0f;
  style.ScrollbarRounding = 12.0f;
  style.GrabMinSize = 12.0f;
  style.GrabRounding = 4.0f;
  style.WindowPadding = ImVec2(12, 12);
}

// NEW: Enhanced streaming with process handle capture for termination
#ifdef _WIN32
static bool RunHiddenStreamExeWithHandle(
    const std::string &exe, const std::string &args,
    const std::function<void(const std::string &)> &onLine,
    DWORD &out_exit_code, HANDLE &out_process_handle,
    std::atomic<bool> &should_stop) {
  out_exit_code = 0;
  out_process_handle = NULL;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;
  HANDLE hRead = NULL, hWrite = NULL;
  if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    return false;
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;

  PROCESS_INFORMATION pi{};

  std::wstring wExe = Widen(exe);
  std::wstring wCmdLine = Widen(std::string("\"") + exe + "\" " + args);
  BOOL ok = CreateProcessW(wExe.c_str(), &wCmdLine[0], NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!ok) {
    CloseHandle(hRead);
    CloseHandle(hWrite);
    return false;
  }
  CloseHandle(hWrite);

  out_process_handle = pi.hProcess;

  std::string buffer;
  buffer.reserve(4096);
  char chunk[512];
  DWORD bytes = 0;
  while (!should_stop) {
    DWORD wait_result = WaitForSingleObject(
        pi.hProcess, 10); // Check every 10ms for maximum responsiveness
    if (wait_result == WAIT_OBJECT_0)
      break; // Process exited

    BOOL r = ReadFile(hRead, chunk, sizeof(chunk), &bytes, NULL);
    if (!r || bytes == 0)
      continue;
    buffer.append(chunk, chunk + bytes);
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos) {
        std::string line = buffer.substr(pos, nl - pos);
        // Strip ANSI escape sequences
        line = stripAnsiCodes(line);
        onLine(line);
      }
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos > 0)
      buffer.erase(0, pos);
  }

  if (should_stop) {
    // Terminate the entire process tree, not just the immediate process
    // This ensures Docker and all child processes are stopped
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32W pe32;
      pe32.dwSize = sizeof(pe32);

      if (Process32FirstW(hSnapshot, &pe32)) {
        DWORD parent_pid = GetProcessId(pi.hProcess);
        do {
          if (pe32.th32ParentProcessID == parent_pid) {
            HANDLE hChild =
                OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
            if (hChild) {
              TerminateProcess(hChild, 1);
              CloseHandle(hChild);
            }
          }
        } while (Process32NextW(hSnapshot, &pe32));
      }
      CloseHandle(hSnapshot);
    }

    // Finally terminate the parent process
    TerminateProcess(pi.hProcess, 1);
    onLine("[STOPPED] Task was terminated by user");
  }

  // Read remaining output
  while (true) {
    BOOL r = ReadFile(hRead, chunk, sizeof(chunk), &bytes, NULL);
    if (!r || bytes == 0)
      break;
    buffer.append(chunk, chunk + bytes);
    size_t pos = 0;
    size_t nl;
    while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
      if (nl > pos) {
        std::string line = buffer.substr(pos, nl - pos);
        // Strip ANSI escape sequences
        line = stripAnsiCodes(line);
        onLine(line);
      }
      size_t next = nl + 1;
      if (next < buffer.size() &&
          ((buffer[nl] == '\r' && buffer[next] == '\n') ||
           (buffer[nl] == '\n' && buffer[next] == '\r')))
        next++;
      pos = next;
    }
    if (pos > 0)
      buffer.erase(0, pos);
  }
  if (!buffer.empty())
    onLine(buffer);

  // Wait for process with timeout to prevent GUI freezing
  DWORD wait_result = WaitForSingleObject(pi.hProcess, 5000); // 5 second timeout
  if (wait_result == WAIT_TIMEOUT) {
    // Process is taking too long, terminate it
    TerminateProcess(pi.hProcess, 1);
    out_exit_code = 1;
  } else {
    GetExitCodeProcess(pi.hProcess, &out_exit_code);
  }
  CloseHandle(pi.hThread);
  CloseHandle(hRead);
  return true;
}
#else
// macOS/Linux version
static bool RunHiddenStreamExeWithHandle(
    const std::string &exe, const std::string &args,
    const std::function<void(const std::string &)> &onLine,
    int &out_exit_code, int &out_process_handle,
    std::atomic<bool> &should_stop) {
  out_exit_code = 0;
  out_process_handle = 0;

  if (g_show_debug_console) {
    ConsoleLog("[DEBUG][Mac/Linux] RunHiddenStreamExeWithHandle exe='" + exe + "' args='" + args + "'");
  }

  // Create pipes for stdout/stderr
  int pipe_stdout[2], pipe_stderr[2];
  if (pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
    if (g_show_debug_console) {
      ConsoleLog("[ERROR][Mac/Linux] Failed to create pipes: " + std::string(strerror(errno)));
    }
    return false;
  }

  // Fork the process
  pid_t pid = fork();
  if (pid == -1) {
    if (g_show_debug_console) {
      ConsoleLog("[ERROR][Mac/Linux] fork() failed: " + std::string(strerror(errno)));
    }
    close(pipe_stdout[0]);
    close(pipe_stdout[1]);
    close(pipe_stderr[0]);
    close(pipe_stderr[1]);
    return false;
  }

  if (pid == 0) {
    // Child process
    close(pipe_stdout[0]); // Close read end
    close(pipe_stderr[0]); // Close read end
    
    // Create new process group for proper termination
    setpgid(0, 0);
    
    // Redirect stdout and stderr to pipes
    dup2(pipe_stdout[1], STDOUT_FILENO);
    dup2(pipe_stderr[1], STDERR_FILENO);
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    // Build command array
    std::string full_command = exe + " " + args;
    std::vector<std::string> tokens;
    std::stringstream ss(full_command);
    std::string token;
    while (ss >> token) {
      tokens.push_back(token);
    }
    
    // Convert to char* array
    std::vector<char*> argv;
    for (auto& t : tokens) {
      argv.push_back(const_cast<char*>(t.c_str()));
    }
    argv.push_back(nullptr);

    // Execute the command
    execvp(exe.c_str(), argv.data());
    exit(1); // If execvp fails
  } else {
    // Parent process
    if (g_show_debug_console) {
      ConsoleLog("[DEBUG][Mac/Linux] Forked process PID: " + std::to_string(pid));
    }
    
    close(pipe_stdout[1]); // Close write end
    close(pipe_stderr[1]); // Close write end
    
    out_process_handle = pid;

    // Set pipes to non-blocking
    int flags = fcntl(pipe_stdout[0], F_GETFL, 0);
    fcntl(pipe_stdout[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(pipe_stderr[0], F_GETFL, 0);
    fcntl(pipe_stderr[0], F_SETFL, flags | O_NONBLOCK);

    std::string buffer;
    buffer.reserve(4096);
    char chunk[512];
    int bytes = 0;
    int status;
    
    while (!should_stop) {
      // Check if process is still running
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == pid) {
        // Process exited
        if (g_show_debug_console) {
          if (WIFEXITED(status)) {
            ConsoleLog("[DEBUG][Mac/Linux] Process " + std::to_string(pid) + " exited with code: " + std::to_string(WEXITSTATUS(status)));
          } else if (WIFSIGNALED(status)) {
            ConsoleLog("[DEBUG][Mac/Linux] Process " + std::to_string(pid) + " terminated by signal: " + std::to_string(WTERMSIG(status)));
          }
        }
        break;
      }
      
      // Read from stdout
      bytes = read(pipe_stdout[0], chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer.append(chunk, bytes);
        size_t pos = 0;
        size_t nl;
        while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
          if (nl > pos) {
            std::string line = buffer.substr(pos, nl - pos);
            // Strip ANSI escape sequences
            line = stripAnsiCodes(line);
            onLine(line);
          }
          size_t next = nl + 1;
          if (next < buffer.size() &&
              ((buffer[nl] == '\r' && buffer[next] == '\n') ||
               (buffer[nl] == '\n' && buffer[next] == '\r')))
            next++;
          pos = next;
        }
        if (pos > 0)
          buffer.erase(0, pos);
      }
      
      // Read from stderr
      bytes = read(pipe_stderr[0], chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer.append(chunk, bytes);
        size_t pos = 0;
        size_t nl;
        while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
          if (nl > pos) {
            std::string line = buffer.substr(pos, nl - pos);
            // Strip ANSI escape sequences
            line = stripAnsiCodes(line);
            onLine(line);
          }
          size_t next = nl + 1;
          if (next < buffer.size() &&
              ((buffer[nl] == '\r' && buffer[next] == '\n') ||
               (buffer[nl] == '\n' && buffer[next] == '\r')))
            next++;
          pos = next;
        }
        if (pos > 0)
          buffer.erase(0, pos);
      }
      
      // Small delay to prevent busy waiting
      usleep(10000); // 10ms
    }

    if (should_stop) {
      // Terminate the entire process tree
      // First try to kill the process group
      if (g_show_debug_console) {
        ConsoleLog("[DEBUG][Mac/Linux] Sending SIGTERM to process group " + std::to_string(pid));
      }
      int result = killpg(pid, SIGTERM);
      if (result != 0 && g_show_debug_console) {
        ConsoleLog("[ERROR][Mac/Linux] killpg(SIGTERM) failed: " + std::string(strerror(errno)));
      }
      
      // Wait a bit for graceful termination
      usleep(100000); // 100ms
      
      // Check if still running and force kill
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == 0) {
        // Still running, force kill
        if (g_show_debug_console) {
          ConsoleLog("[DEBUG][Mac/Linux] Process still running, sending SIGKILL to group " + std::to_string(pid));
        }
        killpg(pid, SIGKILL);
      }
      
      onLine("[STOPPED] Task was terminated by user");
    }

    // Read remaining output
    while (true) {
      bytes = read(pipe_stdout[0], chunk, sizeof(chunk));
      if (bytes <= 0) break;
      buffer.append(chunk, bytes);
      size_t pos = 0;
      size_t nl;
      while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
        if (nl > pos) {
          std::string line = buffer.substr(pos, nl - pos);
          // Strip ANSI escape sequences
          line = stripAnsiCodes(line);
          onLine(line);
        }
        size_t next = nl + 1;
        if (next < buffer.size() &&
            ((buffer[nl] == '\r' && buffer[next] == '\n') ||
             (buffer[nl] == '\n' && buffer[next] == '\r')))
          next++;
        pos = next;
      }
      if (pos > 0)
        buffer.erase(0, pos);
    }
    
    // Read remaining stderr
    while (true) {
      bytes = read(pipe_stderr[0], chunk, sizeof(chunk));
      if (bytes <= 0) break;
      buffer.append(chunk, bytes);
      size_t pos = 0;
      size_t nl;
      while ((nl = buffer.find_first_of("\r\n", pos)) != std::string::npos) {
        if (nl > pos) {
          std::string line = buffer.substr(pos, nl - pos);
          // Strip ANSI escape sequences
          line = stripAnsiCodes(line);
          onLine(line);
        }
        size_t next = nl + 1;
        if (next < buffer.size() &&
            ((buffer[nl] == '\r' && buffer[next] == '\n') ||
             (buffer[nl] == '\n' && buffer[next] == '\r')))
          next++;
        pos = next;
      }
      if (pos > 0)
        buffer.erase(0, pos);
    }
    
    if (!buffer.empty())
      onLine(buffer);

    // Wait for process with timeout
    int timeout_count = 0;
    while (timeout_count < 500) { // 5 second timeout (500 * 10ms)
      int wait_result = waitpid(pid, &status, WNOHANG);
      if (wait_result == pid) {
        // Process finished
        if (WIFEXITED(status)) {
          out_exit_code = WEXITSTATUS(status);
          if (g_show_debug_console) {
            ConsoleLog("[DEBUG][Mac/Linux] Process finished with exit code: " + std::to_string(out_exit_code));
          }
        } else {
          out_exit_code = 1;
          if (g_show_debug_console) {
            ConsoleLog("[DEBUG][Mac/Linux] Process terminated abnormally");
          }
        }
        break;
      }
      usleep(10000); // 10ms
      timeout_count++;
    }
    
    if (timeout_count >= 500) {
      // Timeout - kill the process
      if (g_show_debug_console) {
        ConsoleLog("[DEBUG][Mac/Linux] Process timeout, sending SIGKILL to group " + std::to_string(pid));
      }
      killpg(pid, SIGKILL);
      out_exit_code = 1;
    }

    close(pipe_stdout[0]);
    close(pipe_stderr[0]);
    
    if (g_show_debug_console) {
      ConsoleLog("[DEBUG][Mac/Linux] Process cleanup complete for PID " + std::to_string(pid));
    }
    return true;
  }
}
#endif

// NEW: Thread function for TaskInstance
void ExecuteTaskThread(std::shared_ptr<TaskInstance> task) {
#ifdef _WIN32
  std::string cmd = task->command;
  std::string exe;
  std::string args;
  if (!cmd.empty() && cmd[0] == '"') {
    size_t end = cmd.find('"', 1);
    if (end != std::string::npos) {
      exe = cmd.substr(1, end - 1);
      size_t pos = cmd.find_first_not_of(' ', end + 1);
      if (pos != std::string::npos)
        args = cmd.substr(pos);
    } else {
      exe = cmd;
    }
  } else {
    size_t sp = cmd.find(' ');
    if (sp == std::string::npos)
      exe = cmd;
    else {
      exe = cmd.substr(0, sp);
      args = cmd.substr(sp + 1);
    }
  }
  for (char &c : exe)
    if (c == '/')
      c = '\\';

#ifdef _WIN32
  DWORD code = 0;
#else
  int code = 0;
#endif
  auto onLine = [&](const std::string &ln) {
    std::lock_guard<std::mutex> lock(task->log_mutex);
    task->log_output.push_back(ln);
    if (task->log_output.size() > 1000)
      task->log_output.erase(task->log_output.begin());

    // Check if container has been created by looking for specific log messages
    if (!task->container_created.load()) {
      if (ln.find("Starting container:") != std::string::npos ||
          ln.find("Container") != std::string::npos &&
              ln.find("started") != std::string::npos ||
          ln.find("docker run") != std::string::npos &&
              ln.find("--name") != std::string::npos) {
        task->container_created.store(true);
      }
    }
  };
  if (g_show_debug_console) {
    ConsoleLog(std::string("[DEBUG] ExecuteTaskThread original cmd: ") + cmd);
    ConsoleLog(std::string("[DEBUG] parsed exe='") + exe + "' args='" + args + "'");
  }
  bool ok = RunHiddenStreamExeWithHandle(
      exe, args, onLine, code, task->process_handle, task->should_stop);
  if (!ok) {
    // Fallback: try via cmd.exe /C <original cmd>
    std::string fb_exe = "cmd.exe";
    std::string fb_args = std::string("/C ") + cmd;
    if (g_show_debug_console) {
      ConsoleLog("[WARN] Direct exec failed, trying fallback via cmd.exe");
      ConsoleLog(std::string("[DEBUG] fallback exe='") + fb_exe + "' args='" + fb_args + "'");
    }
    ok = RunHiddenStreamExeWithHandle(
        fb_exe, fb_args, onLine, code, task->process_handle, task->should_stop);
  }
  if (!ok) {
    std::lock_guard<std::mutex> lock(task->log_mutex);
    task->log_output.push_back("[ERROR] Failed to execute command");
    task->is_running = false;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(task->log_mutex);
    if (task->should_stop) {
      task->log_output.push_back("[STOPPED] Task terminated");
    } else if (code == 0) {
      task->log_output.push_back("[SUCCESS] Command completed successfully");
    } else {
      task->log_output.push_back("[ERROR] Command failed with exit code: " +
                                 std::to_string(code));
    }
  }
#else
  if (g_show_debug_console) {
    ConsoleLog("[DEBUG][Mac/Linux] ExecuteTaskThread command: " + task->command);
  }
  FILE *pipe = popen(task->command.c_str(), "r");
  if (!pipe) {
    if (g_show_debug_console) {
      ConsoleLog("[ERROR][Mac/Linux] popen failed: " + std::string(strerror(errno)));
    }
    std::lock_guard<std::mutex> lock(task->log_mutex);
    task->log_output.push_back("[ERROR] Failed to execute command");
    task->is_running = false;
    return;
  }
  if (g_show_debug_console) {
    ConsoleLog("[DEBUG][Mac/Linux] Task started successfully");
  }
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) && !task->should_stop) {
    std::lock_guard<std::mutex> lock(task->log_mutex);
    task->log_output.push_back(std::string(buffer));
    if (task->log_output.size() > 1000)
      task->log_output.erase(task->log_output.begin());
  }
  int ret = pclose(pipe);
  {
    std::lock_guard<std::mutex> lock(task->log_mutex);
    if (task->should_stop) {
      if (g_show_debug_console) {
        ConsoleLog("[DEBUG][Mac/Linux] Task stopped by user");
      }
      task->log_output.push_back("[STOPPED] Task terminated");
    } else if (ret == 0) {
      if (g_show_debug_console) {
        ConsoleLog("[DEBUG][Mac/Linux] Task completed successfully");
      }
      task->log_output.push_back("[SUCCESS] Command completed successfully");
    } else {
      if (g_show_debug_console) {
        ConsoleLog("[ERROR][Mac/Linux] Task failed with exit code: " + std::to_string(ret));
      }
      task->log_output.push_back("[ERROR] Command failed with exit code: " +
                                 std::to_string(ret));
    }
  }
#endif
  task->is_running = false;
#ifdef _WIN32
  if (task->process_handle) {
    CloseHandle(task->process_handle);
    task->process_handle = NULL;
  }
#else
  // On Unix, process_handle is a PID (int), no cleanup needed
  // The process is already terminated by pclose() in the Unix branch above
  if (g_show_debug_console && task->process_handle > 0) {
    ConsoleLog("[DEBUG][Mac/Linux] Cleaning up process handle for PID: " + std::to_string(task->process_handle));
  }
  task->process_handle = 0;
#endif
}

// Thread function to execute command asynchronously (LEGACY - kept for
// compatibility)
void ExecuteCommandThread(const std::string cmd, AppState *state) {
#ifdef _WIN32
  // Prefer launching the executable directly to avoid cmd.exe quoting issues
  std::string exe;
  std::string args;
  if (!cmd.empty() && cmd[0] == '"') {
    size_t end = cmd.find('"', 1);
    if (end != std::string::npos) {
      exe = cmd.substr(1, end - 1);
      size_t pos = cmd.find_first_not_of(' ', end + 1);
      if (pos != std::string::npos)
        args = cmd.substr(pos);
    } else {
      exe = cmd; // fallback
    }
  } else {
    size_t sp = cmd.find(' ');
    if (sp == std::string::npos)
      exe = cmd;
    else {
      exe = cmd.substr(0, sp);
      args = cmd.substr(sp + 1);
    }
  }
  for (char &c : exe)
    if (c == '/')
      c = '\\';

#ifdef _WIN32
  DWORD code = 0;
#else
  int code = 0;
#endif
  auto onLine = [&](const std::string &ln) {
    std::lock_guard<std::mutex> lock(state->log_mutex);
    state->log_output.push_back(ln);
    if (state->log_output.size() > 1000)
      state->log_output.erase(state->log_output.begin());
  };
  if (g_show_debug_console) {
    ConsoleLog(std::string("[DEBUG] ExecuteCommandThread original cmd: ") + cmd);
    ConsoleLog(std::string("[DEBUG] parsed exe='") + exe + "' args='" + args + "'");
  }
  bool ok = RunHiddenStreamExe(exe, args, onLine, code);
  if (!ok) {
    // Fallback through cmd.exe for shell built-ins
    std::string fb_exe = "cmd.exe";
    std::string fb_args = std::string("/C ") + cmd;
    if (g_show_debug_console) {
      ConsoleLog("[WARN] Direct exec failed, trying fallback via cmd.exe");
    }
    ok = RunHiddenStreamExe(fb_exe, fb_args, onLine, code);
  }
  if (!ok) {
    std::lock_guard<std::mutex> lock(state->log_mutex);
    state->log_output.push_back("[ERROR] Failed to execute command");
    state->is_running = false;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(state->log_mutex);
    if (code == 0)
      state->log_output.push_back("[SUCCESS] Command completed successfully");
    else
      state->log_output.push_back("[ERROR] Command failed with exit code: " +
                                  std::to_string(code));
  }
#else
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    std::lock_guard<std::mutex> lock(state->log_mutex);
    state->log_output.push_back("[ERROR] Failed to execute command");
    state->is_running = false;
    return;
  }
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    std::lock_guard<std::mutex> lock(state->log_mutex);
    state->log_output.push_back(std::string(buffer));
    if (state->log_output.size() > 1000)
      state->log_output.erase(state->log_output.begin());
  }
  int ret = pclose(pipe);
  {
    std::lock_guard<std::mutex> lock(state->log_mutex);
    if (ret == 0)
      state->log_output.push_back("[SUCCESS] Command completed successfully");
    else
      state->log_output.push_back("[ERROR] Command failed with exit code: " +
                                  std::to_string(ret));
  }
#endif
  state->is_running = false;
}

void ExecuteCommand(const std::string &cmd, AppState &state) {
  // If a command is already running, wait for it to finish
  if (state.command_thread.joinable()) {
    state.command_thread.join();
  }

  // Clear logs and set running flag
  {
    std::lock_guard<std::mutex> lock(state.log_mutex);
    state.log_output.clear();
    state.log_output.push_back("[INFO] Executing: " + cmd);
  }
  state.is_running = true;
  state.show_logs = true;

  // Launch command in background thread
  state.command_thread = std::thread(ExecuteCommandThread, cmd, &state);
}

// NEW: Task management functions
void StartTask(AppState &state, const std::string &task_name,
               const std::string &cmd) {
  std::lock_guard<std::mutex> lock(state.tasks_mutex);

  // Check if we've reached the concurrent task limit
  int running_count = 0;
  for (const auto &task : state.tasks) {
    if (task->is_running)
      running_count++;
  }

  if (running_count >= state.max_concurrent_tasks) {
    // Can't start more tasks - add to queue or reject
    if (g_show_debug_console) {
      ConsoleLog("[WARN] StartTask: at limit, cannot start new task");
    }
    return;
  }

  // Create new task
  int task_id = state.next_task_id++;
  auto task = std::make_shared<TaskInstance>(task_id, task_name, cmd);
  task->log_output.push_back("[INFO] Task started: " + task_name);
  task->log_output.push_back("[INFO] Command: " + cmd);
  if (g_show_debug_console) {
    ConsoleLog("[INFO] StartTask: " + task_name);
    ConsoleLog("[INFO] Cmd: " + cmd);
  }
  task->is_running = true;
  task->container_created = false; // Reset container creation flag

  // Add to tasks list
  state.tasks.push_back(task);

  // Launch worker thread
  task->worker_thread = std::thread(ExecuteTaskThread, task);
  task->worker_thread.detach(); // Detach so we don't need to join manually

  // Switch to logs tab
  state.switch_to_logs_tab = true;
}

// NEW: Start multiple tasks of the same type
// Start tasks immediately but execute them asynchronously
void StartMultipleTasks(AppState &state, const std::string &task_type,
                        int count) {
  // Determine mode for this task type without mutating UI state
  int mode = 0;
  if (task_type == "Feedback")
    mode = 0;
  else if (task_type == "Verify")
    mode = 1;
  else if (task_type == "Both")
    mode = 2;
  else if (task_type == "Audit")
    mode = 3;

  std::string base_name = task_type;
  if (!state.task_directory.empty()) {
    size_t last_slash = state.task_directory.find_last_of("/\\");
    std::string basename = (last_slash != std::string::npos)
                               ? state.task_directory.substr(last_slash + 1)
                               : state.task_directory;
    base_name = basename + " - " + task_type;
  }

  if (g_show_debug_console) {
    ConsoleLog("[INFO] Preparing to start " + std::to_string(count) +
               " task(s) of type " + task_type +
               " (mode=" + std::to_string(mode) + ")");
  }
  
  // Switch to logs tab immediately to show user that tasks are starting
  state.switch_to_logs_tab = true;
  
  // Create all tasks in a background thread to avoid blocking the UI
  // Use sequential creation to avoid race conditions in Docker image naming
  std::thread([&state, task_type, count, mode, base_name]() {
    for (int i = 0; i < count; i++) {
      std::string task_name = base_name;
      if (count > 1) {
        task_name += " #" + std::to_string(i + 1);
      }

      // Generate unique suffix for this task to avoid Docker image conflicts
      // Use timestamp + task number to ensure uniqueness even when created simultaneously
      auto now = std::chrono::system_clock::now();
      auto time_t = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) %
                1000;
      
      std::stringstream ss;
      ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
      ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
      ss << "_" << std::to_string(i + 1);
      
      std::string unique_suffix = task_type + "_task" + ss.str();
      std::transform(unique_suffix.begin(), unique_suffix.end(),
                     unique_suffix.begin(), ::tolower);
      
      // Build command using override mode to avoid touching UI state
      std::string cmd = BuildCommand(state, unique_suffix, mode);
      if (g_show_debug_console) {
        ConsoleLog("[INFO] Built command for [" + task_name + "]: " + cmd);
      }

      StartTask(state, task_name, cmd);
      
      // Small delay between task creation to ensure proper sequencing
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }).detach();
}


void StopAllTasks(AppState &state) {
  std::lock_guard<std::mutex> lock(state.tasks_mutex);

  // Immediately stop all tasks first
  for (auto &task : state.tasks) {
    if (task->is_running) {
      task->should_stop = true;
      // Terminate process immediately if we have a handle
#ifdef _WIN32
      if (task->process_handle) {
        TerminateProcess(task->process_handle, 1);
      }
#else
      // On Unix, send SIGTERM to the process
      if (task->process_handle > 0) {
        if (g_show_debug_console) {
          ConsoleLog("[DEBUG][Mac/Linux] Sending SIGTERM to task PID: " + std::to_string(task->process_handle));
        }
        int result = kill(task->process_handle, SIGTERM);
        if (result != 0 && g_show_debug_console) {
          ConsoleLog("[ERROR][Mac/Linux] kill() failed: " + std::string(strerror(errno)));
        }
      }
#endif
    }
  }

  // Start Docker cleanup in a separate thread (non-blocking)
  std::thread([&state]() {
    // Only stop containers that match our task naming pattern
    std::string kill_autobuild_cmd =
        "docker kill $(docker ps -q --filter \"name=autobuild-*\") 2>/dev/null "
        "|| true";

#ifdef _WIN32
    std::string bash = FindBash();
    if (!bash.empty()) {
      // Skip graceful stop, go straight to kill for immediate termination
      std::string args = std::string("-lc \"export PATH=/c/Program\\ "
                                     "Files/Docker/Docker/resources/bin:/"
                                     "mingw64/bin:/usr/bin:$PATH && ") +
                         kill_autobuild_cmd + "\"";
      std::vector<std::string> output;
      DWORD exit_code;
      RunHiddenCaptureExe(bash, args, output, exit_code);
    }
#else
    // Skip graceful stop, go straight to kill for immediate termination
    std::vector<std::string> output;
    int exit_code;
    RunHiddenCaptureExe("bash",
                        std::string("-c \"") + kill_autobuild_cmd + "\"",
                        output, exit_code);
#endif
  }).detach(); // Detach so it runs independently
}

void RemoveTask(AppState &state, int task_id) {
  std::lock_guard<std::mutex> lock(state.tasks_mutex);
  for (auto it = state.tasks.begin(); it != state.tasks.end(); ++it) {
    if ((*it)->id == task_id) {
      // Store the task pointer before any operations that might invalidate the iterator
      auto task_ptr = *it;
      
      // If task is running, stop it first using the same logic as StopAllTasks
      if (task_ptr->is_running) {
        task_ptr->should_stop = true;
        task_ptr->is_running = false;
        
        // Terminate process immediately if we have a handle
#ifdef _WIN32
        if (task_ptr->process_handle) {
          TerminateProcess(task_ptr->process_handle, 1);
          CloseHandle(task_ptr->process_handle);
          task_ptr->process_handle = NULL;
        }
#else
        // On Unix, send SIGTERM to the process
        if (task_ptr->process_handle > 0) {
          if (g_show_debug_console) {
            ConsoleLog("[DEBUG][Mac/Linux] Deleting task, sending SIGTERM to PID: " + std::to_string(task_ptr->process_handle));
          }
          int result = kill(task_ptr->process_handle, SIGTERM);
          if (result != 0 && g_show_debug_console) {
            ConsoleLog("[ERROR][Mac/Linux] kill() failed: " + std::string(strerror(errno)));
          }
          task_ptr->process_handle = 0;
        }
#endif
        
        // Extract the data we need before creating the thread
        std::string task_name = task_ptr->name;
        size_t dash_pos = task_name.find(" - ");
        std::string basename;
        if (dash_pos != std::string::npos) {
          basename = task_name.substr(0, dash_pos);
        }

        // Start Docker cleanup in a separate thread (non-blocking)
        std::thread([basename]() {
          // Use the extracted basename directly - no need to access the task object
          // Try multiple container name patterns to ensure we catch the container
          std::vector<std::string> kill_commands = {
              "docker kill $(docker ps -q --filter \"name=autobuild-" + basename +
                  "_from_task*\") 2>/dev/null || true",
              "docker kill $(docker ps -q --filter \"name=" + basename +
                  "_from_task*\") 2>/dev/null || true",
              "docker kill $(docker ps -q --filter \"name=autobuild-" + basename +
                  "*\") 2>/dev/null || true",
              "docker kill $(docker ps -q --filter \"name=" + basename +
                  "*\") 2>/dev/null || true"};

#ifdef _WIN32
          std::string bash = FindBash();
          if (!bash.empty()) {
            for (const auto &kill_cmd : kill_commands) {
              std::string args = std::string("-lc \"export PATH=/c/Program\\ "
                                             "Files/Docker/Docker/resources/bin:/"
                                             "mingw64/bin:/usr/bin:$PATH && ") +
                                 kill_cmd + "\"";
              std::vector<std::string> output;
              DWORD exit_code;
              RunHiddenCaptureExe(bash, args, output, exit_code);
            }
          }
#else
          for (const auto &kill_cmd : kill_commands) {
            std::vector<std::string> output;
            int exit_code;
            RunHiddenCaptureExe("bash", std::string("-c \"") + kill_cmd + "\"",
                                output, exit_code);
          }
#endif
        }).detach();
      }

      // Wait for thread to finish if it's joinable
      if (task_ptr->worker_thread.joinable()) {
        task_ptr->worker_thread.join();
      }

      // Now safely erase the task from the vector
      state.tasks.erase(it);
      break;
    }
  }
}

int GetRunningTaskCount(AppState &state) {
  std::lock_guard<std::mutex> lock(state.tasks_mutex);
  int count = 0;
  for (const auto &task : state.tasks) {
    if (task->is_running)
      count++;
  }
  return count;
}

// Helpers for Manage tab
static std::vector<std::string> RunShellLines(const std::string &sh) {
#ifdef _WIN32
  std::string bash = FindBash();
  std::vector<std::string> lines;
#ifdef _WIN32
  DWORD code = 0;
#else
  int code = 0;
#endif
  if (bash.empty()) {
    lines.push_back(
        "[ERROR] Bash not found. Install Git for Windows or MSYS2.");
    return lines;
  }
  std::string args =
      std::string(
          "-lc \"export PATH=/c/Program\\ "
          "Files/Docker/Docker/resources/bin:/mingw64/bin:/usr/bin:$PATH && ") +
      sh + "\"";
  RunHiddenCaptureExe(bash, args, lines, code);
  return lines;
#else
  std::string cmd = sh;
  std::vector<std::string> lines;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return lines;
  char buffer[512];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    std::string line(buffer);
    if (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();
    if (!line.empty() && (line.back() == '\r'))
      line.pop_back();
    lines.push_back(line);
  }
  pclose(pipe);
  return lines;
#endif
}

static std::string ExtractTimestamp(const std::string &container_name) {
  size_t p = container_name.find_last_of('-');
  if (p == std::string::npos || p + 1 >= container_name.size())
    return "";
  std::string t = container_name.substr(p + 1);
  for (char c : t)
    if (c < '0' || c > '9')
      return "";
  return t;
}

static bool FindDirByName(const std::string &root, const std::string &needle,
                          std::string &out, int depth = 3) {
  if (depth < 0)
    return false;
  DIR *d = opendir(root.c_str());
  if (!d)
    return false;
  struct dirent *e;
  bool ok = false;
  struct stat st{};
  while (!ok && (e = readdir(d)) != NULL) {
    if (!e->d_name || e->d_name[0] == '.')
      continue;
    std::string name(e->d_name);
    std::string p = root + "/" + name;
    if (stat(p.c_str(), &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode)) {
      if (name == needle) {
        out = p;
        ok = true;
        break;
      }
      if (FindDirByName(p, needle, out, depth - 1)) {
        ok = true;
        break;
      }
    }
  }
  closedir(d);
  return ok;
}

static std::string FindLatestModePath(const std::string &root,
                                      const std::string &mode) {
  time_t best_time = 0;
  std::string best_path;
  DIR *root_dir = opendir(root.c_str());
  if (!root_dir)
    return "";
  struct dirent *task_ent;
  struct stat st{};
  while ((task_ent = readdir(root_dir)) != NULL) {
    if (!task_ent->d_name || task_ent->d_name[0] == '.')
      continue;
    std::string task_dir = root + "/" + task_ent->d_name;
    if (!DirectoryExists(task_dir))
      continue;
    DIR *ts_dir = opendir(task_dir.c_str());
    if (!ts_dir)
      continue;
    struct dirent *ts_ent;
    while ((ts_ent = readdir(ts_dir)) != NULL) {
      if (!ts_ent->d_name || ts_ent->d_name[0] == '.')
        continue;
      std::string ts_path = task_dir + "/" + ts_ent->d_name;
      if (!DirectoryExists(ts_path))
        continue;
      std::string candidate = ts_path + "/" + mode;
      if (DirectoryExists(candidate)) {
        if (stat(ts_path.c_str(), &st) == 0) {
          if (st.st_mtime > best_time) {
            best_time = st.st_mtime;
            best_path = candidate;
          }
        }
      }
    }
    closedir(ts_dir);
  }
  closedir(root_dir);
  return best_path;
}

static std::string GuessLogPathForContainer(const AppState &state,
                                            const std::string &name) {
  // Resolve a log root candidate by walking up a few directories if needed
  auto resolve_root = [](const std::string &root) -> std::string {
    if (DirectoryExists(root))
      return root;
    std::string prefix = "";
    for (int i = 0; i < 4; i++) {
      if (DirectoryExists(prefix + root))
        return prefix + root;
      prefix += "../";
    }
    return root; // fallback
  };
  // Prefer mode from container name
  std::string mode;
  if (name.find("-feedback-") != std::string::npos)
    mode = "feedback";
  else if (name.find("-verify-") != std::string::npos)
    mode = "verify";

  for (const auto &root : state.log_folder_paths) {
    std::string r = resolve_root(root);
    if (!mode.empty()) {
      std::string p = FindLatestModePath(r, mode);
      if (!p.empty())
        return p;
    } else {
      std::string p1 = FindLatestModePath(r, "feedback");
      std::string p2 = FindLatestModePath(r, "verify");
      if (!p1.empty())
        return p1;
      if (!p2.empty())
        return p2;
    }
  }
  return "";
}

static void RefreshDockerState(AppState &state) {
  state.containers.clear();
  state.images.clear();
  state.docker_unavailable = false;
  
  // Check if Docker is available by running a simple ping command
  auto ping_result = RunShellLines("docker info 2>&1");
  bool docker_available = false;
  for (const auto &ln : ping_result) {
    // Check for common Docker unavailability errors
    if (ln.find("error during connect") != std::string::npos ||
        ln.find("Cannot connect to the Docker daemon") != std::string::npos ||
        ln.find("Is the docker daemon running") != std::string::npos ||
        ln.find("no puede encontrar") != std::string::npos ||  // Spanish: "cannot find"
        ln.find("El sistema no puede encontrar") != std::string::npos) {
      state.docker_unavailable = true;
      state.docker_loaded = true;
      return;
    }
    // If we see valid Docker info output, it's available
    if (ln.find("Server Version") != std::string::npos ||
        ln.find("Containers:") != std::string::npos) {
      docker_available = true;
    }
  }
  
  // If Docker ping failed completely, mark as unavailable
  if (!docker_available && !ping_result.empty()) {
    state.docker_unavailable = true;
    state.docker_loaded = true;
    return;
  }
  
  auto cl = RunShellLines(
      "docker ps -a --format "
      "'{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.Status}}\t{{.CreatedAt}}'");
  for (const auto &ln : cl) {
    if (ln.empty())
      continue;
    // Skip error lines
    if (ln.find("error during connect") != std::string::npos ||
        ln.find("Cannot connect") != std::string::npos)
      continue;
    std::stringstream ss(ln);
    std::string id, name, image, status, created;
    std::getline(ss, id, '\t');
    std::getline(ss, name, '\t');
    std::getline(ss, image, '\t');
    std::getline(ss, status, '\t');
    std::getline(ss, created, '\t');
    AppState::DockerContainer dc;
    dc.id = id;
    dc.name = name;
    dc.image = image;
    dc.status = status;
    dc.created = created;
    dc.log_path = GuessLogPathForContainer(state, name);
    state.containers.push_back(dc);
  }
  auto il = RunShellLines(
      "docker images --format '{{.Repository}}:{{.Tag}}\t{{.ID}}\t{{.Size}}'");
  for (const auto &ln : il) {
    if (ln.empty())
      continue;
    // Skip error lines
    if (ln.find("error during connect") != std::string::npos ||
        ln.find("Cannot connect") != std::string::npos)
      continue;
    std::stringstream ss(ln);
    std::string repo, id, size;
    std::getline(ss, repo, '\t');
    std::getline(ss, id, '\t');
    std::getline(ss, size, '\t');
    state.images.push_back(AppState::DockerImage{repo, id, size});
  }
  state.docker_loaded = true;
}

static void OpenFolderExternal(const std::string &path) {
#ifdef _WIN32
  std::string p = path;
  for (char &c : p)
    if (c == '/')
      c = '\\';
  std::string cmd = std::string("explorer \"") + p + "\"";
  std::vector<std::string> lines;
#ifdef _WIN32
  DWORD code = 0;
#else
  int code = 0;
#endif
  RunHiddenCapture(cmd, lines, code);
#else
  std::string cmd = std::string("xdg-open \"") + path + "\"";
  std::vector<std::string> lines;
  int code = 0;
  RunHiddenCapture(cmd, lines, code);
#endif
}

std::string BuildCommand(const AppState &state,
                         const std::string &unique_suffix,
                         int selected_mode_override) {
  std::string cmd;

  // Convert paths to Unix format on Windows
#ifdef _WIN32
  std::string task_dir_unix = state.task_directory.empty()
                                  ? ""
                                  : ConvertToUnixPath(state.task_directory);
  std::string workdir_unix =
      state.workdir.empty() ? "" : ConvertToUnixPath(state.workdir);
  std::string output_dir_unix =
      state.output_dir.empty() ? "" : ConvertToUnixPath(state.output_dir);
#else
  const std::string &task_dir_unix = state.task_directory;
  const std::string &workdir_unix = state.workdir;
  const std::string &output_dir_unix = state.output_dir;
#endif

  // Use bash from MSYS2 or Git Bash if available, with proper PATH
#ifdef _WIN32
  std::string bash = FindBash();
  if (bash.empty())
    return "cmd.exe /C echo ERROR: Bash not found. Install Git for Windows or MSYS2.";
#else
  cmd = "bash autobuild/scripts/autobuild.sh";
#endif

  cmd += " ";

  // Build CLI args once
  std::string args;
  int _mode = (selected_mode_override >= 0) ? selected_mode_override
                                            : state.selected_mode;
  switch (_mode) {
  case 0:
    args += "feedback";
    break;
  case 1:
    args += "verify";
    break;
  case 2:
    args += "both";
    break;
  case 3:
    args += "audit";
    break;
  }
  if (!task_dir_unix.empty())
    args += " --task \\\"" + task_dir_unix + "\\\"";
  if (!state.api_key.empty())
    args += " --api-key \\\"" + state.api_key + "\\\"";

  // NEW: Add --no-cache flag if enabled
  if (state.use_docker_no_cache) {
    args += " --no-cache";
  }

  if (!state.image_tag.empty()) {
    std::string image_tag = state.image_tag;
    if (state.auto_lowercase_names) {
      std::transform(image_tag.begin(), image_tag.end(), image_tag.begin(),
                     ::tolower);
    }
    // Add unique suffix to avoid conflicts between concurrent tasks
    if (!unique_suffix.empty()) {
      // Replace :latest with unique suffix
      size_t colon_pos = image_tag.find(":latest");
      if (colon_pos != std::string::npos) {
        image_tag = image_tag.substr(0, colon_pos) + ":" + unique_suffix;
      } else {
        image_tag += ":" + unique_suffix;
      }
    }
    // Ensure the final image name is unique to avoid conflicts with existing
    // images
    image_tag = GenerateUniqueImageName(image_tag);
    args += " --image-tag \\\"" + image_tag + "\\\"";
  } else if (state.auto_lowercase_names && !state.task_directory.empty()) {
    std::string task_dir = state.task_directory;
    size_t last_slash = task_dir.find_last_of("/\\");
    std::string basename = (last_slash != std::string::npos)
                               ? task_dir.substr(last_slash + 1)
                               : task_dir;
    std::transform(basename.begin(), basename.end(), basename.begin(),
                   ::tolower);
    std::string auto_tag = "autobuild-" + basename;
    if (!unique_suffix.empty()) {
      auto_tag += ":" + unique_suffix;
    } else {
      auto_tag += ":latest";
    }
    // Ensure the final image name is unique to avoid conflicts with existing
    // images
    auto_tag = GenerateUniqueImageName(auto_tag);
    args += " --image-tag \\\"" + auto_tag + "\\\"";
  }
  if (!state.container_name.empty()) {
    std::string container_name = state.container_name;
    if (state.auto_lowercase_names) {
      std::transform(container_name.begin(), container_name.end(),
                     container_name.begin(), ::tolower);
    }
    // Add unique suffix to container name to avoid conflicts
    if (!unique_suffix.empty()) {
      container_name += "_from_" + unique_suffix;
    }
    args += " --container-name \\\"" + container_name + "\\\"";
  } else if (state.auto_lowercase_names && !state.task_directory.empty()) {
    std::string task_dir = state.task_directory;
    size_t last_slash = task_dir.find_last_of("/\\");
    std::string basename = (last_slash != std::string::npos)
                               ? task_dir.substr(last_slash + 1)
                               : task_dir;
    std::transform(basename.begin(), basename.end(), basename.begin(),
                   ::tolower);
    std::string auto_container = "autobuild-" + basename;
    // Add unique suffix to container name to avoid conflicts
    if (!unique_suffix.empty()) {
      auto_container += "_from_" + unique_suffix;
    }
    args += " --container-name \\\"" + auto_container + "\\\"";
  }
  if (!workdir_unix.empty())
    args += " --workdir \\\"" + workdir_unix + "\\\"";
  
  // Always provide an output-dir to avoid permission issues in Program Files
  std::string final_output_dir;
  if (!output_dir_unix.empty()) {
    final_output_dir = output_dir_unix;
    // Make output directory unique for each task to avoid conflicts
    if (!unique_suffix.empty()) {
      final_output_dir += "_" + unique_suffix;
    }
  } else {
    // Use default log path configured in GUI (user-accessible location)
    std::string default_log_root = state.log_folder_paths.empty() 
        ? std::string() 
        : state.log_folder_paths[std::max(0, state.selected_log_folder)];
    
    if (default_log_root.empty()) {
      // Fallback: use ResolveDefaultLogsPath logic
#ifdef _WIN32
      std::string exe_dir = GetExecutableDir();
      if (exe_dir.find("Program Files") != std::string::npos) {
        // MSI install: use APPDATA
        const char* appdata = getenv("APPDATA");
        if (appdata != nullptr) {
          default_log_root = std::string(appdata) + "\\Autobuild\\logs";
        } else {
          const char* userprofile = getenv("USERPROFILE");
          if (userprofile != nullptr) {
            default_log_root = std::string(userprofile) + "\\Documents\\Autobuild\\logs";
          }
        }
      } else {
        // Development: use relative to exe
        default_log_root = exe_dir + "/../autobuild/logs";
      }
#elif defined(__APPLE__)
      const char* home = getenv("HOME");
      if (home) {
        default_log_root = std::string(home) + "/Library/Application Support/Autobuild/logs";
      }
#else // Linux
      const char* home = getenv("HOME");
      if (home) {
        default_log_root = std::string(home) + "/.autobuild/logs";
      }
#endif
    }
    
    if (!default_log_root.empty()) {
#ifdef _WIN32
      final_output_dir = ConvertToUnixPath(default_log_root);
#else
      final_output_dir = default_log_root;
#endif
    }
  }
  
  if (!final_output_dir.empty()) {
    args += " --output-dir \\\"" + final_output_dir + "\\\"";
  }

#ifdef _WIN32
  {
    // Get the absolute path to the autobuild.sh script and convert to Unix format
    // Use GetExecutableDir() instead of GetCurrentDirectoryA() to find script relative to exe
    std::string exe_dir = GetExecutableDir();
    // Script is installed inside bin directory: bin/autobuild/scripts/autobuild.sh
    std::string script_path = exe_dir + "/autobuild/scripts/autobuild.sh";
    
    // Convert Windows path to Unix format for bash
    for (char &c : script_path) {
      if (c == '\\') c = '/';
    }
    // Convert C: to /c for MSYS2/Git Bash
    if (script_path.length() >= 2 && script_path[1] == ':') {
      script_path[0] = tolower(script_path[0]);
      script_path = "/" + script_path.substr(0, 1) + script_path.substr(2);
    }
    
    if (g_show_debug_console) {
      ConsoleLog("[DEBUG][Windows] Script path: " + script_path);
    }
      
    const char *head =
        "export PATH=/c/Program\\ "
        "Files/Docker/Docker/resources/bin:/mingw64/bin:/usr/bin:$PATH; export "
        "PYTHONUNBUFFERED=1 PYTHONIOENCODING=utf-8; ";
    // Use single quotes around the script path to handle spaces properly
    std::string quoted_script_path = "'" + script_path + "'";
    
    cmd = std::string("\"") + bash + "\" -lc \"" + head +
          "if command -v stdbuf >/dev/null 2>&1; then stdbuf -oL -eL bash " +
          quoted_script_path + " " + args + "; else bash " + quoted_script_path + " " + args +
          "; fi\"";
  }
#else
  // Mac/Linux: Find script relative to executable location
  std::string exe_dir = GetExecutableDir();
  std::string script_path = exe_dir + "/autobuild/scripts/autobuild.sh";
  
  if (g_show_debug_console) {
    ConsoleLog("[DEBUG][Mac/Linux] Executable dir: " + exe_dir);
    ConsoleLog("[DEBUG][Mac/Linux] Script path: " + script_path);
  }
  
  cmd = "bash '" + script_path + "' " + args;
  
  if (g_show_debug_console) {
    ConsoleLog("[DEBUG][Mac/Linux] BuildCommand result: " + cmd);
  }
#endif

  return cmd;
}

void RenderMainUI(AppState &state) {
  // Critical safety check - ensure ImGui is in a valid state
  if (!GImGui || !GImGui->CurrentWindow) {
    if (state.dev_mode) {
      DevLog(state, "CRITICAL: ImGui context or current window is null!");
    }
    if (g_show_debug_console) {
      ConsoleLog("CRITICAL: ImGui context or current window is null!");
    }
    return;
  }

  // Pre-emptive ID stack validation
  if (g_show_debug_console && GImGui->CurrentWindow) {
    int id_stack_size = GImGui->CurrentWindow->IDStack.Size;
    if (id_stack_size < 1) {
      ConsoleLog(
          "PRE-EMPTIVE WARNING: IDStack size is " +
          std::to_string(id_stack_size) +
          " before rendering - this will likely cause assertion failure!");
    }
  }

  ImGuiIO &io = ImGui::GetIO();
  // Allow the SDL window to handle its own resizing by leaving some margin
  ImGui::SetNextWindowPos(ImVec2(2, g_titlebar_height + 2));
  ImVec2 _size = io.DisplaySize; 
  _size.x -= 4; // Leave margin for resize handles
  _size.y = (_size.y > g_titlebar_height) ? (_size.y - g_titlebar_height - 4) : _size.y;
  // Always update window size to match SDL window size
  ImGui::SetNextWindowSize(_size, ImGuiCond_Always);

  // Set window style properties before the window begins
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // Black border
  
  // Add window fade-in animation
  static bool window_first_frame = true;
  if (window_first_frame) {
    g_animation_manager.StartAnimation("window_fade", 0.5f);
    window_first_frame = false;
  }
  
  float window_alpha = 1.0f;
  if (g_animation_manager.IsAnimationPlaying("window_fade")) {
    auto& anim = g_animation_manager.GetAnimation("window_fade");
    anim.start_value = 0.0f;
    anim.end_value = 1.0f;
    window_alpha = anim.GetValue();
  }
  
  // Apply alpha to window background
  ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
  bg_color.w *= window_alpha;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, bg_color);

  ImGuiWindowScope main_window_scope(
      "Autobuild 2.0 - Verification Orchestrator", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | 
          ImGuiWindowFlags_NoResize);

  // Pop the style variables immediately after window begins to avoid stack issues
  ImGui::PopStyleColor(1); // Border color
  ImGui::PopStyleVar(2);   // WindowRounding and WindowBorderSize

  // Dev overlay is rendered at the end to stay on top

  // Clear keyboard focus when drag begins to allow drag and drop to work
  // properly
  if (state.should_clear_focus) {
    ImGui::SetKeyboardFocusHere(-1); // Clear focus from any active item
    state.should_clear_focus = false;
  }

  // Periodically log stack sizes when dev mode enabled (every 60 frames)
  static int frame_counter = 0;
  if (state.dev_mode && (frame_counter % 60 == 0)) {
    ImGuiContext &g = *GImGui;
    ImGuiWindow *w = g.CurrentWindow;
    std::stringstream ss;
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    ss << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "."
       << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << "IDStack=" << w->IDStack.Size << " ColorStack=" << g.ColorStack.Size
       << " StyleVarStack=" << g.StyleVarStack.Size;
    ss << " FontStack=" << g.FontStack.Size << " Windows=" << g.Windows.Size;
    DevLog(state, ss.str());
  }
  frame_counter++;

  // Clean up dummy IDs from previous frame at the start of new frame
  CleanupImGuiIDStack(state);

  // Track ID stack changes to identify root cause
  TrackIDStackChanges(state);

  // Validate ImGui state for debugging
  ValidateImGuiState(state);
  FixImGuiIDStack(state);

  // Don't clean up dummy IDs here - let them stay until the next frame
  // CleanupImGuiIDStack(state);

  // Track state changes for debugging
  static int last_id_stack_size = -1;
  if (state.dev_mode && GImGui && GImGui->CurrentWindow) {
    int current_id_stack_size = GImGui->CurrentWindow->IDStack.Size;
    if (last_id_stack_size != -1 &&
        current_id_stack_size != last_id_stack_size) {
      DevLog(state, "IDStack changed from " +
                        std::to_string(last_id_stack_size) + " to " +
                        std::to_string(current_id_stack_size));
    }
    last_id_stack_size = current_id_stack_size;

    // Console output for critical state issues
    if (g_show_debug_console) {
      if (current_id_stack_size < 1) {
        ConsoleLog("WARNING: IDStack size is " +
                   std::to_string(current_id_stack_size) +
                   " - this may cause assertion failures!");
      }
      if (current_id_stack_size > 10) {
        ConsoleLog("WARNING: IDStack size is " +
                   std::to_string(current_id_stack_size) +
                   " - unusually high, possible stack leak!");
      }

      // Check for other stack imbalances
      ImGuiContext &g = *GImGui;
      if (g.ColorStack.Size < 0) {
        ConsoleLog("ERROR: ColorStack.Size < 0, PushStyleColor/PopStyleColor "
                   "mismatch!");
      }
      if (g.StyleVarStack.Size < 0) {
        ConsoleLog("ERROR: StyleVarStack.Size < 0, PushStyleVar/PopStyleVar "
                   "mismatch!");
      }
      if (g.FontStack.Size < 0) {
        ConsoleLog("ERROR: FontStack.Size < 0, PushFont/PopFont mismatch!");
      }
    }
  }

  // Header
  ImGui::PushFont(io.Fonts->Fonts[0]);
  ImGui::PopFont();

  // Dev mode indicator
  if (state.dev_mode) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[DEV MODE]");
    ImGui::Separator();
  }


  // Main content area
  bool is_config_tab = false;
  ImGuiTabBarScope _main_tabs("MainTabs");
  if (_main_tabs) {
    {
      ImGuiTabItemScope _tab_config("Configuration");
      if (_tab_config) {
        is_config_tab = true;
        ImGui::Spacing();
        // Docker configuration (all modes are Docker-based now)
        {
          ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
                             "Docker Configuration");
          ImGui::Spacing();

          // Task directory with file browser hint
          ImGui::Text("Task Directory:");
          ImGui::SameLine();
          ImGui::TextDisabled("(?)");
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Drag and drop a folder from Windows Explorer here");
          }

          // Apply pending drop if available
          if (!state.pending_drop_file.empty() &&
              state.drop_target == DropTarget::TaskDirectory) {
            state.task_directory = state.pending_drop_file;
            state.pending_drop_file.clear();
            state.drop_target = DropTarget::None;
            // Validate immediately after drop
            state.validation = ValidateTaskDirectory(state.task_directory);
          }

          char task_buf[512];
          strncpy(task_buf, state.task_directory.c_str(), sizeof(task_buf) - 1);
          task_buf[sizeof(task_buf) - 1] = '\0';
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60);

          // Highlight input field when hovering for drop
          {
            ImVec4 bg = (state.is_hovering_drop_zone &&
                         state.drop_target == DropTarget::TaskDirectory)
                            ? ImVec4(0.3f, 0.5f, 0.7f, 1.0f)
                            : ImVec4(0.25f, 0.30f, 0.35f, 1.0f);
            ImGuiStyleColorScope _bg(ImGuiCol_FrameBg, bg);
            if (ImGui::InputText("##task", task_buf, sizeof(task_buf))) {
              state.task_directory = task_buf;
              // Validate on text change
              state.validation = ValidateTaskDirectory(state.task_directory);
            }
            // Always check for hover to enable drag and drop (even when typing
            // or overlapped)
            if (ImGui::IsItemHovered(
                    ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                    ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                    ImGuiHoveredFlags_AllowWhenOverlappedByItem |
                    ImGuiHoveredFlags_AllowWhenOverlappedByWindow)) {
              state.drop_target = DropTarget::TaskDirectory;
            }
          }

          // Show validation icon
          ImGui::SameLine();
          if (!state.task_directory.empty()) {
            if (DirectoryExists(state.task_directory)) {
              AnimatedStatusIndicator("[OK]", ImVec4(0.4f, 1.0f, 0.4f, 1.0f), false, "task_dir_ok");
              if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Directory exists");
              }
            } else {
              AnimatedStatusIndicator("[X]", ImVec4(1.0f, 0.4f, 0.4f, 1.0f), true, "task_dir_error");
              if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Directory does not exist!");
              }
            }
          }

          // Show validation results
          if (!state.task_directory.empty()) {
            ImGui::Spacing();
            {
              ImGuiChildScope _validation_child("ValidationResults",
                                                ImVec2(0, 180), true);

              // Show found items
              if (!state.validation.found_items.empty()) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Found:");
                for (const auto &item : state.validation.found_items) {
                  ImGui::TextUnformatted(item.c_str());
                }
              }

              // Show missing items
              if (!state.validation.missing_items.empty()) {
                if (!state.validation.found_items.empty()) {
                  ImGui::Spacing();
                }
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Missing:");
                for (const auto &item : state.validation.missing_items) {
                  ImGui::TextUnformatted(item.c_str());
                }
              }

              // Overall status
              if (state.validation.missing_items.empty() &&
                  !state.validation.found_items.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                                   "[OK] Task directory is valid!");
              } else if (!state.validation.missing_items.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "[Warning] Some required files are missing");
              }
            }
          }

          ImGui::Spacing();

          // API Key with show/hide button
          ImGui::Text("Gemini API Key:");
          ImGui::SameLine();

          // Eye button to toggle visibility (disabled if no API key)
          bool has_api_key = !state.api_key.empty();
          if (!has_api_key) {
            ImGuiStyleColorScope _c1(ImGuiCol_Button,
                                     ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGuiStyleColorScope _c2(ImGuiCol_ButtonHovered,
                                     ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGuiStyleColorScope _c3(ImGuiCol_ButtonActive,
                                     ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGuiStyleColorScope _c4(ImGuiCol_Text,
                                     ImVec4(0.5f, 0.5f, 0.5f, 0.6f));
            ImGuiDisabledScope _dis;
            if (ImGui::SmallButton(state.show_api_key ? "Hide" : "Show")) {
              state.show_api_key = !state.show_api_key;
            }
          } else {
            if (ImGui::SmallButton(state.show_api_key ? "Hide" : "Show")) {
              state.show_api_key = !state.show_api_key;
            }
          }
          ImGui::SameLine();
          ImGui::TextDisabled("(?)");
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "API Key is automatically saved to autobuild_gui.json");
          }

          char key_buf[256];
          strncpy(key_buf, state.api_key.c_str(), sizeof(key_buf) - 1);
          key_buf[sizeof(key_buf) - 1] = '\0';
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 240);

          // Use password flag if not showing
          ImGuiInputTextFlags flags =
              state.show_api_key ? 0 : ImGuiInputTextFlags_Password;
          if (ImGui::InputText("##apikey", key_buf, sizeof(key_buf), flags)) {
            std::string new_api_key = key_buf;
            // Only update and save if the API key actually changed
            if (new_api_key != state.api_key) {
              state.api_key = new_api_key;
              SaveConfig(state); // Auto-save when API key changes
            }
          }

          ImGui::Spacing();

          // Optional fields (collapsible) with smooth animation
          static bool advanced_options_open = false;
          bool was_open = advanced_options_open;
          if (ImGui::CollapsingHeader("Advanced Options", &advanced_options_open)) {
            // Start animation when opening
            if (!was_open && advanced_options_open) {
              g_animation_manager.StartAnimation("advanced_options", 0.3f);
            }
            
            // Get animation progress for smooth height transition
            float anim_progress = 1.0f;
            if (g_animation_manager.IsAnimationPlaying("advanced_options")) {
              auto& anim = g_animation_manager.GetAnimation("advanced_options");
              anim.start_value = 0.0f;
              anim.end_value = 1.0f;
              anim_progress = anim.GetValue();
            }
            
            // Apply alpha based on animation progress
            ImVec4 text_color = ImGui::GetStyle().Colors[ImGuiCol_Text];
            text_color.w *= anim_progress;
            ImGui::PushStyleColor(ImGuiCol_Text, text_color);
            
            ImGui::Indent();

            ImGui::Text("Image Tag:");
            char img_buf[256];
            strncpy(img_buf, state.image_tag.c_str(), sizeof(img_buf) - 1);
            img_buf[sizeof(img_buf) - 1] = '\0';
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 240);
            if (ImGui::InputText("##imagetag", img_buf, sizeof(img_buf))) {
              state.image_tag = img_buf;
            }

            ImGui::Text("Container Name:");
            char cont_buf[256];
            strncpy(cont_buf, state.container_name.c_str(),
                    sizeof(cont_buf) - 1);
            cont_buf[sizeof(cont_buf) - 1] = '\0';
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 240);
            if (ImGui::InputText("##container", cont_buf, sizeof(cont_buf))) {
              state.container_name = cont_buf;
            }

            ImGui::Text("Working Directory:");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Drag and drop a folder here");
            }

            // Apply pending drop for workdir
            if (!state.pending_drop_file.empty() &&
                state.drop_target == DropTarget::WorkingDirectory) {
              state.workdir = state.pending_drop_file;
              state.pending_drop_file.clear();
              state.drop_target = DropTarget::None;
            }

            char work_buf[256];
            strncpy(work_buf, state.workdir.c_str(), sizeof(work_buf) - 1);
            work_buf[sizeof(work_buf) - 1] = '\0';
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);

            {
              ImVec4 bg = (state.is_hovering_drop_zone &&
                           state.drop_target == DropTarget::WorkingDirectory)
                              ? ImVec4(0.3f, 0.5f, 0.7f, 1.0f)
                              : ImVec4(0.20f, 0.25f, 0.29f, 1.0f);
              ImGuiStyleColorScope _bg(ImGuiCol_FrameBg, bg);
              if (ImGui::InputText("##workdir", work_buf, sizeof(work_buf))) {
                state.workdir = work_buf;
              }
              // Always check for hover to enable drag and drop (even when
              // typing or overlapped)
              if (ImGui::IsItemHovered(
                      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                      ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                      ImGuiHoveredFlags_AllowWhenOverlappedByItem |
                      ImGuiHoveredFlags_AllowWhenOverlappedByWindow)) {
                state.drop_target = DropTarget::WorkingDirectory;
              }
            }

            // Show validation icon
            ImGui::SameLine();
            if (!state.workdir.empty()) {
              if (DirectoryExists(state.workdir)) {
                AnimatedStatusIndicator("[OK]", ImVec4(0.4f, 1.0f, 0.4f, 1.0f), false, "workdir_ok");
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("Directory exists");
                }
              } else {
                AnimatedStatusIndicator("[X]", ImVec4(1.0f, 0.4f, 0.4f, 1.0f), true, "workdir_error");
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("Directory does not exist!");
                }
              }
            }
            ImGui::NewLine();
            ImGui::Text("Output Directory:");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Drag and drop a folder here");
            }

            // Apply pending drop for output dir
            if (!state.pending_drop_file.empty() &&
                state.drop_target == DropTarget::OutputDirectory) {
              state.output_dir = state.pending_drop_file;
              state.pending_drop_file.clear();
              state.drop_target = DropTarget::None;
            }

            char out_buf[256];
            strncpy(out_buf, state.output_dir.c_str(), sizeof(out_buf) - 1);
            out_buf[sizeof(out_buf) - 1] = '\0';
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);

            {
              ImVec4 bg = (state.is_hovering_drop_zone &&
                           state.drop_target == DropTarget::OutputDirectory)
                              ? ImVec4(0.3f, 0.5f, 0.7f, 1.0f)
                              : ImVec4(0.20f, 0.25f, 0.29f, 1.0f);
              ImGuiStyleColorScope _bg(ImGuiCol_FrameBg, bg);
              if (ImGui::InputText("##outdir", out_buf, sizeof(out_buf))) {
                state.output_dir = out_buf;
              }
              // Always check for hover to enable drag and drop (even when
              // typing or overlapped)
              if (ImGui::IsItemHovered(
                      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                      ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                      ImGuiHoveredFlags_AllowWhenOverlappedByItem |
                      ImGuiHoveredFlags_AllowWhenOverlappedByWindow)) {
                state.drop_target = DropTarget::OutputDirectory;
              }
            }

            // Show validation icon
            ImGui::SameLine();
            if (!state.output_dir.empty()) {
              if (DirectoryExists(state.output_dir)) {
                AnimatedStatusIndicator("[OK]", ImVec4(0.4f, 1.0f, 0.4f, 1.0f), false, "output_dir_ok");
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("Directory exists");
                }
              } else {
                AnimatedStatusIndicator("[X]", ImVec4(1.0f, 0.4f, 0.4f, 1.0f), true, "output_dir_error");
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("Directory does not exist!");
                }
              }
            }

            ImGui::Unindent();
            
            // Pop text color animation
            ImGui::PopStyleColor(1); // Text color
          }
        }

        // Settings section (available for all modes)
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "Settings");
        ImGui::Spacing();

        ImGui::Text("Log Folder Paths:");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Manage multiple log directories. Select one to "
                            "view logs from that location.");
        }

        // Display list of log paths
        ImGui::BeginChild("LogPathsList", ImVec2(0, 100), true);
        for (int i = 0; i < (int)state.log_folder_paths.size(); i++) {

          // Radio button for selection
          if (ImGui::RadioButton("##select", state.selected_log_folder == i)) {
            state.selected_log_folder = i;
            SaveConfig(state);
          }
          ImGui::SameLine();

          // Display path
          ImGui::TextWrapped("%s", state.log_folder_paths[i].c_str());
          ImGui::SameLine();

          // Delete button
          {
            ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                      ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::SmallButton(
                    (std::string("Delete##") + std::to_string(i)).c_str())) {
              state.log_folder_paths.erase(state.log_folder_paths.begin() + i);
              if (state.selected_log_folder >=
                  (int)state.log_folder_paths.size()) {
                state.selected_log_folder =
                    (int)state.log_folder_paths.size() - 1;
              }
              if (state.selected_log_folder < 0)
                state.selected_log_folder = 0;
              SaveConfig(state);
            }
          }
        }
        ImGui::EndChild();

        // Add new log path
        ImGui::Spacing();
        ImGui::Text("Add New Log Path:");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Drag and drop a folder here or type the path");
        }

        // Apply pending drop for new log path
        if (!state.pending_drop_file.empty() &&
            state.drop_target == DropTarget::NewLogPath) {
          state.new_log_path_input = state.pending_drop_file;
          state.pending_drop_file.clear();
          state.drop_target = DropTarget::None;
        }

        char new_path_buf[512];
        strncpy(new_path_buf, state.new_log_path_input.c_str(),
                sizeof(new_path_buf) - 1);
        new_path_buf[sizeof(new_path_buf) - 1] = '\0';
        ImGui::SetNextItemWidth(-200);

        // Highlight input field when hovering for drop
        {
          ImVec4 bg = (state.is_hovering_drop_zone &&
                       state.drop_target == DropTarget::NewLogPath)
                          ? ImVec4(0.3f, 0.5f, 0.7f, 1.0f)
                          : ImVec4(0.20f, 0.25f, 0.29f, 1.0f);
          ImGuiStyleColorScope _bg(ImGuiCol_FrameBg, bg);
          if (ImGui::InputText("##newlogpath", new_path_buf,
                               sizeof(new_path_buf))) {
            state.new_log_path_input = new_path_buf;
          }
          // Always check for hover to enable drag and drop (even when typing or
          // overlapped)
          if (ImGui::IsItemHovered(
                  ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                  ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                  ImGuiHoveredFlags_AllowWhenOverlappedByItem |
                  ImGuiHoveredFlags_AllowWhenOverlappedByWindow)) {
            state.drop_target = DropTarget::NewLogPath;
          }
        }

        ImGui::SameLine();

        // Show validation icon
        if (!state.new_log_path_input.empty()) {
          if (DirectoryExists(state.new_log_path_input)) {
            AnimatedStatusIndicator("[OK]", ImVec4(0.4f, 1.0f, 0.4f, 1.0f), false, "log_path_ok");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Directory exists");
            }
          } else {
            AnimatedStatusIndicator("[X]", ImVec4(1.0f, 0.4f, 0.4f, 1.0f), true, "log_path_error");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Directory does not exist!");
            }
          }
          ImGui::SameLine();
        }

        if (AnimatedButton("Add Path", ImVec2(0, 0), "add_path")) {
          if (!state.new_log_path_input.empty()) {
            // Only add if directory exists
            if (DirectoryExists(state.new_log_path_input)) {
              state.log_folder_paths.push_back(state.new_log_path_input);
              state.new_log_path_input.clear();
              SaveConfig(state);
            }
          }
        }

        // Auto-lowercase image/container names option
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Checkbox("Auto-convert image/container names to lowercase",
                            &state.auto_lowercase_names)) {
          SaveConfig(state);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip(
              "When enabled, automatically converts image tags and container "
              "names to lowercase to avoid Docker errors like:\n'invalid tag: "
              "repository name must be lowercase'");
        }

        // NEW: Docker no-cache option
        ImGui::Spacing();
        if (ImGui::Checkbox("Always use --no-cache for Docker builds",
                            &state.use_docker_no_cache)) {
          SaveConfig(state);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip(
              "Forces Docker to rebuild images from scratch without using "
              "cached layers. Ensures fresh builds every time.");
        }

        // NEW: Max concurrent tasks setting
        ImGui::Spacing();
        ImGui::Text("Maximum Concurrent Tasks:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (ImGui::SliderInt("##maxconcurrent", &state.max_concurrent_tasks, 1,
                             20)) {
          SaveConfig(state);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip(
              "Maximum number of audits/builds that can run simultaneously "
              "(1-20)\n\nWARNING: High values (>10) may:\n- Overload your "
              "system\n- Cause Docker resource conflicts\n- Slow down all "
              "tasks\n- Use excessive memory/CPU");
        }

        // Show warning for high values
        if (state.max_concurrent_tasks > 10) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[WARNING]");
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("High concurrent task limit! This may overload "
                              "your system and cause performance issues.");
          }
        }

        // Individual task counters are now in the main execution area

      } // End Docker configuration section
    }
  }
  // Use SetSelected flag to automatically switch to Manage tab when requested
  ImGuiTabItemFlags manage_tab_flags = 0;
  if (state.switch_to_manage_tab) {
    manage_tab_flags = ImGuiTabItemFlags_SetSelected;
    state.switch_to_manage_tab = false;
  }

  // Manage tab - containers/images and log shortcuts
  {
    ImGuiTabItemScope _tab_manage("Manage", nullptr, manage_tab_flags);
    if (_tab_manage) {
      if (!state.docker_loaded) {
        if (g_fonts_loaded && g_font_awesome_solid) {
          // Use Font Awesome icon inside the button
          ImGui::PushFont(g_font_awesome_solid);
          if (ImGui::Button((std::string(ICON_FA_REFRESH " ") + "Refresh").c_str())) {
            RefreshDockerState(state);
          }
          ImGui::PopFont();
        } else {
          if (ImGui::Button("Refresh")) {
            RefreshDockerState(state);
          }
        }
      } else {
        if (g_fonts_loaded && g_font_awesome_solid) {
          // Use Font Awesome icon inside the button
          ImGui::PushFont(g_font_awesome_solid);
          if (ImGui::Button((std::string(ICON_FA_REFRESH " ") + "Refresh").c_str())) {
            RefreshDockerState(state);
          }
          ImGui::PopFont();
        } else {
          if (ImGui::Button("Refresh")) {
            RefreshDockerState(state);
          }
        }
      }
      ImGui::SameLine();
      ImGui::TextDisabled("(Docker containers, images, logs)");
      ImGui::Separator();

      // Show user-friendly message if Docker is not available
      if (state.docker_unavailable) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f)); // Orange warning color
        ImGui::TextWrapped("Docker Desktop is not running or not accessible.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextWrapped("Please start Docker Desktop and click the Refresh button above to load containers and images.");
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Gray info text
        ImGui::TextWrapped("If Docker is installed:");
        ImGui::BulletText("Windows: Start Docker Desktop from the Start menu");
        ImGui::BulletText("macOS: Start Docker Desktop from Applications");
        ImGui::BulletText("Linux: Run 'sudo systemctl start docker'");
        ImGui::PopStyleColor();
      } else {
        // Docker is available - show containers and images

      // Containers list
      ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Containers");

      // NEW: Bulk container cleanup button
      if (state.containers.size() > 1) {
        ImGui::SameLine();
        ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                  ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("Remove All Containers")) {
          for (const auto &c : state.containers) {
            std::string sh = std::string("docker rm -f \"") + c.name +
                             "\" >/dev/null 2>&1 || true";
            RunShellLines(sh);
          }
          RefreshDockerState(state);
        }
      }

      {
        ImGuiChildScope _containers("containers", ImVec2(0, 200), true);
        for (size_t i = 0; i < state.containers.size(); ++i) {
          const auto &c = state.containers[i];
          
          // Get available width before creating columns
          float available_width = ImGui::GetContentRegionAvail().x;
          
          // Use columns for proper layout with scrollable text
          ImGui::Columns(3, nullptr, false);
          
          // Text column (scrollable if too long) - reserve space for buttons
          ImGui::SetColumnWidth(0, available_width - 160.0f);
          ImGui::SetColumnWidth(1, 90.0f);  // Fixed width for Open Logs button
          ImGui::SetColumnWidth(2, 80.0f);  // Fixed width for Delete button
          
          ImGui::BeginChild(("##container_text_" + std::to_string(i)).c_str(),
                           ImVec2(0, ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 6 - 2),
                           false,
                           ImGuiWindowFlags_HorizontalScrollbar);
          
          // Build the full container info string
          std::string container_info = c.name + " | " + c.image + " | " + c.status;
          ImGui::TextUnformatted(container_info.c_str());
          ImGui::EndChild();
          ImGui::NextColumn();
          
          // Open Logs column
          if (!c.log_path.empty()) {
            if (ImGui::SmallButton(
                    (std::string("Open Logs##") + std::to_string(i)).c_str())) {
              OpenFolderExternal(c.log_path);
            }
          } else {
            ImGui::TextDisabled("(no log)");
          }
          ImGui::NextColumn();
          
          // Delete column
          if (ImGui::SmallButton(
                  (std::string("Delete##") + std::to_string(i)).c_str())) {
            std::string sh = std::string("docker rm -f \"") + c.name +
                             "\" >/dev/null 2>&1 || true";
            RunShellLines(sh);
            RefreshDockerState(state);
          }
          ImGui::NextColumn();
          
          ImGui::Columns(1);
          ImGui::Separator();
        }
      }


      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.6f, 1.0f), "Images");

      // NEW: Bulk image cleanup button
      if (state.images.size() > 1) {
        ImGui::SameLine();
        ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                  ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("Remove All Images")) {
          std::string all_errors;
          bool any_success = false;

          for (const auto &img : state.images) {
            std::string error_msg;
            if (SafeDeleteImage(img.id, error_msg)) {
              any_success = true;
            } else {
              if (!all_errors.empty())
                all_errors += "\n\n";
              all_errors +=
                  "Image " + img.repo_tag + " (" + img.id + "):\n" + error_msg;
            }
          }

          if (any_success) {
            RefreshDockerState(state);
          }

          if (!all_errors.empty()) {
            state.image_delete_error = "Bulk deletion errors:\n\n" + all_errors;
          }
        }
      }

      {
        ImGuiChildScope _images("images", ImVec2(0, 160), true);
        for (size_t i = 0; i < state.images.size(); ++i) {
          const auto &img = state.images[i];
          
          // Get available width before creating columns
          float available_width = ImGui::GetContentRegionAvail().x;
          
          // Use columns for proper layout with scrollable text
          ImGui::Columns(2, nullptr, false);
          
          // Text column (scrollable if too long) - reserve space for button
          ImGui::SetColumnWidth(0, available_width - 80.0f);
          ImGui::SetColumnWidth(1, 80.0f);  // Fixed width for Delete button
          
          ImGui::BeginChild(("##image_text_" + std::to_string(i)).c_str(),
                           ImVec2(0, ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 6 - 2),
                           false,
                           ImGuiWindowFlags_HorizontalScrollbar);
          
          // Build the full image info string
          std::string image_info = img.repo_tag + " | " + img.id + " | " + img.size;
          ImGui::TextUnformatted(image_info.c_str());
          ImGui::EndChild();
          ImGui::NextColumn();
          
          // Delete column
          if (ImGui::SmallButton(
                  (std::string("Delete##") + std::to_string(i)).c_str())) {
            std::string error_msg;
            if (SafeDeleteImage(img.id, error_msg)) {
              RefreshDockerState(state);
            } else {
              // Store error message and show error window
              state.image_delete_error = error_msg;
            }
          }
          ImGui::NextColumn();
          
          ImGui::Columns(1);
          ImGui::Separator();
        }
      }

      // Error popup for image deletion moved to global scope (after TabBar)

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.5f, 1.0f), "Logs Browser");

      // Determine current logs root
      std::string logs_root =
          state.log_folder_paths.empty()
              ? std::string()
              : state.log_folder_paths[std::max(0, state.selected_log_folder)];
      if (!logs_root.empty() && DirectoryExists(logs_root)) {
        // Left: tasks list
        ImGui::BeginChild("logs_tasks",
                          ImVec2(ImGui::GetContentRegionAvail().x * 0.33f, 220),
                          true);
        DIR *d = opendir(logs_root.c_str());
        if (d) {
          int idx = 0;
          struct dirent *e;
          while ((e = readdir(d)) != NULL) {
            if (!e->d_name || e->d_name[0] == '.')
              continue;
            std::string task_dir = logs_root + "/" + e->d_name;
            if (!DirectoryExists(task_dir))
              continue;
            bool selected = (state.selected_task_index == idx);
            if (ImGui::Selectable(e->d_name, selected)) {
              state.selected_task_index = idx;
              state.selected_run_index = -1;
            }
            idx++;
          }
          closedir(d);
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Middle: runs list for selected task
        ImGui::BeginChild("logs_runs",
                          ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 220),
                          true);
        std::string selected_task_name;
        int current_task_idx = 0;
        DIR *d2 = opendir(logs_root.c_str());
        if (d2) {
          struct dirent *e;
          while ((e = readdir(d2)) != NULL) {
            if (!e->d_name || e->d_name[0] == '.')
              continue;
            std::string task_dir = logs_root + "/" + e->d_name;
            if (!DirectoryExists(task_dir))
              continue;
            if (current_task_idx == state.selected_task_index) {
              selected_task_name = e->d_name;
              break;
            }
            current_task_idx++;
          }
          closedir(d2);
        }
        if (!selected_task_name.empty()) {
          std::string task_dir = logs_root + "/" + selected_task_name;
          DIR *d3 = opendir(task_dir.c_str());
          if (d3) {
            int idx = 0;
            struct dirent *e;
            while ((e = readdir(d3)) != NULL) {
              if (!e->d_name || e->d_name[0] == '.')
                continue;
              std::string run_dir = task_dir + "/" + e->d_name;
              if (!DirectoryExists(run_dir))
                continue;
              bool selected = (state.selected_run_index == idx);
              if (ImGui::Selectable(e->d_name, selected)) {
                state.selected_run_index = idx;
              }
              idx++;
            }
            closedir(d3);
          }
        } else {
          ImGui::TextDisabled("Select a task to see runs");
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Right: actions for selected run
        ImGui::BeginChild("logs_actions", ImVec2(0, 220), true);
        if (!selected_task_name.empty() && state.selected_run_index >= 0) {
          // Recompute selected run name
          std::string run_name;
          int idx = 0;
          std::string task_dir = logs_root + "/" + selected_task_name;
          DIR *d4 = opendir(task_dir.c_str());
          if (d4) {
            struct dirent *e;
            while ((e = readdir(d4)) != NULL) {
              if (!e->d_name || e->d_name[0] == '.')
                continue;
              std::string run_dir = task_dir + "/" + e->d_name;
              if (!DirectoryExists(run_dir))
                continue;
              if (idx == state.selected_run_index) {
                run_name = e->d_name;
                break;
              }
              idx++;
            }
            closedir(d4);
          }
          std::string run_dir =
              run_name.empty() ? std::string() : (task_dir + "/" + run_name);
          if (!run_dir.empty()) {
            ImGui::TextWrapped("%s", run_dir.c_str());
            if (AnimatedButton("Open", ImVec2(0, 0), "open_folder")) {
              OpenFolderExternal(run_dir);
            }
            ImGui::SameLine();
            {
              ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                        ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
              if (AnimatedButton("Delete", ImVec2(0, 0), "delete_folder")) {
                state.pending_delete_path = run_dir;
                state.show_confirm_delete = true;
              }
            }
          }
        } else {
          ImGui::TextDisabled("Select a run to manage");
        }
        ImGui::EndChild();
      } else {
        ImGui::TextDisabled(
            "Logs root not found: set a valid log folder in Settings");
      }

      // Confirmation modal moved to global scope (after TabBar)

      } // end else (Docker is available)
      // _tab_manage RAII closes the tab
    }
  }
  // Use SetSelected flag to automatically switch to this tab when requested
  ImGuiTabItemFlags logs_tab_flags = 0;
  if (state.switch_to_logs_tab) {
    logs_tab_flags = ImGuiTabItemFlags_SetSelected;
    state.switch_to_logs_tab = false;
  }

  // NEW: Task Logs tab with closeable sub-tabs for each task
  {
    ImGuiTabItemScope _tab_logs("Task Logs", nullptr, logs_tab_flags);
    if (_tab_logs) {
      state.show_logs = true;
      ImGui::Spacing();

      // Get tasks snapshot
      std::vector<std::shared_ptr<TaskInstance>> tasks_snapshot;
      {
        std::lock_guard<std::mutex> lock(state.tasks_mutex);
        tasks_snapshot = state.tasks;
      }

      if (tasks_snapshot.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "No tasks running. Start a task to see logs here.");
      } else {
        // Create tabs for each task with close buttons
        std::vector<int> tasks_to_remove;
        ImGuiTabBarScope _task_tabs("TaskTabs",
                                    ImGuiTabBarFlags_Reorderable |
                                        ImGuiTabBarFlags_FittingPolicyScroll);
        if (_task_tabs) {

          for (size_t idx = 0; idx < tasks_snapshot.size(); idx++) {
            auto &task = tasks_snapshot[idx];

            // Tab title with status indicator (use an ID suffix instead of
            // PushID/PopID)
            std::string tab_title = task->name;
            if (task->is_running) {
              if (task->container_created.load()) {
                tab_title += " [Running]";
              } else {
                tab_title += " [Creating Container...]";
              }
            } else {
              tab_title += " [Stopped]";
            }

            bool tab_open = true;
            std::string tab_label = tab_title + "##" + std::to_string(task->id);
            ImGuiTabItemScope _tab_task(tab_label.c_str(), &tab_open);
            if (_tab_task) {
              // Use ImGuiStateTracker to monitor ID stack
              ImGuiStateTracker tracker(state);

              ImGui::Spacing();

              // Task controls - always show Manage button
              ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                        ImVec4(0.2f, 0.6f, 0.8f, 1.0f));
              if (g_fonts_loaded && g_font_awesome_solid) {
                ImGui::PushFont(g_font_awesome_solid);
                if (AnimatedButton((std::string(ICON_FA_COG " ") + "Go to Manage Tab").c_str(), ImVec2(0, 0), "manage_tab")) {
                  state.switch_to_manage_tab = true;
                }
                ImGui::PopFont();
              } else {
                if (AnimatedButton("Go to Manage Tab", ImVec2(0, 0), "manage_tab")) {
                  state.switch_to_manage_tab = true;
                }
              }
              
              if (task->is_running) {
                ImGui::SameLine();
                if (g_fonts_loaded && g_font_awesome_solid) {
                  ImGui::PushFont(g_font_awesome_solid);
                  AnimatedStatusIndicator(ICON_FA_RUNNING, ImVec4(0.2f, 0.8f, 0.2f, 1.0f), true, "task_running");
                  ImGui::PopFont();
                } else {
                  AnimatedStatusIndicator("", ImVec4(0.2f, 0.8f, 0.2f, 1.0f), true, "task_running");
                }
                ImGui::SameLine();
                AnimatedStatusIndicator("Running", ImVec4(0.2f, 0.8f, 0.2f, 1.0f), true, "task_running_text");
                
                // Add animated progress indicator
                ImGui::Spacing();
                ImGui::Text("Progress:");
                ImGui::SameLine();
                // Simulate progress based on time running (for demo purposes)
                static float progress_time = 0.0f;
                progress_time += g_animation_manager.delta_time;
                float progress = std::fmod(progress_time * 0.1f, 1.0f); // Slow progress simulation
                
                // Ensure progress bar is always visible with minimum width
                ImVec2 progress_size = ImVec2(200, 20); // Fixed height for better visibility
                AnimatedProgressBar(progress, progress_size, "Processing", "task_progress");
              } else {
                ImGui::SameLine();
                if (g_fonts_loaded && g_font_awesome_regular) {
                  ImGui::PushFont(g_font_awesome_regular);
                  AnimatedStatusIndicator(ICON_FA_STOPPED, ImVec4(0.6f, 0.6f, 0.6f, 1.0f), false, "task_stopped");
                  ImGui::PopFont();
                } else {
                  AnimatedStatusIndicator("", ImVec4(0.6f, 0.6f, 0.6f, 1.0f), false, "task_stopped");
                }
                ImGui::SameLine();
                AnimatedStatusIndicator("Stopped", ImVec4(0.6f, 0.6f, 0.6f, 1.0f), false, "task_stopped_text");
                
                // Show progress bar even when stopped (complete)
                ImGui::Spacing();
                ImGui::Text("Progress:");
                ImGui::SameLine();
                ImVec2 progress_size = ImVec2(200, 20); // Fixed height for better visibility
                AnimatedProgressBar(1.0f, progress_size, "Complete", "task_progress_complete");
              }

              ImGui::SameLine();
              if (AnimatedButton("Clear Logs", ImVec2(0, 0), "clear_logs")) {
                std::lock_guard<std::mutex> lock(task->log_mutex);
                task->log_output.clear();
              }
              ImGui::SameLine();
              if (AnimatedButton("Copy All", ImVec2(0, 0), "copy_all")) {
                std::string all_logs;
                {
                  std::lock_guard<std::mutex> lock(task->log_mutex);
                  for (const auto &line : task->log_output) {
                    all_logs += line + "\n";
                  }
                }
                ImGui::SetClipboardText(all_logs.c_str());
              }

              ImGui::SameLine();
              static bool auto_scroll = true;
              ImGui::Checkbox("Auto-scroll", &auto_scroll);

              ImGui::SameLine();
              ImGui::TextDisabled("|");
              ImGui::SameLine();
              int log_count = 0;
              {
                std::lock_guard<std::mutex> lock(task->log_mutex);
                log_count = (int)task->log_output.size();
              }
              ImGui::Text("Lines: %d", log_count);

              ImGui::Spacing();

              // Search bar
              ImGui::Text("Search:");
              ImGui::SameLine();
              ImGui::SetNextItemWidth(300);
              char search_buf[256];
              strncpy(search_buf, task->log_search_filter.c_str(),
                      sizeof(search_buf));
              search_buf[sizeof(search_buf) - 1] = '\0';
              if (ImGui::InputText("##search", search_buf,
                                   sizeof(search_buf))) {
                task->log_search_filter = search_buf;
              }
              ImGui::SameLine();
              if (AnimatedButton("Clear", ImVec2(0, 0), "clear_search")) {
                task->log_search_filter.clear();
              }

              // Copy logs
              std::vector<std::string> log_copy;
              {
                std::lock_guard<std::mutex> lock(task->log_mutex);
                log_copy = task->log_output;
              }

              ImGui::Spacing();
              ImGui::Separator();
              ImGui::Spacing();

              // Log viewer
              {
                ImGuiChildScope _tasklog("TaskLogArea", ImVec2(0, 0), true,
                                         ImGuiWindowFlags_HorizontalScrollbar);
                ImGuiStyleVarScope _sv(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
                ImGuiTextWrapScope _tw(ImGui::GetContentRegionAvail().x);

                for (size_t i = 0; i < log_copy.size(); i++) {
                  const auto &line = log_copy[i];

                  // Apply search filter
                  if (!task->log_search_filter.empty()) {
                    std::string line_lower = line;
                    std::string filter_lower = task->log_search_filter;
                    std::transform(line_lower.begin(), line_lower.end(),
                                   line_lower.begin(), ::tolower);
                    std::transform(filter_lower.begin(), filter_lower.end(),
                                   filter_lower.begin(), ::tolower);
                    if (line_lower.find(filter_lower) == std::string::npos) {
                      continue;
                    }
                  }

                  // Color coding
                  ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);

                  if (line.find("[ERROR]") != std::string::npos ||
                      line.find("error:") != std::string::npos ||
                      line.find("Error") != std::string::npos ||
                      line.find("failed") != std::string::npos) {
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                  } else if (line.find("[SUCCESS]") != std::string::npos ||
                             line.find("success") != std::string::npos ||
                             line.find("Passed") != std::string::npos) {
                    color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                  } else if (line.find("[WARN]") != std::string::npos ||
                             line.find("warning:") != std::string::npos) {
                    color = ImVec4(1.0f, 0.9f, 0.3f, 1.0f);
                  } else if (line.find("[INFO]") != std::string::npos) {
                    color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                  } else if (line.find("[STOPPED]") != std::string::npos) {
                    color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
                  }

                  ImGui::PushStyleColor(ImGuiCol_Text, color);
                  ImGui::TextWrapped("%s", line.c_str());
                  ImGui::PopStyleColor();
                }

                // Auto-scroll
                if (auto_scroll && task->is_running &&
                    ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
                  ImGui::SetScrollHereY(1.0f);
                }
              }
            }

            // Check if tab was closed
            if (!tab_open) {
              // Guardrail: Prevent closing tab if task is still running
              if (task->is_running) {
                // Show a message and prevent closing
                if (g_show_debug_console) {
                  ConsoleLog("[DEBUG] Task " + task->name + " is running, preventing tab closure");
                }
                state.show_cannot_close_popup = true;
                // Reset tab_open to prevent closing
                tab_open = true;
              } else {
                // Mark for removal (RemoveTask will handle stopping if needed)
                tasks_to_remove.push_back(task->id);

                // Force ID stack validation and fix when tab is closed
                ValidateImGuiState(state);
                FixImGuiIDStack(state);
              }
            }

            // No PopID needed (we used a unique label instead)
          }
          // Remove closed tasks
          for (int task_id : tasks_to_remove) {
            RemoveTask(state, task_id);
          }

          // Force ID stack validation and fix after tab operations
          ValidateImGuiState(state);
          FixImGuiIDStack(state);
        }
      }
    }
  }

  // Popup for preventing tab closure when task is running
  if (state.show_cannot_close_popup) {
    if (g_show_debug_console) {
      ConsoleLog("[DEBUG] Opening 'Cannot Close Running Task' popup");
    }
    ImGui::OpenPopup("Cannot Close Running Task");
    state.show_cannot_close_popup = false;
  }
  if (ImGui::BeginPopupModal("Cannot Close Running Task", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (g_show_debug_console) {
      ConsoleLog("[DEBUG] 'Cannot Close Running Task' popup is now visible");
    }
    ImGui::Text("Cannot close this tab while the task is still running.");
    ImGui::Text("Please wait for the task to complete or stop it first.");
    ImGui::Separator();
    
    // Center the OK button
    float button_width = 120.0f;
    float available_width = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((available_width - button_width) * 0.5f);
    
    if (ImGui::Button("OK", ImVec2(button_width, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Process Monitor tab 
  {
    ImGuiTabItemScope _tab_proc("Process Monitor");
    if (_tab_proc) {
      ImGui::Spacing();

      // Get running task count
      int running_count = GetRunningTaskCount(state);

      ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Active Processes");
      ImGui::SameLine();
      ImGui::Text("(%d running / %d max)", running_count,
                  state.max_concurrent_tasks);

      ImGui::Spacing();

      // Control buttons
      if (running_count > 0) {
        // Check if any tasks are still creating containers
        int creating_containers = 0;
        int ready_to_stop = 0;
        {
          std::lock_guard<std::mutex> lock(state.tasks_mutex);
          for (const auto &task : state.tasks) {
            if (task->is_running) {
              if (task->container_created.load()) {
                ready_to_stop++;
              } else {
                creating_containers++;
              }
            }
          }
        }

        if (creating_containers > 0 && ready_to_stop == 0) {
          // All tasks are still creating containers
          ImGuiDisabledScope _dis;
          ImGui::Button("Stop All Tasks (Creating Containers...)");
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.0f, 1.0f),
                             "All tasks creating containers...");
        } else if (creating_containers > 0 && ready_to_stop > 0) {
          // Some tasks ready, some still creating
          ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
          if (g_fonts_loaded && g_font_awesome_solid) {
            ImGui::PushFont(g_font_awesome_solid);
            if (ImGui::Button((std::string(ICON_FA_STOP " ") + "Stop All Tasks").c_str())) {
              StopAllTasks(state);
            }
            ImGui::PopFont();
          } else {
            if (ImGui::Button("Stop All Tasks")) {
              StopAllTasks(state);
            }
          }
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.0f, 1.0f),
                             "(%d creating containers, %d ready)",
                             creating_containers, ready_to_stop);
        } else {
          // All tasks ready to stop
          ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
          if (g_fonts_loaded && g_font_awesome_solid) {
            ImGui::PushFont(g_font_awesome_solid);
            if (ImGui::Button((std::string(ICON_FA_STOP " ") + "Stop All Tasks").c_str())) {
              StopAllTasks(state);
            }
            ImGui::PopFont();
          } else {
            if (ImGui::Button("Stop All Tasks")) {
              StopAllTasks(state);
            }
          }
        }
      } else {
        ImGuiDisabledScope _dis;
        ImGui::Button("Stop All Tasks");
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Tasks list
      std::vector<std::shared_ptr<TaskInstance>> tasks_snapshot;
      {
        std::lock_guard<std::mutex> lock(state.tasks_mutex);
        tasks_snapshot = state.tasks;
      }

      if (tasks_snapshot.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "No tasks to display.");
      } else {
        ImGui::BeginChild("ProcessList", ImVec2(0, 0), true);

        for (auto &task : tasks_snapshot) {

          // Status indicator
          if (task->is_running) {
            if (task->container_created.load()) {
              AnimatedStatusIndicator("[Running]", ImVec4(0.3f, 1.0f, 0.3f, 1.0f), true, "proc_running");
            } else {
              // Show loading spinner for container creation
              AnimatedLoadingSpinner("Creating Container...", 6.0f, "container_spinner", 0.5f);
            }
          } else {
            AnimatedStatusIndicator("[Stopped]", ImVec4(0.7f, 0.7f, 0.7f, 1.0f), false, "proc_stopped");
          }

          ImGui::SameLine();
          ImGui::Text("Task #%d: %s", task->id, task->name.c_str());

          ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150);

          // Always show Manage button to redirect to Manage tab for deletion
          ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                    ImVec4(0.2f, 0.6f, 0.8f, 1.0f));
          if (AnimatedButton(
                  (std::string("Manage##") + std::to_string(task->id))
                      .c_str(), ImVec2(0, 0), ("manage_proc_" + std::to_string(task->id)).c_str())) {
            state.switch_to_manage_tab = true;
          }

          // Show command
          ImGui::Indent();
          ImGui::TextDisabled("Command: %s", task->command.c_str());
          ImGui::Unindent();

          ImGui::Separator();
        }

        ImGui::EndChild();
        // _tab_proc RAII closes the tab
      }
    }
  }

   // About tab
   {
    ImGuiTabItemScope _tab_about("About");
    if (_tab_about) {
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f),"Version: 2.0.0");
      ImGui::Text("Build: %s", __DATE__);
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Text("Core Features:");
      ImGui::BulletText("Docker-based verification workflows");
      ImGui::BulletText("Gemini CLI integration for AI-powered verification");
      ImGui::BulletText("Multi-task execution with concurrent task management");
      ImGui::BulletText("Real-time task monitoring and logging");
      ImGui::BulletText("Cross-platform GUI (Windows, Linux, macOS)");
      ImGui::BulletText("Modern OpenGL animation system");
      ImGui::BulletText("Professional MSI installer with license");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Text("Verification Modes:");
      ImGui::BulletText("Feedback - Interactive development with Gemini Prompt 1 & 2");
      ImGui::BulletText("Verify - Reproduce customer command sequences");
      ImGui::BulletText("Both - Run feedback then verify back-to-back");
      ImGui::BulletText("Audit - Analyze verify.sh and prompt for clarity");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Text("Advanced Features:");
      ImGui::BulletText("Docker container and image management");
      ImGui::BulletText("Log aggregation and search across multiple tasks");
      ImGui::BulletText("Configurable concurrent task limits");
      ImGui::BulletText("Task directory validation and auto-detection");
      ImGui::BulletText("Drag-and-drop task folder support");
      ImGui::BulletText("Real-time Docker state monitoring");
      ImGui::BulletText("Custom title bar with window controls");
      ImGui::BulletText("FontAwesome icon integration");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Text("Technology Stack:");
      ImGui::BulletText("C99 (core library)");
      ImGui::BulletText("x86_64 Assembly (performance)");
      ImGui::BulletText("C++17 (GUI)");
      ImGui::BulletText("SDL2 + Dear ImGui (interface)");
      ImGui::BulletText("OpenGL 4.1 (animation)");
      ImGui::BulletText("GLM (math library)");
      ImGui::BulletText("Bash (orchestration)");
      ImGui::BulletText("Docker (containerization)");
      ImGui::BulletText("Google Gemini API (AI verification)");
      ImGui::BulletText("WiX Toolset (Windows installer)");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Text("License: MIT");
      ImGui::Text("Copyright (c) 2025 Autobuild");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Keyboard Shortcuts:");
      ImGui::BulletText("Ctrl+D - Toggle Developer Mode");
      ImGui::BulletText("Ctrl+M - Toggle Metrics Window (Only in Dev Mode)");
      ImGui::BulletText("Ctrl+S - Toggle Style Editor (Only in Dev Mode)");
      ImGui::BulletText("Ctrl+O - Toggle Demo Window (Only in Dev Mode)");
      ImGui::BulletText("Ctrl+C - Toggle Debug Info (Only in Dev Mode)");
      // No manual EndTabItem() here; RAII handles it
    }
  }

  // Global modals (render every frame regardless of active tab)
  // Image delete error display (using regular window to avoid assertions)
  if (!state.image_delete_error.empty()) {
    // Draw semi-transparent overlay to simulate modal behavior
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("ImageDeleteOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs)) {
      // This window just provides the overlay background
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    
    // Draw the actual error window on top
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 0));
    if (ImGui::Begin("Image Delete Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error deleting image:");
      ImGui::Separator();
      ImGuiChildScope _err_content("ErrorContent", ImVec2(0, 200), true,
                                   ImGuiWindowFlags_HorizontalScrollbar);
      
      // Parse and display the error message with proper icon rendering
      std::istringstream error_stream(state.image_delete_error);
      std::string line;
      bool is_first_line = true;
      
      while (std::getline(error_stream, line)) {
        if (is_first_line) {
          // First line (error title) - no icon
          ImGui::TextWrapped("%s", line.c_str());
          is_first_line = false;
        } else if (line.find("Containers using this image:") != std::string::npos) {
          // Section header - no icon
          ImGui::TextWrapped("%s", line.c_str());
        } else if (line.find("Please stop and remove") != std::string::npos) {
          // Instructions - no icon
          ImGui::TextWrapped("%s", line.c_str());
        } else if (line.find(ICON_FA_CUBE) != std::string::npos) {
          // Container line with icon - render with Font Awesome and proper wrapping
          if (g_fonts_loaded && g_font_awesome_solid) {
            ImGui::PushFont(g_font_awesome_solid);
            // Render the icon first
            ImGui::TextUnformatted("  ");
            ImGui::SameLine();
            ImGui::TextUnformatted(ICON_FA_CUBE);
            ImGui::PopFont();
            
            // Get the rest of the line after the icon
            std::string container_info = line.substr(line.find(ICON_FA_CUBE) + std::strlen(ICON_FA_CUBE));
            if (!container_info.empty() && container_info[0] == ' ') {
              container_info = container_info.substr(1); // Remove leading space
            }
            
            // Render the container info with proper wrapping
            ImGui::SameLine();
            ImGui::TextWrapped("%s", container_info.c_str());
          } else {
            ImGui::TextWrapped("%s", line.c_str());
          }
        } else if (!line.empty()) {
          // Other lines - regular text
          ImGui::TextWrapped("%s", line.c_str());
        }
      }
      ImGui::Separator();
      float button_width = 120.0f;
      float available_width = ImGui::GetContentRegionAvail().x;
      ImGui::SetCursorPosX((available_width - button_width) * 0.5f);
      if (AnimatedButton("OK", ImVec2(button_width, 0), "error_ok")) {
        state.image_delete_error.clear();
      }
    }
    ImGui::End();
  }

  // Confirm delete modal
  if (state.show_confirm_delete) {
    ImGui::OpenPopup("Confirm Delete");
    // Center the popup on screen
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  }
  if (ImGui::BeginPopupModal("Confirm Delete", nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove )) {
    ImGui::SetWindowSize(ImVec2(400, 0));
    ImGui::TextWrapped("Delete this logs directory?\n%s",
                       state.pending_delete_path.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float button_width = 130.0f;
    float spacing = 20.0f;
    float total_width = button_width * 2 + spacing;
    float available_width = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((available_width - total_width) * 0.7f);

    {
      ImGuiStyleColorScope _btn(ImGuiCol_Button,
                                ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
      bool confirmed = AnimatedButton("Yes, Delete", ImVec2(button_width, 0), "confirm_delete");
      if (confirmed) {
        RemoveDirectoryRecursive(state.pending_delete_path);
        state.pending_delete_path.clear();
        state.show_confirm_delete = false;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    bool cancelled = AnimatedButton("Cancel", ImVec2(button_width, 0), "cancel_delete");
    if (cancelled) {
      state.pending_delete_path.clear();
      state.show_confirm_delete = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Bottom action bar - only show in Configuration tab
  if (is_config_tab) {
    ImGui::Separator();
    ImGui::Spacing();

    bool can_execute = !state.is_running;
    bool is_valid = true;
    std::string status_message = "Ready";
    ImVec4 status_color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); // Green

    // All modes are Docker modes now (feedback, verify, both, audit)
    if (state.task_directory.empty()) {
      can_execute = false;
      is_valid = false;
      status_message = "Missing: Task Directory";
      status_color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // Red
    } else if (!DirectoryExists(state.task_directory)) {
      can_execute = false;
      is_valid = false;
      status_message = "Task directory does not exist";
      status_color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // Red
    } else if (!state.validation.has_env_dir ||
               !state.validation.has_dockerfile) {
      can_execute = false;
      is_valid = false;
      status_message = "Task directory incomplete (missing env/ or Dockerfile)";
      status_color = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange
    } else if (state.selected_mode != 3 && (!state.validation.has_verify_dir ||
                                            !state.validation.has_verify_sh ||
                                            !state.validation.has_prompt)) {
      // For non-audit modes, require verify directory and prompt
      can_execute = false;
      is_valid = false;
      status_message = "Task directory incomplete (missing verify/ or prompt)";
      status_color = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange
    } else if (state.api_key.empty()) {
      can_execute = false;
      is_valid = false;
      status_message = "Missing: API Key";
      status_color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // Red
    } else {
      status_message = "Ready to execute";
    }

    if (state.is_running) {
      status_message = "Running...";
      status_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Yellow
      can_execute = false;
    }

    // NEW: Individual task counters and execution
    int running_count = GetRunningTaskCount(state);
    bool at_limit = (running_count >= state.max_concurrent_tasks);
    int available_slots = state.max_concurrent_tasks - running_count;

    // Task counters section
    ImGui::Text("Task Counts:");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Set how many of each task type to run when clicking the buttons "
          "below\n\nSliders automatically limit to available concurrent slots");
    }

    // Show warning if at limit
    if (at_limit) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[AT LIMIT]");
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum concurrent tasks reached! Stop some tasks "
                          "to start new ones.");
      }
    } else if (available_slots < 3) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[WARNING]");
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Only %d concurrent slots available. Consider stopping some tasks.",
            available_slots);
      }
    }

    ImGui::Spacing();

    // Create a grid of task counters and buttons
    const float button_width = 120.0f;
    const float counter_width = 60.0f;
    const float spacing = 10.0f;

    // Helper function to create task row
    auto CreateTaskRow = [&](const char *name, int &count, int mode,
                             const char *task_type) {
      ImGui::Text("%s:", name);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(counter_width);

      // Calculate max value for slider (don't exceed available slots)
      int max_slider = std::min(10, available_slots);
      if (max_slider < 1)
        max_slider = 1; // At least 1

      // Clamp current value to valid range
      if (count > max_slider)
        count = max_slider;
      if (count < 1)
        count = 1; // Ensure minimum of 1

      if (ImGui::SliderInt(("##" + std::string(name) + "_count").c_str(),
                           &count, 1, max_slider)) {
        SaveConfig(state);
      }
      ImGui::SameLine();

      // Show helpful status information
      if (available_slots == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[FULL]");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("All %d concurrent slots are occupied. Stop some tasks to free up slots.",
                            state.max_concurrent_tasks);
        }
        ImGui::SameLine();
      } else if (available_slots < 3) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[%d LEFT]", available_slots);
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%d concurrent slots available out of %d total",
                            available_slots, state.max_concurrent_tasks);
        }
        ImGui::SameLine();
      } else {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "[READY]");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%d concurrent slots available", available_slots);
        }
        ImGui::SameLine();
      }

      // Align button vertically with slider by using same height
      float slider_height = ImGui::GetFrameHeight();

      // Compute per-row executability instead of relying on global selected mode
      bool row_can_execute = true;
      if (state.task_directory.empty() || !DirectoryExists(state.task_directory))
        row_can_execute = false;
      else if (!state.validation.has_env_dir || !state.validation.has_dockerfile)
        row_can_execute = false;
      else if (mode != 3 && (!state.validation.has_verify_dir ||
                              !state.validation.has_verify_sh ||
                              !state.validation.has_prompt))
        row_can_execute = false;
      else if (state.api_key.empty())
        row_can_execute = false;

      // Disable button if this row can't execute or if trying to run more than
      // available slots
      bool should_disable = !row_can_execute || (count > available_slots);
      
      if (should_disable) {
        ImGui::BeginDisabled();
      }
      // Add unique ID to prevent conflicts
      ImGui::PushID((std::string(task_type) + "_button").c_str());
      if (AnimatedButton(("Run (" + std::to_string(count) + ")").c_str(),
                        ImVec2(button_width, slider_height), (std::string(task_type) + "_run").c_str())) {
        state.selected_mode = mode;
        StartMultipleTasks(state, task_type, count);
      }
      ImGui::PopID();
      if (should_disable) {
        ImGui::EndDisabled();
      }
    };

    // Row 1: Feedback
    CreateTaskRow("Feedback", state.feedback_count, 0, "Feedback");

    // Row 2: Verify
    CreateTaskRow("Verify", state.verify_count, 1, "Verify");

    // Row 3: Both
    CreateTaskRow("Both", state.both_count, 2, "Both");

    // Row 4: Audit
    CreateTaskRow("Audit", state.audit_count, 3, "Audit");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Status message with animation
    if (at_limit) {
      status_message = "At maximum concurrent tasks (" +
                       std::to_string(state.max_concurrent_tasks) + ")";
      status_color = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
    } else if (running_count > 0) {
      status_message = "Ready (" + std::to_string(running_count) + " running)";
      status_color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
    }

    // Determine if status should pulse (for warnings/errors)
    bool should_pulse = (status_color.x > 0.8f || status_color.y < 0.5f); // Red or orange colors
    AnimatedStatusIndicator(status_message.c_str(), status_color, should_pulse, "main_status");
  }

  // Pop window background color
  ImGui::PopStyleColor(1); // WindowBg
  
  // Render dev overlay last so it remains visible on top
  if (state.dev_mode) {
    RenderDevOverlay(state, io);
  }

  // main_window_scope dtor calls ImGui::End()
}

int main(int argc, char **argv) {

  // Parse command line arguments
  bool show_debug_info = false;
  bool show_help = false;
  bool disable_assertions = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
      show_debug_info = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      show_help = true;
    } else if (strcmp(argv[i], "--no-assert") == 0 ||
               strcmp(argv[i], "-n") == 0) {
      disable_assertions = true;
    }
  }

  if (show_help) {
    printf("Autobuild 2.0 - Verification Orchestrator\n");
    printf("Usage: %s [options]\n", argv[0]);
    printf("\nOptions:\n");
    printf("  --debug, -d    Show debug information and issues in console\n");
    printf("  --no-assert, -n  Disable ImGui assertions (prevents dialog "
           "boxes)\n");
    printf("  --help, -h     Show this help message\n");
    printf("\nDebug mode will:\n");
    printf("  - Show ImGui state information in console\n");
    printf("  - Display ID stack warnings and errors\n");
    printf("  - Log all debug messages to console\n");
    printf("  - Show performance metrics\n");
    printf("  - Catch ImGui assertion failures and log them\n");
    printf(
        "\nUse --no-assert to prevent assertion dialog boxes from appearing\n");
    return 0;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow(
      "Autobuild", 
      SDL_WINDOWPOS_CENTERED, 
      SDL_WINDOWPOS_CENTERED, 
      1280, 720, 
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  
  // Set window size constraints
  SDL_SetWindowMinimumSize(window, 800, 600);
  SDL_SetWindowMaximumSize(window, 2560, 1440);

  // Cross-platform custom title bar: remove OS title bar and draw our own
  TitleBarState titlebar;
  titlebar.enabled = true; // set false to go back to native title bar
  if (titlebar.enabled) {
    SDL_SetWindowBordered(window, SDL_FALSE);
  }
  g_titlebar_height = titlebar.enabled ? titlebar.height : 0.0f;

#ifdef _WIN32
  // Windows runtime icon from embedded resource (see CMake .rc embedding)
  {
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version);
    if (SDL_GetWindowWMInfo(window, &wmi)) {
      HWND hwnd = (HWND)wmi.info.win.window;
      HINSTANCE hInst = GetModuleHandle(NULL);
      HICON big = (HICON)LoadImage(hInst, TEXT("IDI_APPICON"), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
      HICON small = (HICON)LoadImage(hInst, TEXT("IDI_APPICON"), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
      if (big)   SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big);
      if (small) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
    }
  }
#endif

  // Optionally set an app window icon here (see docs)

  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  // Load default font first (required for proper text rendering)
  // Ensure default font loads with proper glyph ranges
  ImFontConfig default_config;
  default_config.GlyphRanges = io.Fonts->GetGlyphRangesDefault();
  io.Fonts->AddFontDefault(&default_config);
  
  // Load Font Awesome fonts with error handling
  // Use smaller font size to match ImGui default (around 13px)
  // Try multiple possible paths for font files
  const char* possible_solid_paths[] = {
    "resources/fonts/fa-solid-900.ttf",
    "./resources/fonts/fa-solid-900.ttf",
    "../resources/fonts/fa-solid-900.ttf",
    "../../resources/fonts/fa-solid-900.ttf"
  };
  
  const char* possible_regular_paths[] = {
    "resources/fonts/fa-regular-400.ttf",
    "./resources/fonts/fa-regular-400.ttf",
    "../resources/fonts/fa-regular-400.ttf",
    "../../resources/fonts/fa-regular-400.ttf"
  };
  
  const char* solid_font_path = "resources/fonts/fa-solid-900.ttf";
  const char* regular_font_path = "resources/fonts/fa-regular-400.ttf";
  
  // Find the correct path for solid font
  for (int i = 0; i < 4; i++) {
    FILE* test = fopen(possible_solid_paths[i], "rb");
    if (test) {
      solid_font_path = possible_solid_paths[i];
      fclose(test);
      break;
    }
  }
  
  // Find the correct path for regular font
  for (int i = 0; i < 4; i++) {
    FILE* test = fopen(possible_regular_paths[i], "rb");
    if (test) {
      regular_font_path = possible_regular_paths[i];
      fclose(test);
      break;
    }
  }
  
  // Load Font Awesome fonts with proper glyph ranges
  // Fix for FontAwesome 6 blurriness: disable MergeMode and use larger font size
  static const ImWchar icon_ranges[] = { 
    0xf000, 0xf8ff,  // Font Awesome range
    0
  };
  
  ImFontConfig solid_config;
  solid_config.MergeMode = true;
  solid_config.GlyphRanges = icon_ranges;
  solid_config.GlyphMinAdvanceX = 13.0f; // Set minimum advance for better rendering
  
  ImFontConfig regular_config;
  regular_config.MergeMode = true;
  regular_config.GlyphRanges = icon_ranges;
  regular_config.GlyphMinAdvanceX = 13.0f; // Set minimum advance for better rendering
  
  // Use larger font size to prevent blurriness
  g_font_awesome_solid = io.Fonts->AddFontFromFileTTF(solid_font_path, 13.0f, &solid_config);
  if (!g_font_awesome_solid) {
    // Fallback to default font
    g_font_awesome_solid = io.Fonts->Fonts[0];
  }
  
  g_font_awesome_regular = io.Fonts->AddFontFromFileTTF(regular_font_path, 13.0f, &regular_config);
  if (!g_font_awesome_regular) {
    // Fallback to default font
    g_font_awesome_regular = io.Fonts->Fonts[0];
  }
  
  // Check if both fonts loaded successfully
  g_fonts_loaded = (g_font_awesome_solid != nullptr && g_font_awesome_regular != nullptr);
  
  if (g_fonts_loaded) {
    if (show_debug_info) printf("Font Awesome fonts loaded successfully\n");
  } else {
    if (show_debug_info) printf("Font Awesome fonts failed to load, using fallback text\n");
    // Ensure we have valid font pointers even if loading failed
    if (!g_font_awesome_solid) g_font_awesome_solid = io.Fonts->Fonts[0];
    if (!g_font_awesome_regular) g_font_awesome_regular = io.Fonts->Fonts[0];
  }
  
  // Build font atlas
  io.Fonts->Build();

  // Set modern style
  SetModernStyle();

  // Application state
  AppState state;

  // Enable debug console output if requested
  g_show_debug_console = show_debug_info;
  if (show_debug_info) {
    printf("Debug logging enabled\n");
    printf("Press Ctrl+D to toggle dev mode in GUI\n");
    printf(
        "Press Ctrl+M for metrics, Ctrl+S for style editor, Ctrl+O for demo\n");
    printf("========================================\n");
  }

  // Apply assertion behavior based on --no-assert flag
  g_imgui_disable_asserts = disable_assertions;
  if (disable_assertions) {
#ifdef _WIN32
    // Suppress CRT assertion dialogs and continue execution
    DisableWindowsAssertDialogs();
#endif
    ImGui::GetIO().ConfigDebugIsDebuggerPresent = false;
    printf(
        "Assertions disabled: continuing after ImGui asserts (no dialogs)\n");
  } else {
    // Default: allow assertion dialogs/breaks
    ImGui::GetIO().ConfigDebugIsDebuggerPresent = true;
  }

  // Load configuration from file
  LoadConfig(state);

  // Main loop
  bool running = true;
  while (running) {
    // Update animations
    g_animation_manager.Update();
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      // Handle debugging shortcuts BEFORE ImGui processes the event
      if (event.type == SDL_KEYDOWN) {
        if ((event.key.keysym.mod & KMOD_CTRL) &&
            event.key.keysym.sym == SDLK_d) {
          state.dev_mode = !state.dev_mode;
          if (!state.dev_mode) {
            // Turn off all dev-related windows when exiting dev mode
            state.show_metrics = false;
            state.show_style_editor = false;
            state.show_demo = false;
            state.show_debug_console = false;
          }
          DevLog(state, std::string("dev_mode toggled: ") +
                            (state.dev_mode ? "ON" : "OFF"));
          continue;
        }
        if (state.dev_mode && (event.key.keysym.mod & KMOD_CTRL) &&
            event.key.keysym.sym == SDLK_m) {
          bool new_value = !state.show_metrics;
          state.show_metrics = new_value;
          if (new_value)
            state.bring_front_metrics = true;
          DevLog(state, std::string("metrics window toggled: ") +
                            (state.show_metrics ? "ON" : "OFF"));
          continue;
        }
        if (state.dev_mode && (event.key.keysym.mod & KMOD_CTRL) &&
            event.key.keysym.sym == SDLK_s) {
          bool new_value = !state.show_style_editor;
          state.show_style_editor = new_value;
          if (new_value)
            state.bring_front_style = true;
          DevLog(state, std::string("style editor toggled: ") +
                            (state.show_style_editor ? "ON" : "OFF"));
          continue;
        }
        if (state.dev_mode && (event.key.keysym.mod & KMOD_CTRL) &&
            event.key.keysym.sym == SDLK_o) {
          bool new_value = !state.show_demo;
          state.show_demo = new_value;
          if (new_value)
            state.bring_front_demo = true;
          DevLog(state, std::string("demo window toggled: ") +
                            (state.show_demo ? "ON" : "OFF"));
          continue;
        }
        if (state.dev_mode && (event.key.keysym.mod & KMOD_CTRL) &&
            event.key.keysym.sym == SDLK_c) {
          state.show_debug_console = !state.show_debug_console;
          DevLog(state, std::string("debug console toggled: ") +
                            (state.show_debug_console ? "ON" : "OFF"));
          continue;
        }
      }

      ImGui_ImplSDL2_ProcessEvent(&event);
      
      if (event.type == SDL_QUIT)
        running = false;
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE)
        running = false;
      
      // Handle window resize events
      if (event.type == SDL_WINDOWEVENT &&
          (event.window.event == SDL_WINDOWEVENT_RESIZED ||
           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        // Get the new window size
        int new_width = event.window.data1;
        int new_height = event.window.data2;
        
        // Update the renderer viewport to match the new window size
        SDL_RenderSetViewport(renderer, NULL);
        SDL_RenderSetLogicalSize(renderer, new_width, new_height);
      }

      // Handle file drop events from OS
      if (event.type == SDL_DROPFILE) {
        char *dropped_filedir = event.drop.file;
        if (dropped_filedir) {
          state.pending_drop_file = std::string(dropped_filedir);
          state.is_hovering_drop_zone = false;
          SDL_free(dropped_filedir);
        }
      }

      // Track when dragging over window
      if (event.type == SDL_DROPBEGIN) {
        state.is_hovering_drop_zone = true;
        state.should_clear_focus =
            true; // Clear input focus to allow drag and drop
      }
      if (event.type == SDL_DROPCOMPLETE) {
        state.is_hovering_drop_zone = false;
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Render UI with error handling
    try {
      RenderMainUI(state);

      // Render debug windows
      if (state.show_metrics) {
        if (state.bring_front_metrics) {
          ImGui::SetWindowFocus("Dear ImGui Metrics/Debugger");
          state.bring_front_metrics = false;
        }
        ImGui::ShowMetricsWindow(&state.show_metrics);
      }
      if (state.show_style_editor) {
        if (state.bring_front_style) {
          ImGui::SetNextWindowFocus();
          state.bring_front_style = false;
        }
        ImGui::Begin("Style Editor", &state.show_style_editor);
        ImGui::ShowStyleEditor();
        ImGui::End();
      }
      if (state.show_demo) {
        if (state.bring_front_demo) {
          ImGui::SetWindowFocus("Dear ImGui Demo");
          state.bring_front_demo = false;
        }
        ImGui::ShowDemoWindow(&state.show_demo);
      }
      if (state.show_debug_console) {
        ImGui::Begin("Debug Info", &state.show_debug_console);
        ImGui::Text("Debug Info - Use this for runtime debugging");
        ImGui::Separator();

        // Show current state
        ImGui::Text("Dev Mode: %s", state.dev_mode ? "ON" : "OFF");
        ImGui::Text("Metrics: %s", state.show_metrics ? "ON" : "OFF");
        ImGui::Text("Style Editor: %s", state.show_style_editor ? "ON" : "OFF");
        ImGui::Text("Demo Window: %s", state.show_demo ? "ON" : "OFF");

        ImGui::Separator();
        ImGui::Text("Keyboard Shortcuts:");
        ImGui::Text("Ctrl+D - Toggle Dev Mode");
        ImGui::Text("Ctrl+M - Toggle Metrics (Only in Dev Mode)");
        ImGui::Text("Ctrl+S - Toggle Style Editor (Only in Dev Mode)");
        ImGui::Text("Ctrl+O - Toggle Demo Window (Only in Dev Mode)");
        ImGui::Text("Ctrl+C - Toggle Debug Info (Only in Dev Mode)");

        ImGui::Separator();
        if (ImGui::Button("Clear All Debug Windows")) {
          state.show_metrics = false;
          state.show_style_editor = false;
          state.show_demo = false;
          state.show_debug_console = false;
        }

        ImGui::End();
      }

    } catch (const std::exception &e) {
      std::string error_msg =
          "EXCEPTION in RenderMainUI: " + std::string(e.what());
      if (state.dev_mode) {
        DevLog(state, error_msg);
      }
      if (g_show_debug_console) {
        ConsoleLog("CRITICAL: " + error_msg);
      }
    } catch (...) {
      std::string error_msg = "UNKNOWN EXCEPTION in RenderMainUI";
      if (state.dev_mode) {
        DevLog(state, error_msg);
      }
      if (g_show_debug_console) {
        ConsoleLog("CRITICAL: " + error_msg);
      }
    }

    // Draw custom top bar last so it stays on top of content
    if (RenderCustomTitleBarSimple(window, titlebar)) {
      running = false;
    }

    // Rendering
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 28, 34, 40, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
    
  }

  // Save configuration before exit
  SaveConfig(state);

  // Wait for command thread to finish if still running
  if (state.command_thread.joinable()) {
    state.command_thread.join();
  }

  // Cleanup
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}



  static void MinimizeWithOSAnim(SDL_Window* window) {
  #ifdef _WIN32
      SDL_SysWMinfo info; SDL_VERSION(&info.version);
      if (SDL_GetWindowWMInfo(window, &info)) {
          HWND hwnd = info.info.win.window;
          // Use standard minimize with proper animation
          ShowWindow(hwnd, SW_MINIMIZE);
          return;
      }
  #endif
      // Use SDL minimize for all other platforms (macOS, Linux)
      // SDL provides proper native minimize on macOS with animation
      SDL_MinimizeWindow(window);
  }
  
  #ifdef _WIN32
  static void BeginNativeDrag(SDL_Window* window) {
      SDL_SysWMinfo info; SDL_VERSION(&info.version);
      if (SDL_GetWindowWMInfo(window, &info)) {
          HWND hwnd = info.info.win.window;
          ReleaseCapture();
          SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
  }
  #else
  // macOS/Linux: Use ImGui's built-in drag functionality
  static void BeginNativeDrag(SDL_Window* window) {
      // SDL doesn't have native drag API on macOS/Linux
      // The drag is handled in the title bar rendering code via ImGui
      // This provides smooth dragging on all platforms
      (void)window; // Unused parameter
  }
  #endif
  

  static bool RenderCustomTitleBarSimple(SDL_Window *window, TitleBarState &tb) {
      if (!tb.enabled)
          return false;
  
      ImGuiViewport *vp = ImGui::GetMainViewport();
      ImVec2 pos = vp->Pos;
      ImVec2 size = ImVec2(vp->Size.x, tb.height);
  
      // Snap to pixel grid to reduce shimmer
      ImGui::SetNextWindowPos(ImVec2((float)(int)pos.x, (float)(int)pos.y));
      ImGui::SetNextWindowSize(ImVec2((float)(int)size.x, (float)(int)size.y));
  
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                               ImGuiWindowFlags_NoNav;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, tb.bg_color);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
  
      bool open = ImGui::Begin("##TopBarSimple", nullptr, flags);
      bool request_close = false;
  
      if (open) {
          ImVec2 avail = ImGui::GetContentRegionAvail();
          float button_w = 40.0f;
          float button_h = tb.height - 12.0f;
          float total_w = button_w * 3.0f;
  
          ImGui::TextUnformatted("Autobuild");
  
          float right_x = ImGui::GetCursorPosX() + avail.x - total_w;
          float y = 6.0f;
          ImGui::SetCursorPos(ImVec2(right_x, y));
  
          bool hovered_any_button = false;
  
          // Minimize
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
          if (ImGui::Button(ICON_FA_WINDOW_MINIMIZE, ImVec2(button_w, button_h))) {
              MinimizeWithOSAnim(window);
          }
          hovered_any_button |= ImGui::IsItemHovered();
          ImGui::PopStyleColor(4);
  
          ImGui::SameLine(0.0f, 0.0f);
          // Maximize / Restore
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
          bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
          if (ImGui::Button(maximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE, ImVec2(button_w, button_h))) {
              if (maximized) SDL_RestoreWindow(window);
              else SDL_MaximizeWindow(window);
          }
          hovered_any_button |= ImGui::IsItemHovered();
          ImGui::PopStyleColor(4);
  
          ImGui::SameLine(0.0f, 0.0f);
          // Close
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
          if (ImGui::Button("X", ImVec2(button_w, button_h))) {
              request_close = true;
          }
          hovered_any_button |= ImGui::IsItemHovered();
          ImGui::PopStyleColor(4);
  
          // Handle drag
          ImGuiIO &io = ImGui::GetIO();
          bool over_bar = ImGui::IsWindowHovered() && !hovered_any_button;
  
          if (over_bar && ImGui::IsMouseClicked(0)) {
          #ifdef _WIN32
              BeginNativeDrag(window); // Smooth native drag
          #else
              tb.dragging = true;
              int mx, my; SDL_GetGlobalMouseState(&mx, &my);
              tb.drag_start_mouse = ImVec2((float)mx, (float)my);
              int wx, wy; SDL_GetWindowPosition(window, &wx, &wy);
              tb.drag_start_window = ImVec2((float)wx, (float)wy);
          #endif
          }
      #ifndef _WIN32
          if (tb.dragging && ImGui::IsMouseDown(0)) {
              int mx, my; SDL_GetGlobalMouseState(&mx, &my);
              int new_x = (int)(tb.drag_start_window.x + (mx - tb.drag_start_mouse.x));
              int new_y = (int)(tb.drag_start_window.y + (my - tb.drag_start_mouse.y));
              SDL_SetWindowPosition(window, new_x, new_y);
          }
          if (tb.dragging && ImGui::IsMouseReleased(0)) tb.dragging = false;
      #endif
      }
  
      ImGui::End();
      ImGui::PopStyleColor(2); // text and window bg
      ImGui::PopStyleVar(3);   // rounding, border size, padding
      return request_close;
  }
  