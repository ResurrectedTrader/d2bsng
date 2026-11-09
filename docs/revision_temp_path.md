# Battle.net revision directory

Under `-multi`, the existing GetTempPathA call-site hook uses a PID-based directory, independent of profile/window names. Concurrent duplicate profiles cannot collide after sanitization or truncation. The helper returns zero on every failure and never falls back to a shared directory. The caller reserves filename space and interprets any nonzero return as a usable directory length.

`std::filesystem` handles temporary-directory lookup, path composition, creation and status checks. `std::format` builds the process-specific name; STL copying preserves the terminating NUL. Existing files and symbolic links are rejected. Win32 remains at the hook's ABI and process-ID boundary. Extraction/deletion of the revision DLL remain owned by the game; empty per-PID directories may remain after exit.
