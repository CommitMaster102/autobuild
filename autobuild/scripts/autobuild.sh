#!/usr/bin/env bash
set -Eeuo pipefail

# Autobuild verification orchestrator
#
# Two-step automation:
# 1) feedback  - build and run container, install/run Gemini CLI with Prompt 1,
#                 then (if verification passes) run Prompt 2. Copies verify assets and prompt.
# 2) verify    - reproduce customer's command sequence on a fresh container using npx Gemini CLI.
# 3) both      - runs feedback then verify back-to-back with a fresh container for verify.

log_info() { echo "[INFO]  $*"; }
log_warn() { echo "[WARN]  $*" 1>&2; }
log_error(){ echo "[ERROR] $*" 1>&2; }
die()      { log_error "$*"; exit 1; }

usage() {
  cat <<EOF
Usage:
  $(basename "$0") feedback --task <abs_task_dir> [--image-tag <tag>] [--container-name <name>] [--workdir <path>] [--api-key <key>] [--output-dir <dir>]
  $(basename "$0") verify   --task <abs_task_dir> [--image-tag <tag>] [--container-name <name>] [--workdir <path>] [--api-key <key>] [--output-dir <dir>]
  $(basename "$0") both     --task <abs_task_dir> [--image-tag <tag>] [--container-name <base>] [--workdir <path>] [--api-key <key>] [--output-dir <dir>]
  $(basename "$0") audit    --task <abs_task_dir> [--image-tag <tag>] [--container-name <name>] [--workdir <path>] [--api-key <key>] [--output-dir <dir>]
Arguments:
  --task            Absolute path to task folder containing env/, verify/, prompt (file or directory)
  --image-tag       Docker image tag to build/use (default: autobuild-<task_name>:latest)
  --container-name  Container base name (default: derived from task name)
  --workdir         Container working directory override (default: parsed from Dockerfile or /workspace)
  --api-key         Gemini API key (default: from GEMINI_API_KEY env)
  --output-dir      Directory to save logs/prompts (default: <workspace>/feedback/<task_name>/<timestamp>)

Notes:
  - feedback: Installs @google/gemini-cli@0.3.0-preview.1 in the container and runs Prompt 1;
              if verification succeeds, runs Prompt 2. Copies verify assets and prompt to workdir.
  - verify:   Runs the exact customer command sequence on a fresh container using npx.
  - both:     Runs feedback then verify back-to-back; verify uses a fresh container.
  - audit:    Runs an audit prompt on the task container.
  - All outputs are saved under the logs root (default: <workspace>/logs). Override via AUTOBUILD_LOGS_ROOT or --output-dir.
EOF
}

