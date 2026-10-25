"""QA for the start-packet check (net.c from_partner): while a QA pair is in a level, send the client's game
port a "mw2coop_start" packet from two addresses and see which one the client acts on.
  - from 127.0.0.1 (not the host's address as the client knows it): must be ignored
  - from the PC's LAN address (the host's address in a QA pair): must be applied
Usage: python -I spoof_test.py <client game dir> <lan ip> <client port>
Needs a pair running (tools/qa_pair.sh) with the client in a level; prints the verdict from the client's
mw2coop.log."""
import os
import socket
import sys
import time

game_dir, lan_ip, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
log = os.path.join(game_dir, 'mw2coop', 'mw2coop.log')


def lines():
    with open(log, encoding='latin-1') as f:
        return f.read().split('\n')


def send(source, text):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((source, 0))
    s.sendto(b'\xff\xff\xff\xff' + text.encode(), (source if source != '127.0.0.1' else '127.0.0.1', port))
    s.close()


before = len(lines())
send('127.0.0.1', 'mw2coop_start start=spoofed1')
time.sleep(1.5)
after = lines()[before:]
spoof_applied = any("progress: host start 'spoofed1'" in l for l in after)
spoof_ignored = any('not the partner' in l for l in after)
print('from 127.0.0.1: applied' if spoof_applied else 'from 127.0.0.1: not applied',
      '(ignored line logged)' if spoof_ignored else '(no ignored line)')

before = len(lines())
send(lan_ip, 'mw2coop_start start=partner1')
time.sleep(1.5)
after = lines()[before:]
partner_applied = any("progress: host start 'partner1'" in l for l in after)
print('from the LAN address: applied' if partner_applied else 'from the LAN address: NOT applied')
ok = (not spoof_applied) and partner_applied
print('PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
