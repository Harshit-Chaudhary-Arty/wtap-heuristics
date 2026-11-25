# WTAP AI Harness Research and Windows Setup Context

## Research goal

The project investigates whether an AI-agent harness using local open-weight
models can tune a multi-parameter WTAP algorithm using fewer solver evaluations
than exhaustive grid search.

The intended agent loop is:

1. Select a parameter combination.
2. Run the deterministic WTAP evaluation script.
3. Read objective quality and runtime.
4. Save the parameters, results, and a short hypothesis.
5. Explore promising parameter regions and avoid repeatedly testing poor or
   duplicate combinations.
6. Stop at a fixed experiment budget or convergence criterion.

The model must not calculate objective values itself. Scripts calculate and
rank results; the model only selects the next experiment.

This cannot guarantee globally optimal parameters without exhaustive search.
The research question is whether an LLM-guided black-box search can find
competitive configurations with fewer evaluations.

Recommended baselines:

- Grid/exhaustive search on a reduced space
- Random search
- Bayesian optimization or Optuna
- LLM-guided search

Compare best objective, solver runtime, number of evaluations, model tokens,
wall-clock time, repeatability, and performance on held-out WTAP instances.

## Existing proof-of-concept

OpenCode was configured with Ollama and the very small model:

```text
qwen2.5-coder:0.5b
```

Relevant files:

- `opencode.json`
- `.opencode/commands/tune-threshold.md`
- `scripts/sweep_threshold.sh`
- `docs/threshold_comparison.md`

The command runs an externally controlled threshold sweep. It does not allow
the model to edit C sources or calculate a result.

Algorithm 4.2 is:

```text
build/4_2_circular_threshold_local_search
```

Repository rule:

- Keep cycle size fixed at 2.
- Tune only `min_improvement`.
- Run `bash scripts/sweep_threshold.sh`.

Thresholds tested:

```text
0.1, 0.08, 0.05, 0.01, 0.001
```

Stored result:

```text
Chosen min_improvement: 0.001
It has the lowest objective on 15 of 15 instances.
```

The 0.5B model is only a weak baseline. It can follow a constrained command and
copy output, but it is not reliable for multi-parameter experimental reasoning.

## Candidate models

None of these models was trained specifically on WTAP.

1. `qwen2.5-coder:0.5b`
   - Weak baseline.
   - Fast, but inadequate for complex parameter interactions.
   - Qwen2.5-Coder was trained on code, text-code grounding, synthetic data,
     mathematics, and general text.

2. `qwen3.5:9b`
   - Preferred first practical model.
   - Official name is Qwen3.5 9B, not "Qwen 9.1B."
   - Trained for multimodal understanding and post-trained with large-scale
     reinforcement learning for reasoning, coding, agents, and tool use.
   - Fast enough for repeated local experiments.

3. `qwen3:14b`
   - Balanced reasoning model.
   - Qwen3 used an approximately 36-trillion-token corpus including web/PDF
     text and synthetic code, mathematics, STEM, and reasoning data.
   - Better capacity than 9B, but slower.

4. `qwen3:30b-a3b`
   - Stronger primary model.
   - Mixture-of-experts model with 30B total and about 3B active parameters per
     token.
   - Better for interacting parameters, but all weights still occupy memory.

5. `deepseek-r1:32b`
   - Ollama name for DeepSeek-R1-Distill-Qwen-32B.
   - Reasoning reference model.
   - Fine-tuned from Qwen2.5-32B using about 600,000 reasoning and 200,000
     non-reasoning examples generated through the DeepSeek-R1 pipeline.
   - Strong reasoning, but slower, verbose, and less focused on agent tooling.

## University machine hardware

Reported GPU:

```text
Quadro RTX 6000
```

The original Quadro RTX 6000 has 24 GB VRAM, not 32 GB. Do not confuse it with
the RTX 6000 Ada, which has 48 GB.

Verify on Windows:

```powershell
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
```

Expected suitability with 4-bit quantization:

- Qwen 0.5B: easy
- Qwen3.5 9B: comfortable
- Qwen3 14B: comfortable
- Qwen3 30B-A3B: likely fits, but use a limited context first
- DeepSeek-R1 32B: tight; may partially offload to system RAM and become slow

The machine reportedly has more than 200 GB system RAM, which permits CPU
offloading but does not provide GPU-level speed.

## Context and token budget

Do not send all raw experiment logs on every call. Store complete history in
CSV/JSON and provide the model with:

