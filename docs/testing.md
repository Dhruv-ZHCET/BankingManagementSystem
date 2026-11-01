# Test plan

Run `make clean && make && make test` from the project root.

The smoke test starts a local server against a temporary copy of the project data and exercises authentication, balance, transfer, loan application, feedback submission, role denial, and clean logout. It is an integration smoke test, not a formal crash-consistency proof.

Additional manual tests recommended for the course evaluation:

1. Launch two customer sessions and attempt simultaneous withdrawals/transfers; verify no lost update and no negative balance.
2. Attempt customer access to another customer's account; expect `ERR PERMISSION_DENIED` or a generic failure.
3. Submit invalid amounts (zero, negative, overflow) and same-account transfers.
4. Apply for a loan as customer; assign it as manager; approve/reject it as employee.
5. Close a zero-balance account; verify non-zero accounts cannot be closed.
6. Kill the server at controlled points in a disposable copy and test journal recovery.
7. Test full disk, permission errors, malformed data files, and a second server process.

Never run destructive tests against data you need to preserve.
