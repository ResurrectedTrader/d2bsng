# Battle.net revision directory

Under `-multi`, the existing GetTempPathA call-site hook now uses a PID-based directory, independent of profile/window names. Concurrent duplicate profiles cannot collide after sanitization or truncation. The helper returns zero on every failure and never falls back to a shared directory. The caller reserves filename space and interprets any nonzero return as a usable directory length.

GetTempPathA required-size results are rejected before the returned length is used; directory creation failures, ordinary-file collisions and existing reparse points fail the check. A small destination is cleared and fails before creating a directory. Extraction and deletion of the revision DLL remain owned by the game. Empty per-PID directories may remain after exit.

The existing call-site patch is retained because replacing GetTempPathA process-wide would redirect unrelated users. Related upstream: blizzhackers/d2bs `dfbe33552ce568be575b813e900708af5a051c61`.
