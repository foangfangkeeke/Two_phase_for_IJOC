# Repository Agent Instructions

- Do not add multiple fallback paths for conditions that should be hard
  failures. For example, do not silently continue when a required file is
  missing; allow the program to fail with the real error.
- When the user issues a command, first restate and understand the request,
  then provide a plan, and only then start the operation.
- Before modifying code, run `git status --short` and report whether the
  working tree is clean. If it is dirty, obtain explicit user consent for that
  specific modification request before editing. Consent applies only to the
  current request and must be obtained again for every later code-modification
  request. Preserve all unrelated user changes.
- Do not use `run.bat` as the default build path. Only run it when the user
  explicitly asks, and then invoke it through `cmd.exe`, for example:
  `cmd.exe /d /s /c "call .\run.bat <nul"`.
- The validated direct Release build command for this repository is:

  ```text
  cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat`" && cd /d C:\faq_work\Papers\Two_phase_wireless_charging_and_integer_lshape && cmake.exe -S . -B out/build/x64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe && cmake.exe --build out/build/x64-ninja --config Release"
  ```
