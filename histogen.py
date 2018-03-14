#!/usr/bin/env python

import argparse
import copy
import json
import matplotlib.pyplot as plt
import sys
import threading
import time


def update_histogram(json, output):
    histogram = output['object']
    histogram['count'] += 1

    latency_usec = json['latency-user-hw'] / 1000
    if latency_usec > histogram['max'] or histogram['max'] == 0:
        histogram['max'] = latency_usec
    if latency_usec < histogram['min'] or histogram['min'] == 0:
        histogram['min'] = latency_usec

    if latency_usec < 0:
        histogram['time_error'] += 1
    elif latency_usec < len(histogram['histogram']):
        histogram['histogram'][latency_usec] += 1
    else:
        histogram['outliers'] += 1


def main(args=None):

    parser = argparse.ArgumentParser(
        description='histogen')

    parser.add_argument('-c', '--count', type=int, dest='count', default=0)
#    parser.add_argument('-i', '--interval', type=int, dest='interval')

    parser.add_argument('infile', nargs='?', type=argparse.FileType('r'),
                       default=sys.stdin)
    parser.add_argument('outfile', nargs='?', type=argparse.FileType('w'),
                       default=sys.stdout)

    args = parser.parse_args(args)

    output = None
    histogram_empty = {
        'type': 'histogram',
        'object': {
            'count': 0,
            'min': 0,
            'max': 0,
            'outliers': 0,
            'time_error': 0,
            'histogram': [0] * 100,
        }
    }

    histogram_out = copy.deepcopy(histogram_empty)

    count = 0
    try:
        for line in args.infile:
            try:
                j = json.loads(line)
                if j['type'] == 'latency':
                    update_histogram(j['object'], histogram_out)

                    if args.count != 0:
                        count += 1

                        if count == args.count:
                            print json.dumps(histogram_out)
                            count = 0
                            histogram_out = copy.deepcopy(histogram_empty)

            except ValueError:
                pass

    except KeyboardInterrupt as e:
        pass

    if output == None:
        print json.dumps(histogram_out)


if __name__ == '__main__':
    main()
