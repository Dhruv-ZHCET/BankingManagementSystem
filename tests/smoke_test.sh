#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d)
PID=""
cleanup() { if [ -n "$PID" ]; then kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; fi; rm -rf "$TMP"; }
trap cleanup EXIT INT TERM
cp -R "$ROOT/data" "$TMP/data"
(cd "$TMP" && BANK_PORT=18080 exec "$ROOT/bank_server") >"$TMP/server.log" 2>&1 &
PID=$!
sleep 1
python3 - "$TMP" <<'PY'
import socket,sys,time

def connect(user,pw):
    s=socket.create_connection(('127.0.0.1',18080),timeout=3)
    f=s.makefile('rwb',buffering=0)
    f.write(f'LOGIN {user} {pw}\n'.encode())
    r=f.readline().decode().strip()
    assert r.startswith('OK LOGIN'), (user,r)
    return s,f

def cmd(f,c):
    f.write((c+'\n').encode()); r=f.readline().decode().strip(); return r
s,f=connect('customer1','cust123')
r=cmd(f,'BALANCE 1001'); assert r.startswith('OK BALANCE'),r
r=cmd(f,'TRANSFER 1001 1002 100'); assert r.startswith('OK TRANSFER'),r
r=cmd(f,'BALANCE 1002'); assert r=='ERR PERMISSION_DENIED',r
r=cmd(f,'LOAN_APPLY 1001 100000 home renovation'); assert r.startswith('OK LOAN_APPLIED'),r
loan_id=int(r.split('=')[1])
r=cmd(f,'FEEDBACK 1001 App is easy to use'); assert r.startswith('OK FEEDBACK_SUBMITTED'),r
feedback_id=int(r.split('=')[1])
r=cmd(f,'HISTORY 1001'); assert r.startswith('OK HISTORY'),r
assert cmd(f,'LOGOUT')=='OK LOGOUT';s.close()
s,f=connect('manager1','manager123')
r=cmd(f,f'LOAN_ASSIGN {loan_id} 2001'); assert r=='OK LOAN_ASSIGNED',r
r=cmd(f,'FEEDBACK_LIST'); assert 'App is easy to use' in r,r
r=cmd(f,f'FEEDBACK_REVIEW {feedback_id}'); assert r=='OK FEEDBACK_REVIEWED',r
assert cmd(f,'LOGOUT')=='OK LOGOUT';s.close()
s,f=connect('employee1','emp123')
r=cmd(f,f'LOAN_PROCESS {loan_id} APPROVE'); assert r=='OK LOAN_PROCESSED',r
r=cmd(f,'CUSTOMER_ADD testcustomer testpass TestCustomer 0'); assert r.startswith('OK CUSTOMER_CREATED'),r
assert cmd(f,'LOGOUT')=='OK LOGOUT';s.close()
s,f=connect('admin','admin123')
r=cmd(f,'USER_LIST'); assert 'testcustomer' in r,r
r=cmd(f,'ACCOUNTS'); assert 'TestCustomer' in r,r
assert cmd(f,'LOGOUT')=='OK LOGOUT';s.close()
print('Smoke test passed: auth, balance, transfer, ownership, loan lifecycle, feedback, customer creation, admin listing.')
PY