require_cmd() { command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"; }
resolve_abs_path() { local p="$1"; if [ -d "$p" ] || [ -f "$p" ]; then echo "$(cd "$(dirname "$p")" && pwd -P)/$(basename "$p")"; else echo "$p"; fi; }
derive_task_name() { basename "$1"; }
derive_image_tag() { echo "autobuild-$(basename "$1"):latest"; }

# Convert MSYS2 path to Windows path for Docker on Windows
# Uses cygpath if available for accurate conversion, otherwise manual conversion
to_windows_path() {
  local path="$1"
  # Check if running on Windows (MSYS2/Git Bash)
  if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    # Try using cygpath for accurate conversion (handles /tmp/, etc.)
    if command -v cygpath >/dev/null 2>&1; then
      cygpath -w "$path" 2>/dev/null || echo "$path"
    else
      # Fallback: Manual conversion for drive letters
      if [[ "$path" =~ ^/([a-z])/(.*) ]]; then
        local drive="${BASH_REMATCH[1]}"
        local rest="${BASH_REMATCH[2]}"
        echo "${drive^^}:/${rest}"
      else
        echo "$path"
      fi
    fi
  else
    echo "$path"
  fi
}

run_and_capture() {
  local logfile="$1"; shift
  mkdir -p "$(dirname "$logfile")"
  set +e
  "$@" 2>&1 | tee -a "$logfile"
  local rc=${PIPESTATUS[0]}
  set -e
  return $rc
}

parse_workdir_from_dockerfile() {
  local env_dir="$1"; local dockerfile="$env_dir/Dockerfile"
  if [ -f "$dockerfile" ]; then
    local wd
    wd=$(grep -iE '^[[:space:]]*WORKDIR[[:space:]]+' "$dockerfile" | tail -n 1 | awk '{print $2}') || true
    if [ -n "${wd:-}" ]; then echo "$wd"; return 0; fi
  fi
  echo "/workspace"
}

select_prompt_file() {
  local prompt_dir="$1"
  if [ -f "$prompt_dir/prompt.txt" ]; then echo "$prompt_dir/prompt.txt"; return 0; fi
  local first_file; first_file=$(find "$prompt_dir" -maxdepth 1 -type f -print | head -n 1 || true)
  [ -n "${first_file:-}" ] || die "No prompt file found in: $prompt_dir"
  echo "$first_file"
}

get_prompt_path() {
  local task_dir="$1"
  local p="$task_dir/prompt"
  if [ -f "$p" ]; then echo "$p"; return 0; fi
  if [ -d "$p" ]; then select_prompt_file "$p"; return 0; fi
  # Also allow prompt.txt directly under task
  if [ -f "$task_dir/prompt.txt" ]; then echo "$task_dir/prompt.txt"; return 0; fi
  die "Missing prompt file or directory: expected $task_dir/prompt or $task_dir/prompt.txt"
}

read_verification_command_from_path() {
  local verify_path="$1"
  if [ -d "$verify_path" ]; then
    if [ -f "$verify_path/verification_command" ]; then cat "$verify_path/verification_command"; return 0; fi
    if [ -f "$verify_path/verify.sh" ]; then echo "bash verify.sh"; return 0; fi
    if [ -f "$verify_path/command" ]; then cat "$verify_path/command"; return 0; fi
    die "No verification command found in directory: $verify_path"
  fi
  if [ -f "$verify_path" ]; then
    cat "$verify_path"; return 0
  fi
  die "Verification path not found: $verify_path"
}

build_image() {
  local env_dir="$1"; local image_tag="$2"; local logfile="${3:-}"; local no_cache_flag="${4:-}"
  local env_dir_win="$(to_windows_path "$env_dir")"
  log_info "Building image: $image_tag from $env_dir"
  
  # If --no-cache is specified, check if we can remove the existing image
  if [ -n "$no_cache_flag" ]; then
    log_info "Using --no-cache flag for Docker build"
    if docker image inspect "$image_tag" >/dev/null 2>&1; then
      # Check if any containers are using this image
      local containers_using_image
      containers_using_image=$(docker ps -aq --filter "ancestor=$image_tag" 2>/dev/null | wc -l)
      
      if [ "$containers_using_image" -gt 0 ]; then
        log_warn "Image $image_tag is being used by $containers_using_image container(s). Building with --no-cache but keeping existing image."
        log_warn "Consider stopping containers using this image for a complete fresh rebuild."
      else
        log_info "Removing existing image to force fresh build: $image_tag"
        docker rmi -f "$image_tag" >/dev/null 2>&1 || log_warn "Could not remove image $image_tag (may be in use)"
      fi
    fi
  fi
  
  if [ -n "$logfile" ]; then
    run_and_capture "$logfile" docker build $no_cache_flag -t "$image_tag" "$env_dir_win"
  else
    docker build $no_cache_flag -t "$image_tag" "$env_dir_win"
  fi
}

run_container_keepalive() {
  local image_tag="$1"; local container_name="$2"
  log_info "Starting container: $container_name"
  # Use MSYS_NO_PATHCONV to prevent Windows path conversion on volume mounts
  MSYS_NO_PATHCONV=1 docker run -v /var/run/docker.sock:/var/run/docker.sock --name "$container_name" -d -i "$image_tag" sleep infinity >/dev/null
}

run_container_customer_exact() {
  local image_tag="$1"; local container_name="$2"
  log_info "Starting container (customer sequence): $container_name"
  # Use MSYS_NO_PATHCONV to prevent Windows path conversion on volume mounts
  MSYS_NO_PATHCONV=1 docker run -v /var/run/docker.sock:/var/run/docker.sock --name "$container_name" -d -i "$image_tag" >/dev/null || true
}

ensure_container_running() {
  local container_name="$1"
  if ! docker ps --format '{{.Names}}' | grep -qx "$container_name"; then
    # Container failed to start or exited - show logs for debugging
    echo "[ERROR] Container $container_name is not running after docker run"
    echo "[ERROR] Container logs:"
    docker logs "$container_name" 2>&1 || echo "[ERROR] Could not retrieve container logs"
    echo "[ERROR] Container status:"
    docker ps -a --filter "name=^${container_name}$" --format "table {{.Names}}\t{{.Status}}\t{{.Image}}" || true
    die "Container $container_name failed to start or exited immediately"
  fi
}

container_id_of() { local container_name="$1"; docker ps -aqf name="^${container_name}$"; }

copy_prompt_into_container() {
  local prompt_file="$1"; local container_name="$2"; local workdir="$3"
  local prompt_win="$(to_windows_path "$prompt_file")"
  log_info "Copying prompt.txt into container"
  MSYS_NO_PATHCONV=1 docker cp "$prompt_win" "$container_name:$workdir/prompt.txt"
}


copy_verify_to_container() {
  local verify_path="$1"; local container_name="$2"; local workdir="$3"
  local verify_win="$(to_windows_path "$verify_path")"
  if [ -d "$verify_path" ]; then
    log_info "Copying verify directory into container workdir: $workdir"
    MSYS_NO_PATHCONV=1 docker cp "$verify_win/." "$container_name:$workdir/"
  elif [ -f "$verify_path" ]; then
    log_info "Copying verify file into container workdir: $workdir"
    MSYS_NO_PATHCONV=1 docker cp "$verify_win" "$container_name:$workdir/"
  else
    die "Verify path does not exist: $verify_path"
  fi
}

compose_prompt1_file() {
  local src_prompt_file="$1"; local out_file="$2"
  cat >"$out_file" <<'EOF'
**Task:** 

1.  Read the user request from the `prompt` file and execute the specified tasks. 
2.  Use the `verify.sh` script to test your solution. 

**Analysis of `verify.sh`:** 

Upon completion of the task, provide a concise analysis of the `verify.sh` script's effectiveness. Your summary should address the following: 

*   **Sufficiency:** Does the script contain adequate tests to confirm a successful task completion? 
*   **Over-testing:** Does the script make rigid assumptions about the solution's implementation that might incorrectly fail a valid approach? 
*   **Scope:** Does the script test for requirements not explicitly stated in `prompt`? 

---
Below is the content of prompt.txt for this task. Treat it as the user request:
---
EOF
  echo >>"$out_file"
  cat "$src_prompt_file" >>"$out_file"
}

compose_prompt2_file() {
  local out_file="$1"
  cat >"$out_file" <<'EOF'
**Hypothetical Scenario:** 

If the `verify.sh` script had not been provided, could you have successfully completed the task as defined in `prompt.txt`? 

**Prompt and Verification Analysis:** 

Identify any ambiguities or under-specified elements in either the `prompt.txt` or the `verify.sh` script that could have led to a failed test.
EOF
}

# --- build the Minimal Audit Prompt into a file
compose_audit_prompt_file() {
  local out_file="$1"
  cat >"$out_file" <<'EOF'
# Minimal Audit Prompt (for Gemini CLI)

Context (read-only):
- _context/prompt.txt   = task
- _context/verify/      = verifier (entire folder)
- _context/Dockerfile   = environment contract (initial state)

Working environment:
- You are executing inside the environment created by the Dockerfile.
- All files/subfolders in the current working directory (except `_context/`) are the live environment.
- `_context/` is reference-only and read-only.

Task:
Analyze only. Determine if the verifier is valid for the task, if the task is clear enough to verify, and whether the verifier would also accept other valid implementations (within constraints). Do not implement or propose fixes.

Output in EXACTLY this format:
<VERIFY_VALID>Yes/No</VERIFY_VALID>
<VERIFY_REASON>[1–2 sentences. Explicitly address: behavior vs implementation, over-constraint vs prompt/Dockerfile invariants, environment/path assumptions, functional coverage, hardcoded/irrelevant data, and prompt–verify alignment.]</VERIFY_REASON>
<PROMPT_CLEAR>Yes/No</PROMPT_CLEAR>
<PROMPT_REASON>[1–2 sentences on whether the task is clear enough to verify and why.]</PROMPT_REASON>
<OTHER_VALID_SOLUTIONS_OK>Yes/No</OTHER_VALID_SOLUTIONS_OK>
<OTHER_SOLUTIONS_REASON>[1–2 sentences on whether the verifier would pass other valid solutions under the constraints and why.]</OTHER_SOLUTIONS_REASON>

Constraints:
- Treat Dockerfile-defined paths, names, and platform as environment invariants (valid hardcoding).
- Do NOT invent requirements beyond prompt.txt or implied by the Dockerfile.
- Do NOT suggest modifying or implementing anything; audit only.
- Do NOT modify _context/prompt.txt, _context/verify/*, or _context/Dockerfile.
- Keep each reason to 1–2 sentences.
EOF
}

# --- copy prompt/verify/Dockerfile into WORKDIR/_context inside container
# --- copy prompt/verify/Dockerfile into WORKDIR/_context inside container
copy_context_into_container() {
  local task_dir="$1"; local container_name="$2"; local workdir="$3"
  local env_dir="$task_dir/env"
  local verify_dir="$task_dir/verify"

  # Resolve files (use existing helper)
  local prompt_path; prompt_path="$(get_prompt_path "$task_dir")"
  [ -f "$prompt_path" ] || die "Prompt file not found (resolved to: $prompt_path)"
  [ -d "$verify_dir" ] || die "Missing $verify_dir directory"
  [ -f "$verify_dir/verify.sh" ] || die "Missing $verify_dir/verify.sh"
  [ -f "$env_dir/Dockerfile" ]  || die "Missing $env_dir/Dockerfile"

  log_info "Using PROMPT:     $prompt_path"
  log_info "Using VERIFY DIR: $verify_dir"
  log_info "Using DOCKERFILE: $env_dir/Dockerfile"

  docker exec -u root "$container_name" bash -lc "mkdir -p '$workdir/_context/verify'"

  local prompt_win="$(to_windows_path "$prompt_path")"
  local verify_dir_win="$(to_windows_path "$verify_dir")"
  local dockerfile_win="$(to_windows_path "$env_dir/Dockerfile")"
  
  MSYS_NO_PATHCONV=1 docker cp "$prompt_win"      "$container_name:$workdir/_context/prompt.txt"
  MSYS_NO_PATHCONV=1 docker cp "$verify_dir_win/." "$container_name:$workdir/_context/verify/"
  MSYS_NO_PATHCONV=1 docker cp "$dockerfile_win"  "$container_name:$workdir/_context/Dockerfile"
}


feedback() {
  local task_dir="$1"; local image_tag="$2"; local container_name="$3"; local workdir="$4"; local gemini_api_key="$5"; local log_dir="$6"; local no_cache_flag="${7:-}"
  local env_dir="$task_dir/env"
  local verify_dir_candidate="$task_dir/verify"
  local verify_file_candidate="$task_dir/command"
  local prompt_path; prompt_path=$(get_prompt_path "$task_dir")

  [ -d "$env_dir" ] || die "Missing env directory: $env_dir"

  local verify_path=""
  if [ -d "$verify_dir_candidate" ] || [ -f "$verify_dir_candidate" ]; then
    verify_path="$verify_dir_candidate"
  elif [ -f "$verify_file_candidate" ]; then
    verify_path="$verify_file_candidate"
  else
    die "Missing verify path: expected $verify_dir_candidate (dir or file) or $verify_file_candidate (file)"
  fi

  mkdir -p "$log_dir"

  build_image "$env_dir" "$image_tag" "$log_dir/docker_build.log" "$no_cache_flag"
  if [ -z "$workdir" ]; then workdir=$(parse_workdir_from_dockerfile "$env_dir"); log_info "Using WORKDIR from Dockerfile: $workdir"; fi

  run_container_keepalive "$image_tag" "$container_name"; ensure_container_running "$container_name"
  docker exec -u root "$container_name" bash -lc "mkdir -p '$workdir'"

  copy_verify_to_container "$verify_path" "$container_name" "$workdir"
  copy_prompt_into_container "$prompt_path" "$container_name" "$workdir"

  # Prepare prompts on host and copy into container and logs
  local tmpdir; tmpdir=$(mktemp -d)
  compose_prompt1_file "$prompt_path" "$tmpdir/prompt1.txt"
  compose_prompt2_file "$tmpdir/prompt2.txt"
  # Remove existing files to avoid conflicts
  rm -f "$log_dir/prompt1.txt" "$log_dir/prompt2.txt"
  cp "$tmpdir/prompt1.txt" "$log_dir/prompt1.txt"
  cp "$tmpdir/prompt2.txt" "$log_dir/prompt2.txt"
  local p1_win="$(to_windows_path "$tmpdir/prompt1.txt")"
  local p2_win="$(to_windows_path "$tmpdir/prompt2.txt")"
  MSYS_NO_PATHCONV=1 docker cp "$p1_win" "$container_name:$workdir/prompt1.txt"
  MSYS_NO_PATHCONV=1 docker cp "$p2_win" "$container_name:$workdir/prompt2.txt"

  # Install Gemini CLI globally inside the container
  log_info "Installing Gemini CLI inside container"
  run_and_capture "$log_dir/gemini_install.log" docker exec -u root "$container_name" bash -lc "which npm >/dev/null 2>&1 || (echo 'npm is required in the image' >&2; exit 1); npm install -g @google/gemini-cli@0.3.0-preview.1"

  # Run Prompt 1
  log_info "Running Prompt 1 with gemini CLI"
  run_and_capture "$log_dir/gemini_prompt1.log" docker exec -i -e GEMINI_API_KEY="$gemini_api_key" "$container_name" bash -lc "cd '$workdir' && PROMPT=\$(cat prompt1.txt) && gemini --debug -y --prompt \"\$PROMPT\""

  # Run verification
  local verification_cmd; verification_cmd=$(read_verification_command_from_path "$verify_path")
  printf '%s' "$verification_cmd" > "$log_dir/verification_command.txt"
  log_info "Running verification: $verification_cmd"
  set +e
  run_and_capture "$log_dir/verification.log" docker exec -u root "$container_name" bash -lc "cd '$workdir' && chmod +x verify.sh 2>/dev/null || true && $verification_cmd"
  local verify_rc=$?
  set -e

  if [ "$verify_rc" -eq 0 ]; then
    log_info "Verification passed; running Prompt 2"
    # Defensively ensure prompt2.txt exists in the container workdir (some verify steps may clean files)
    if ! docker exec -u root "$container_name" bash -lc "test -f '$workdir/prompt2.txt'"; then
      log_warn "prompt2.txt missing in container; re-copying before Prompt 2"
      MSYS_NO_PATHCONV=1 docker cp "$p2_win" "$container_name:$workdir/prompt2.txt"
    fi
    run_and_capture "$log_dir/gemini_prompt2.log" docker exec -i -e GEMINI_API_KEY="$gemini_api_key" "$container_name" bash -lc "cd '$workdir' && PROMPT=\$(cat prompt2.txt) && gemini --debug -y --prompt \"\$PROMPT\""
  else
    log_warn "Verification failed; skipping Prompt 2. Exit code: $verify_rc"
  fi

  log_info "Feedback step complete. Container left running: $container_name"
}

verify() {
  local task_dir="$1"; local image_tag="$2"; local container_name="$3"; local workdir="$4"; local gemini_api_key="$5"; local log_dir="$6"; local no_cache_flag="${7:-}"
  local env_dir="$task_dir/env"
  local verify_dir_candidate="$task_dir/verify"
  local verify_file_candidate="$task_dir/command"
  local prompt_path; prompt_path=$(get_prompt_path "$task_dir")

  [ -d "$env_dir" ] || die "Missing env directory: $env_dir"

  local verify_path=""
  if [ -d "$verify_dir_candidate" ] || [ -f "$verify_dir_candidate" ]; then
    verify_path="$verify_dir_candidate"
  elif [ -f "$verify_file_candidate" ]; then
    verify_path="$verify_file_candidate"
  else
    die "Missing verify path: expected $verify_dir_candidate (dir or file) or $verify_file_candidate (file)"
  fi

  mkdir -p "$log_dir"

  # Read RAW prompt content only (no additional instructions) and log it
  local prompt_raw; prompt_raw=$(cat "$prompt_path")
  printf '%s' "$prompt_raw" > "$log_dir/prompt_raw.txt"

  build_image "$env_dir" "$image_tag" "$log_dir/docker_build.log" "$no_cache_flag"
  run_container_customer_exact "$image_tag" "$container_name"; ensure_container_running "$container_name"

  # Copy prompt to container to avoid path conversion issues with MSYS2
  local tmpdir; tmpdir=$(mktemp -d)
  printf '%s' "$prompt_raw" > "$tmpdir/prompt_raw.txt"
  local praw_win="$(to_windows_path "$tmpdir/prompt_raw.txt")"
  MSYS_NO_PATHCONV=1 docker cp "$praw_win" "$container_name:/tmp/prompt_raw.txt"
  rm -rf "$tmpdir"

  log_info "Running Gemini via npx in container (customer sequence)"
  run_and_capture "$log_dir/gemini_npx.log" docker exec -i -e GEMINI_API_KEY="$gemini_api_key" "$container_name" \
    bash -c 'npx --yes @google/gemini-cli@0.3.0-preview.1 --yolo --debug --prompt "$(cat /tmp/prompt_raw.txt)"'

  local cid; cid=$(container_id_of "$container_name")
  log_info "docker inspect $cid"
  docker inspect "$cid" > "$log_dir/docker_inspect.json"

  if [ -z "$workdir" ]; then workdir=$(parse_workdir_from_dockerfile "$env_dir"); log_info "Using WORKDIR from Dockerfile: $workdir"; fi
  docker exec -u root "$cid" bash -lc "mkdir -p '$workdir'"

  # Copy verify files only AFTER Gemini run to avoid leaking hints to the agent
  copy_verify_to_container "$verify_path" "$container_name" "$workdir"
  # Also provide the raw prompt to the container for any verification scripts that expect prompt.txt
  copy_prompt_into_container "$prompt_path" "$container_name" "$workdir"

  local verification_cmd; verification_cmd=$(read_verification_command_from_path "$verify_path")
  printf '%s' "$verification_cmd" > "$log_dir/verification_command.txt"
  log_info "Executing verification in container: $verification_cmd"
  run_and_capture "$log_dir/verification.log" docker exec -u root "$cid" bash -lc "cd '$workdir' && chmod +x verify.sh 2>/dev/null || true && $verification_cmd"

  log_info "Verify step complete. Container left running: $container_name"
}

audit() {
  local task_dir="$1"; local image_tag="$2"; local container_name="$3"
  local workdir="$4";  local gemini_api_key="$5"; local log_dir="$6"; local no_cache_flag="${7:-}"
  local env_dir="$task_dir/env"

  [ -d "$env_dir" ] || die "Missing env directory: $env_dir"
  mkdir -p "$log_dir"

  # Build image (adjust if your Dockerfile needs the task root as context)
  build_image "$env_dir" "$image_tag" "$log_dir/docker_build.log" "$no_cache_flag"
  if [ -z "$workdir" ]; then
    workdir=$(parse_workdir_from_dockerfile "$env_dir")
    log_info "Using WORKDIR from Dockerfile: $workdir"
  fi

  run_container_keepalive "$image_tag" "$container_name"
  ensure_container_running "$container_name"
  docker exec -u root "$container_name" bash -lc "mkdir -p '$workdir'"

  # Copy prompt/verify/Dockerfile into _context
  copy_context_into_container "$task_dir" "$container_name" "$workdir"

  # Emit the audit prompt (host + container)
  local tmpdir; tmpdir=$(mktemp -d)
  compose_audit_prompt_file "$tmpdir/audit_prompt.txt"
  cp "$tmpdir/audit_prompt.txt" "$log_dir/audit_prompt.txt"
  local audit_win="$(to_windows_path "$tmpdir/audit_prompt.txt")"
  MSYS_NO_PATHCONV=1 docker cp "$audit_win" "$container_name:$workdir/_context/audit_prompt.txt"

  # Ensure Gemini CLI
  log_info "Installing Gemini CLI in container (global)"
  run_and_capture "$log_dir/gemini_install.log" docker exec -u root "$container_name" bash -lc \
    "command -v npm >/dev/null 2>&1 || { echo 'npm is required'; exit 1; }; npm install -g @google/gemini-cli@0.3.0-preview.1"

  # Run audit and capture to log
  log_info "Running audit prompt"
  run_and_capture "$log_dir/gemini_audit.log" docker exec -e GEMINI_API_KEY="$gemini_api_key" "$container_name" \
    bash -lc "cd '$workdir/_context' && gemini --debug -y --prompt \"\$(cat audit_prompt.txt)\""

  log_info "Audit complete. Logs at: $log_dir"
}


main() {
  local mode="" task_dir="" image_tag="" container_name="" workdir="" api_key="${GEMINI_API_KEY:-}" output_dir="" no_cache=""
  [ $# -ge 1 ] || { usage; exit 1; }
  mode="$1"; shift || true
  
  # Require docker for all modes
  require_cmd docker
  while [ $# -gt 0 ]; do
    case "$1" in
      --task)            task_dir="$(resolve_abs_path "$2")"; shift 2;;
      --image-tag)       image_tag="$2"; shift 2;;
      --container-name)  container_name="$2"; shift 2;;
      --workdir)         workdir="$2"; shift 2;;
      --api-key)         api_key="$2"; shift 2;;
      --output-dir)      output_dir="$2"; shift 2;;
      --no-cache)        no_cache="--no-cache"; shift 1;;
      -h|--help)         usage; exit 0;;
      *)                 log_error "Unknown arg: $1"; usage; exit 1;;
    esac
  done

  [ -n "$task_dir" ] || die "--task is required"; [ -d "$task_dir" ] || die "Task dir not found: $task_dir"
  local task_name; task_name=$(derive_task_name "$task_dir")
  if [ -z "$image_tag" ]; then image_tag=$(derive_image_tag "$task_name"); fi
  if [ -z "$container_name" ]; then container_name="$task_name"; fi
  if [ -z "$api_key" ]; then die "Gemini API Key is required (use --api-key or GEMINI_API_KEY env)"; fi

  # Determine default output directory at workspace level: <workspace>/logs/<task_name>/<timestamp>
  local script_dir; script_dir=$(cd "$(dirname "$0")" && pwd -P)
  local workspace_root; workspace_root=$(cd "$script_dir/.." && pwd -P)
  local base_logs_dir="${AUTOBUILD_LOGS_ROOT:-$workspace_root/logs}"
  
  # Generate timestamp once for both logs and container names
  # Use microsecond precision to avoid conflicts when multiple tasks start simultaneously
  local timestamp; timestamp="$(date +%Y%m%d_%H%M%S_%N | cut -c1-19)"
  if [ -z "$output_dir" ]; then output_dir="$base_logs_dir/$task_name/$timestamp"; fi
  mkdir -p "$output_dir"

  case "$mode" in
    feedback)
      local cname_fb="${container_name}-feedback-${timestamp}"
      local out_fb="$output_dir/feedback"; mkdir -p "$out_fb"
      feedback "$task_dir" "$image_tag" "$cname_fb" "$workdir" "$api_key" "$out_fb" "$no_cache"
      ;;
    verify)
      local cname_v="${container_name}-verify-${timestamp}"
      local out_v="$output_dir/verify"; mkdir -p "$out_v"
      verify   "$task_dir" "$image_tag" "$cname_v" "$workdir" "$api_key" "$out_v" "$no_cache"
      ;;
    both)
      local out_fb="$output_dir/feedback"; mkdir -p "$out_fb"
      local out_v="$output_dir/verify"; mkdir -p "$out_v"
      local cname_fb="${container_name}-feedback-${timestamp}"
      local cname_v="${container_name}-verify-${timestamp}"
      feedback "$task_dir" "$image_tag" "$cname_fb" "$workdir" "$api_key" "$out_fb" "$no_cache"
      verify   "$task_dir" "$image_tag" "$cname_v" "$workdir" "$api_key" "$out_v" "$no_cache"
      ;;
    audit)
      local base="$(basename "$task_dir")"
      local img="${image_tag:-$base:latest}"
      local cname="${container_name:-$base-audit-${timestamp}}"
      # if output_dir was pre-set, append /audit; otherwise create the default with /audit
      local out
      if [ -n "$output_dir" ]; then
        out="$output_dir/audit"
      else
        out="$base_logs_dir/$base/$timestamp/audit"
      fi
      mkdir -p "$out"
      audit "$task_dir" "$img" "$cname" "$workdir" "$api_key" "$out" "$no_cache"
      ;;
    *)
      log_error "Unknown mode: $mode (valid modes: feedback, verify, both, audit)"; usage; exit 1;;
  esac
}

main "$@"
