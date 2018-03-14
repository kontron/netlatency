#!/usr/bin/env python

import sys
import json
import numpy
import dateutil.parser

def print_help():
    print "INPUTFILE|- [OUTPUTFILE]"

def calc_latency(rx_packet):
    result = {}
    tx_user = numpy.datetime64(rx_packet['tx-user-timestamp'])
    tx_hw = None # not available yet
    rx_hw = numpy.datetime64(rx_packet['rx-hw-timestamp'])
    rx_user = numpy.datetime64(rx_packet['rx-user-timestamp'])
    diff_user_hw = rx_hw - tx_user
    diff_user_user = rx_user - tx_user
    result['type'] = 'latency'
    result['object'] = {
        'latency-user-hw': int(diff_user_hw),
        'latency-user-user': int(diff_user_user)
    }
    print json.dumps(result)


def main():

    if len(sys.argv) < 2:
        print_help()
        return

    if sys.argv[1] == '-':
        f = sys.stdin
    else:
        f = open(sys.argv[1])

    try:
        for line in f:
            try:
                j = json.loads(line)
                if j['type'] == 'rx-error':
                    pass
                elif j['type'] == 'rx-packet':
                    calc_latency(j['object'])
                    pass
            except ValueError:
                pass
    except KeyboardInterrupt as e:
        pass


if __name__ == '__main__':
    main()
