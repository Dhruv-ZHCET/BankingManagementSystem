# Banking Management System — CS-513 System Software Mini Project

A C/POSIX file-backed banking system demonstrating TCP socket programming, a thread-per-client server, role-based authorization, password hashing, `pthread_mutex`, advisory `fcntl()` file locks, atomic file replacement, a write-ahead transaction journal, and startup recovery.

> **Coursework prototype only.** This is not suitable for real money or production deployment. See [docs/architecture.md](docs/architecture.md) for the architecture, role matrix, file formats, and known consistency/security limitations.

## 1. Requirements

Ubuntu/Debian packages:

```bash
sudo apt update
sudo apt install build-essential libssl-dev python3
```

## 2. Build and test

Run from the project root:

```bash
make clean
make
make test
```

`make test` runs a smoke integration test in a temporary copy of the data directory, so the demo account balances in your working directory are not mutated by the test.

## 3. Start the server and client

Terminal 1:

```bash
./bank_server
```

Terminal 2:

```bash
./bank_client
```

Server defaults to `127.0.0.1:8080` and loopback only. To select another port, set `BANK_PORT` for both processes, for example `BANK_PORT=18080 ./bank_server` and `BANK_PORT=18080 ./bank_client`.

The client hides password input when attached to a terminal. The demo protocol still sends credentials over an unencrypted TCP connection, so keep it on loopback.

## 4. Demo credentials

Created on first startup only if `data/users.txt` does not exist:

| Username | Password | Role | Linked account |
|---|---|---|---:|
| `customer1` | `cust123` | Customer | 1001 |
| `employee1` | `emp123` | Bank employee | — |
| `manager1` | `manager123` | Manager | — |
| `admin` | `admin123` | Administrator | — |

The password file stores a random salt and PBKDF2-HMAC-SHA-256 password hash (not the plaintext password). These are public demo credentials; change them before sharing the project.

## 5. Commands

Money is represented as integer **paise** (`100 paise = ₹1`). Use no spaces in usernames/passwords/account names; loan purpose and feedback message can contain spaces. Commands depend on your logged-in role; the server enforces the role and customer account ownership.

### Customer

```text
BALANCE <account>
DEPOSIT <account> <amount_paise>
WITHDRAW <account> <amount_paise>
TRANSFER <from_account> <to_account> <amount_paise>
LOAN_APPLY <account> <amount_paise> <purpose text>
LOANS
HISTORY <account>
FEEDBACK <account> <message text>
PASSWORD <new-password>
LOGOUT
EXIT
```

### Bank employee

```text
CUSTOMER_ADD <username> <password> <name-no-spaces> 0
MODIFY_ACCOUNT <account> <new-name-no-spaces>
CLOSE_ACCOUNT <account>
LOANS
LOAN_PROCESS <loan_id> APPROVE|REJECT
HISTORY <account>
BALANCE <account>
PASSWORD <new-password>
```

`CUSTOMER_ADD` opens a zero-balance account and creates a customer login. A zero balance is required to keep failure rollback straightforward in this file-backed coursework design. An account can only be closed when its balance is zero.

### Manager

```text
LOANS
LOAN_ASSIGN <loan_id> <employee_user_id>
ACTIVATE_ACCOUNT <account>
DEACTIVATE_ACCOUNT <account>
FEEDBACK_LIST
FEEDBACK_REVIEW <feedback_id>
PASSWORD <new-password>
```

Use employee user ID `2001` for the seeded `employee1` account.

### Administrator

```text
USER_ADD <username> <password> CUSTOMER|EMPLOYEE|MANAGER|ADMIN <account_number_or_0>
USER_ROLE <username> CUSTOMER|EMPLOYEE|MANAGER|ADMIN
USER_ACTIVE <username> 0|1
USER_LIST
ACCOUNTS
PASSWORD <new-password>
```

A user role change affects future logins; an already-authenticated session may keep its existing role until it disconnects. `USER_ACTIVE 0` blocks new logins, but does not forcibly terminate a current connection.

## 6. Source layout

- `src/server.c`, `src/client.c`: TCP server and interactive client.
- `src/auth.c`, `src/roles.c`: login, password hashes, session reservation, role dispatch.
- `src/customer_module.c`, `src/employee_module.c`, `src/manager_module.c`, `src/administrator_module.c`: separate role command modules.
- `src/banking_service.c`: balance, deposit, withdrawal, transfer.
- `src/storage.c`, `src/locking.c`, `src/journal.c`, `src/recovery.c`: file persistence, locks, journal, recovery.
- `src/domain.c`: account administration, loan workflow, feedback, transaction history.
- `include/`: module interfaces.
- `data/`: demo data. Back it up before manual experiments.
- `docs/architecture.md`: architecture and blueprint diagrams.
- `docs/project_report.md`: project summary, module design, synchronization, test coverage, limitations.
- `docs/commands.md`: role-based command reference.
- `docs/testing.md`: smoke test and additional evaluation tests.

## 7. Correctness and limitations

- Balance updates validate amounts, active accounts, sufficient funds, and overflow.
- Financial operations serialize threads with a process mutex and cooperating processes with `fcntl()` on the stable `data/accounts.lock` file.
- A journal intent is appended and synchronized before balance persistence. Account snapshots are written to a temporary file, synchronized, then atomically renamed.
- The recovery code attempts to complete pending journal records before new balance operations. The smoke test does not prove crash safety under every failure point.
- Account plus user creation spans two files and is not a formal cross-file transaction. Loan/feedback files have locking but no WAL recovery. User session tracking is in-process only. Directory `fsync`, TLS, login throttling, and exhaustive fault injection are not implemented.
- Do not describe the prototype as formally ACID-compliant or production-secure. The project demonstrates relevant synchronization and recovery techniques while documenting remaining boundaries.
