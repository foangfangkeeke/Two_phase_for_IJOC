Don't have too many fallbacks. For instance, don't check if the file exists. If there is a real error, just let the program crash directly.

When a user issues a command, first describe and understand it, then provide a plan, and then start the operation.

The user authorizes routine git operations in this repository, including `git add`, `git commit`, `git commit --amend`, `git checkout`, `git revert`, and history-cleanup commands the user explicitly requests.
If a git command that writes to `.git` is blocked by sandbox permissions, retry the same command with escalation immediately and include a concise justification; do not stop for a separate explanatory turn.
For escalated git commands, use a short default justification such as "需要写入 Git 元数据以完成用户明确请求的版本控制操作。"
This project instruction does not disable the platform sandbox, so escalation may still be required by the runtime.

For manuscript writing or revision tasks, read `manuscript/WRITING_RULES.md` before editing manuscript files.
Follow those rules together with any explicitly invoked writing skill.

For projects that contain `run.bat`, do not use `run.bat` as the default build path from Codex.
Use the recorded, validated direct Ninja build command for that project instead.
Only when the user explicitly asks to execute `run.bat` itself, run it through `cmd.exe`, for example: `cmd.exe /d /s /c "call .\run.bat <nul"`.
Do not invoke `run.bat` directly through PowerShell.

For the wireless charging C++ project at `C:\faq_work\Papers\Two_phase_wireless_charging_and_integer_lshape`, the validated direct build command is:

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat`" && cd /d C:\faq_work\Papers\Two_phase_wireless_charging_and_integer_lshape && cmake.exe -S . -B out/build/x64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe && cmake.exe --build out/build/x64-ninja --config Release"
```
