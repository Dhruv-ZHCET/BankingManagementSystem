# CS-513 System Software Mini Project Report

## Title
Design and Development of a File-Based Banking Management System in C

## Objective
Build a local client-server banking simulation to demonstrate POSIX sockets, multithreading, file management, advisory locking, password hashing, role-based access, financial transaction journaling, and recovery.

## Implemented modules

1. **Customer** (`src/customer_module.c`): balance, deposit, withdrawal, transfer, loan application/status, password change, feedback, transaction history. Customer commands are restricted to the account linked to the authenticated user.
2. **Bank employee** (`src/employee_module.c`): create customer account/login, modify account display name, close zero-balance account, inspect passbook/journal history, process loan applications.
3. **Manager** (`src/manager_module.c`): assign loan applications to employees, activate/deactivate accounts, review and mark customer feedback.
4. **Administrator** (`src/administrator_module.c`): add users, manage roles and active status, list users and accounts, and perform account maintenance.
5. **Authentication** (`src/auth.c`): salted PBKDF2-HMAC-SHA-256 password hashes, one active session per username within this server process, password changes, user management.
6. **Financial services** (`src/banking_service.c`): balance/deposit/withdraw/transfer with input validation, overflow checks, sufficient-funds checks, journal writes and locking.
7. **Storage and recovery** (`src/storage.c`, `src/locking.c`, `src/journal.c`, `src/recovery.c`): text-file persistence, stable lock file, atomic account-file replacement, journal replay for unfinished financial transactions.
8. **Domain workflows** (`src/domain.c`): account administration, loan workflow, feedback persistence, transaction history query.

## Synchronization design

Financial operations use a `pthread_mutex_t` to serialize threads inside one process and an advisory `fcntl()` lock on `data/accounts.lock` to coordinate cooperating processes. The lock file is deliberately separate from `accounts.txt`: the account data file is replaced atomically, whereas the lock file remains at a stable path/inode. Before balance changes, the service records a PREPARED journal entry and synchronizes it. It then writes the updated account snapshot to a temporary file, synchronizes the file, and atomically renames it over `accounts.txt`. A COMMITTED journal marker follows. Startup and balance-changing operations run recovery while holding the shared lock.

## Data representation

All monetary values are integer paise, avoiding floating-point rounding. Account IDs and user IDs are integers. Loan and feedback IDs are monotonically derived from the largest ID currently present in their data files.

## Build and validation

- `make clean && make` compiles the CLI, server, and client with GCC and OpenSSL.
- `make test` runs an integration smoke test in a temporary data directory.
- The smoke test covers login, balance inquiry, transfer, customer account ownership, loan application/assignment/approval, feedback/review, employee-created customer, and administrator listings.

## Limitations and future improvements

This implementation is a teaching prototype, not a production banking application. Cross-file changes such as creating an account and a user are not a single atomic transaction. Loans and feedback do not have WAL-based crash recovery. Authenticated sessions are tracked only in server memory. The line-based protocol does not use TLS; login throttling and robust password policy are not implemented. Parent-directory `fsync`, exhaustive crash-point testing, and formal verification of the recovery algorithm remain future work. Therefore the project demonstrates selected ACID-related techniques but should not be claimed to provide formal ACID guarantees under all failures.
