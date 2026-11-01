CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -O2
THREAD_FLAGS := -pthread
CRYPTO_LIBS := -lcrypto
CORE_SRC := src/account.c src/storage.c src/locking.c src/journal.c src/recovery.c src/banking_service.c src/domain.c
AUTH_SRC := src/auth.c src/roles.c src/customer_module.c src/employee_module.c src/manager_module.c src/administrator_module.c
.PHONY: all clean test
all: banking bank_server bank_client
banking: src/main.c $(CORE_SRC)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $^ -o $@
bank_server: src/server.c $(CORE_SRC) $(AUTH_SRC)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $^ $(CRYPTO_LIBS) -o $@
bank_client: src/client.c
	$(CC) $(CFLAGS) $^ -o $@
test:
	./tests/smoke_test.sh
clean:
	rm -f banking bank_server bank_client tests/domain_test