- parameter definitions and constraints;
- latest trial;
- 10-20 recent detailed trials;
- a compact statistical summary of older trials;
- previously rejected or exhausted parameter ranges.

Recommended initial settings:

```text
Context window: 16384
Maximum generated tokens: 1024
Temperature: 0.0 to 0.2
```

For the 30B-A3B and 32B models on a 24 GB GPU, start with an 8192-token context.

Expected per decision:

- Input: about 3,000-7,000 tokens
- Output: about 200-800 tokens
- A tool call followed by result interpretation may process about
  7,000-15,000 tokens across two model passes

Record Ollama's `prompt_eval_count`, `eval_count`, `prompt_eval_duration`, and
`eval_duration` for every model decision.

## Native Windows setup

WSL is not required. The intended native arrangement is:

```text
Ollama for Windows -> model inference and GPU
OpenCode Desktop   -> agent interface
MSYS2 UCRT64       -> Bash, GCC, and Make
Python for Windows -> analysis and Excel export
Git for Windows    -> repository management
```

OpenCode Desktop has already been installed from the official website.

### 1. NVIDIA driver

Install a recent NVIDIA Windows driver and verify:

```powershell
nvidia-smi
```

### 2. Ollama

Install Ollama from:

```text
https://ollama.com/download/windows
```

Alternatively, in PowerShell:

```powershell
irm https://ollama.com/install.ps1 | iex
```

Use one installation method, not both.

Verify:

```powershell
ollama --version
```

Ollama serves its API at:

```text
http://127.0.0.1:11434
```

Pull and test the first model:

```powershell
ollama pull qwen3.5:9b
ollama run qwen3.5:9b
```

Exit the model prompt using `/bye`.

Verify GPU placement while the model is loaded:

```powershell
ollama ps
nvidia-smi
```

Prefer `100% GPU`.

Test the API:

```powershell
Invoke-RestMethod http://127.0.0.1:11434/v1/models
```

### 3. Git and Python

Install:

```powershell
winget install --id Git.Git -e
winget install --id Python.Python.3.13 -e
```

Verify:

```powershell
git --version
py --version
```

For Python/Excel experiments:

```powershell
cd C:\path\to\major_project
py -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install numpy pandas openpyxl
```

### 4. MSYS2 for the existing C and Bash project

Install:

```powershell
winget install --id MSYS2.MSYS2 -e
```

Open **MSYS2 UCRT64** from the Start menu. Run:

```bash
pacman -Syu
```

If asked to close the terminal, reopen MSYS2 UCRT64 and run `pacman -Syu`
again.

Install development tools:

```bash
pacman -S --needed git make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-python
```

Verify:

```bash
gcc --version
make --version
bash --version
python --version
```

A Windows project path such as:

```text
C:\Users\Student\Documents\major_project
```

is accessed in MSYS2 as:

```bash
cd /c/Users/Student/Documents/major_project
```

Build and test:

```bash
make
bash scripts/sweep_threshold.sh
```

## OpenCode/Ollama configuration

The current `opencode.json` points to Ollama's OpenAI-compatible API:

```text
http://127.0.0.1:11434/v1
```

For Qwen3.5 9B, the important configuration values are:

```json
"model": "ollama/qwen3.5:9b",
"small_model": "ollama/qwen3.5:9b"
```

The Ollama provider's model entry must use:

```json
"qwen3.5:9b": {
  "name": "Qwen3.5 9B"
}
```

Important: `.opencode/commands/tune-threshold.md` currently explicitly pins:

```yaml
model: ollama/qwen2.5-coder:0.5b
```

Changing only `opencode.json` will not change that command's model. Change the
command-level value to:

```yaml
model: ollama/qwen3.5:9b
```

or remove the command-level model field so the global model is used.

## Next implementation step

After the Windows environment and Qwen3.5 9B are verified:

1. Preserve the existing single-parameter sweep as a baseline.
2. Define a multi-parameter search space and valid ranges.
3. Build one deterministic evaluator that accepts one configuration and emits
   structured JSON.
4. Store every trial in an append-only CSV/JSON experiment log.
5. Have the model return only structured proposals and short hypotheses.
6. Reject invalid and duplicate proposals programmatically.
7. Give every search method the same solver-evaluation budget.
8. Repeat runs with fixed seeds where applicable.
9. Evaluate selected parameters on held-out instances.

The model should never edit the solver during a tuning run. It should only
select parameter values and interpret externally calculated results.
