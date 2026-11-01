# Command reference

All commands are one newline-terminated text line. The first line on each connection is `LOGIN username password`. The server replies with one line per command. `OK` indicates a successful operation; `ERR` indicates rejection/failure. Amounts are integer paise.

| Command | Allowed role | Meaning |
|---|---|---|
| `BALANCE account` | Customer (own account), Employee | Read balance |
| `DEPOSIT account paise` | Customer (own account) | Deposit positive amount |
| `WITHDRAW account paise` | Customer (own account) | Withdraw if sufficient balance |
| `TRANSFER from to paise` | Customer (own sender account) | Transfer atomically in account snapshot |
| `HISTORY account` | Customer (own account), Employee | Journal-based transaction history |
| `LOAN_APPLY account paise purpose...` | Customer (own account) | Submit loan application |
| `LOANS` | Customer, Employee, Manager | Customer sees own applications; staff see all |
| `LOAN_ASSIGN loan_id employee_user_id` | Manager | Assign pending loan to an active employee |
| `LOAN_PROCESS loan_id APPROVE|REJECT` | Employee | Process a pending or assigned application |
| `FEEDBACK account message...` | Customer (own account) | Submit feedback |
| `FEEDBACK_LIST` | Manager | List feedback |
| `FEEDBACK_REVIEW feedback_id` | Manager | Mark feedback reviewed |
| `CUSTOMER_ADD username password name 0` | Employee | Create zero-balance account and customer login |
| `MODIFY_ACCOUNT account name` | Employee | Change account display name |
| `CLOSE_ACCOUNT account` | Employee | Deactivate only if balance is zero |
| `ACTIVATE_ACCOUNT account` / `DEACTIVATE_ACCOUNT account` | Manager | Change account active flag |
| `PASSWORD new_password` | All roles | Change own password |
| `USER_ADD username password role account_or_0` | Administrator | Create login |
| `USER_ROLE username role` | Administrator | Change a login's role |
| `USER_ACTIVE username 0|1` | Administrator | Disable/enable future logins |
| `USER_LIST`, `ACCOUNTS` | Administrator | List users/accounts |
| `LOGOUT`, `EXIT` | All roles | End current session |
