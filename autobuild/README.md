## Autobuild Verification Orchestrator 2.0

This toolkit automates verification workflows using Docker and the Gemini CLI.  
It provides three modes:

1. **feedback** — Builds the environment, runs Gemini Prompt 1, verifies the result, and optionally runs Prompt 2.  
2. **verify** — Reproduces the customer’s exact command sequence using Gemini CLI.  
3. **audit** — Analyzes the `verify.sh`, `prompt.txt`, and `Dockerfile` for clarity, correctness, and alignment (no implementation).

### Requirements
- Docker installed and running
- Internet access for `npm`/`npx` inside the container
- A Google Gemini API key

Set your key (either export it in your shell or pass via `--api-key`):

```bash
export GEMINI_API_KEY=your_api_key_here
```

### Task Directory Structure
```
<task_id>/
  env/
    Dockerfile
  verify/
    verify.sh (required)
    verification_command (optional)
  prompt/
    prompt.txt (preferred)
  # OR alternatively, prompt.txt or prompt at the task root
```

### Usage

- Feedback step (interactive development-style run inside a keepalive container):
```bash
bash scripts/autobuild.sh feedback \
  --task /absolute/path/to/<task_id> \
  --api-key "$GEMINI_API_KEY"
```

- Verify step (reproduces the customer's command sequence with `npx`):
```bash
bash scripts/autobuild.sh verify \
  --task /absolute/path/to/<task_id> \
  --api-key "$GEMINI_API_KEY"
```

- Audit step (check for common errors in the verify.sh and prompt, giving Gemini the whole environment context):
```bash
bash scripts/autobuild.sh audit \
  --task /absolute/path/to/<task_id> \
  --api-key "$GEMINI_API_KEY"
```

Optional flags for both commands:
- `--image-tag <tag>`: custom image tag (default: `autobuild-<task_name>:latest`)
- `--container-name <name>`: container name (default: `<task_name>-<timestamp>`)
- `--workdir <path>`: override workdir (default: parsed from `env/Dockerfile` `WORKDIR`, else `/workspace`)

### What the scripts do

- Feedback flow:
  - Builds image from `env/`
  - Runs container with a keepalive process and sets working directory
  - Copies `verify/` and `prompt.txt` (or the first file in `prompt/`) to the workdir
  - Installs `@google/gemini-cli@0.3.0-preview.1` globally inside the container
  - Runs Prompt 1 via `gemini --debug -y --prompt "..."`
  - Runs the verification command (`verify/verification_command` if present, else `bash verify.sh`)
  - If verification succeeds, runs Prompt 2

- Verify flow (customer sequence):
  - `docker build -t <image_tag> env/`
  - `docker run -v /var/run/docker.sock:/var/run/docker.sock --name <container_name> -d -i <image_tag>`
  - `docker exec -i -e GEMINI_API_KEY=<key> <container_name> npx --yes @google/gemini-cli@0.3.0-preview.1 --yolo --debug --prompt "<Prompt 1 content>"`
  - `docker inspect <container_id>`
  - `docker cp verify/. <container_id>:<workdir>` and copy `prompt.txt`
  - `docker exec -u root <container_id> bash -c "cd <workdir> && <verification_command>"`

- Audit flow (static verifier analysis):
  - `docker build -t <image_tag> env/`
  - `docker run -v /var/run/docker.sock:/var/run/docker.sock --name <container_name> -d -i <image_tag> tail -f /dev/null`
  - Determine `WORKDIR` (from Dockerfile), then `docker exec <container_name> bash -lc "mkdir -p $WORKDIR/_context"`
  - `docker cp prompt(.txt) <container_name>:$WORKDIR/_context/prompt.txt`
  - `docker cp verify/verify.sh <container_name>:$WORKDIR/_context/verify.sh`
  - `docker cp env/Dockerfile <container_name>:$WORKDIR/_context/Dockerfile`
  - Write or copy `audit_prompt.txt` into `$WORKDIR/_context/`
  - `docker exec -u root <container_name> bash -lc "npm install -g @google/gemini-cli@0.3.0-preview.1"`
  - `docker exec -e GEMINI_API_KEY=<key> <container_name> bash -lc "cd $WORKDIR/_context && gemini --debug -y --prompt \"$(cat audit_prompt.txt)\""`
  - Save output to `logs/<task_id>/<timestamp>/audit/gemini_audit.log`

### Notes
- The verify flow intentionally follows the customer's sequence. If the container exits immediately after `docker run -d -i`, subsequent steps will fail, mirroring the customer's environment. Ensure your image keeps the container running or sets a suitable default `CMD`.
- `verification_command` can be any shell command(s). If absent, the script runs `bash verify.sh`.

### Cleanup
```bash
docker rm -f <container_name>
```
