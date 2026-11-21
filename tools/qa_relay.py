#!/usr/bin/env python3
"""QA relay for network co-op tests over the real internet (src/net.c, MW2COOP_QARELAY): two test copies of the game on one PC send their packets to this relay
on a server, and it sends each to the other copy, so every packet crosses the internet twice. Run it on the server:

  python3 qa_relay.py <udp port> <allowed source ip> [seconds] [bind address]

Give [bind address] on a server with several addresses: the address the copies send to. Bound to 0.0.0.0, the answers leave from the server's main
address, and a home router's NAT drops them (they come from another address than the one the copy sent to).

Only packets from <allowed source ip> (the PC's public address) are taken. A copy registers each game socket every second ("MWRH" + its game port, 2 bytes);
a game packet "MWR1" + destination ip (4) + destination port (2) + data goes to the copy that registered that port, as "MWR1" + the sender's game ip and port + data.
Standard library only; it stops after [seconds] (default 3 hours) and prints a line of counts every 10 s."""
import socket
import sys
import time


def main():
    port, allowed = int(sys.argv[1]), sys.argv[2]
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 3 * 3600
    bind = sys.argv[4] if len(sys.argv) > 4 else '0.0.0.0'
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((bind, port))
    s.settimeout(1.0)
    by_port, by_ep = {}, {}  # game port -> the copy's endpoint as seen here; endpoint -> game port
    start = last = time.time()
    fwd = drop = hello = 0
    print('relay on %s udp %d for %s, %d s' % (bind, port, allowed, limit), flush=True)
    while time.time() - start < limit:
        try:
            data, ep = s.recvfrom(2048)
        except socket.timeout:
            data = None
        now = time.time()
        if now - last >= 10:
            print('%s forwarded %d dropped %d hellos %d ports %s' % (time.strftime('%H:%M:%S'), fwd, drop, hello, sorted(by_port)), flush=True)
            fwd = drop = hello = 0
            last = now
        if data is None:
            continue
        if ep[0] != allowed:
            drop += 1
            continue
        if data[:4] == b'MWRH' and len(data) >= 6:
            gp = int.from_bytes(data[4:6], 'big')
            by_port[gp] = ep
            by_ep[ep] = gp
            hello += 1
            s.sendto(b'MWRA' + data[4:6], ep)  # the copy counts these: the way back from the relay to its socket works
            continue
        if data[:4] == b'MWR1' and len(data) >= 10:
            dst = by_port.get(int.from_bytes(data[8:10], 'big'))
            src = by_ep.get(ep)
            if dst is None or src is None:
                drop += 1
                continue
            s.sendto(b'MWR1' + data[4:8] + src.to_bytes(2, 'big') + data[10:], dst)
            fwd += 1
            continue
        drop += 1
    print('relay stopped', flush=True)


if __name__ == '__main__':
    main()
