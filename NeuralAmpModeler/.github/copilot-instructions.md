# Copilot Instructions

## Project Guidelines
- Prefers to overwrite remote changes when pushing (does not need remote changes).
- The solution file `NeuralAmpModeler.sln` is located at `NeuralAmpModeler/NeuralAmpModeler.sln` under the repo root.

## Code Style
- When providing code change suggestions, use the file’s exact casing as shown in Visual Studio (avoid all-caps filenames because it confuses VS).
- When emitting code blocks for this repo, always use the non-all-caps Visual Studio path casing (e.g., `NeuralAmpModeler/NAMLibraryBrowserWindow.cpp`), as `NAMLibraryBrowserWindow.cpp` and related files are located in the `NeuralAmpModeler` folder. Update future file paths accordingly.
- Always use `cpp` as the code fence language when providing code blocks for this repo (even for Objective-C++ sections) because other language tags like `objective-c++` confuse VS when applying suggestions.

## UI State Management
- Persist `NAMLibraryBrowserWindow` state per plugin instance in `NeuralAmpModeler`: store search query, selected tag, and expanded state in memory only (not settings file) to avoid conflicts when multiple VST3 instances are running. Restore this state when reopening the browser to avoid cross-instance sharing. Prefer per-instance in-memory state (search query, selected tag, expanded state) — do not use a static/global session state shared across plugin instances.