#!/usr/bin/env python3
"""Wi-Fi workbench: send "wl <command>" to the phone's command port (udp/5141 over the USB network) and print the answer.

    tools/wl.py scan            # a new scan, then:
    tools/wl.py nets            # the access points found
    tools/wl.py raw 3001 <hex>  # one WMI command (id in hex, body as hex bytes)
    tools/wl.py pdev 37 1       # WMI pdev set param
    tools/wl.py chan | ev | last

The phone must run a kernel with patch 0188 and the boot-arg wifidbg=1; the USB network must be up (tools/sweet-usbnet-macos.sh).
Commands are queued on the phone: the answer to most of them is read with "last" a few seconds later (this tool does that for you).
"""
import socket
import sys
import time

PHONE = ('192.168.7.2', 5141)


def ask(text, wait=2.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(wait)
    s.sendto(text.encode(), PHONE)
    try:
        return s.recvfrom(2048)[0].decode(errors='replace')
    except socket.timeout:
        return None


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = ' '.join(sys.argv[1:])
    r = ask('wl ' + cmd)
    if r is None:
        sys.exit('no answer: is the USB network up and the phone running with wifidbg=1?')
    print(r, end='')
    if r.startswith('queued'):
        wait = 18 if cmd.startswith('scan') else 4
        for _ in range(wait * 2):
            time.sleep(0.5)
            last = ask('wl last')
            if last and last != 'no command yet\n' and not last.startswith('queued'):
                pass
        print(ask('wl last') or '', end='')


if __name__ == '__main__':
    main()
