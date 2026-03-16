# Copilot Instructions

## Project Guidelines
- Prefers to overwrite remote changes when pushing (does not need remote changes).

## Code Style
- When providing code change suggestions, use the file’s exact casing as shown in Visual Studio (avoid all-caps filenames because it confuses VS).
- When emitting code blocks for this repo, always use the non-all-caps Visual Studio path casing (e.g., `NeuralAmpModelerCore/NAMLibraryBrowserWindow.cpp`), even if the active tab shows an all-caps variant, because the all-caps path does not apply cleanly in VS.
- Always use `cpp` as the code fence language when providing code blocks for this repo (even for Objective-C++ sections) because other language tags like `objective-c++` confuse VS when applying suggestions.