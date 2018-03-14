#!/usr/bin/env python

import argparse
import json
import matplotlib.pyplot as plt
import numpy as np
import sys


def plot(data):
    max_val = data['max']
    min_val = data['min']
    counts = data['count']
    outliers = data['outliers']
    y = data['histogram']
    x = range(0, len(data['histogram']))

    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.grid(True)
    ax.set_xlabel('latency ($\mu$s)')
    ax.set_ylabel('count')
    ax.bar(x, y)
    plt.yscale('log')

    plt.title('counts: %s, min: %s $\mu$s, max: %s $\mu$s outliers: %s' \
            % (counts, min_val, max_val, outliers))

#    if output is None:
    plt.show()
#    else:
#        plt.savefig(output)

def main(args=None):
    output = None

    parser = argparse.ArgumentParser(
        description='reportgen')
    parser.add_argument('infile', nargs='?', type=argparse.FileType('r'),
                       default=sys.stdin)

    args = parser.parse_args(args)

    try:
        for line in args.infile:
            try:
                j = json.loads(line)

                if j['type'] == 'histogram':
                    plot(j['object'])
                    break
            except ValueError:
                pass

        print "No valid histogram data found"
        sys.exit(1)

    except KeyboardInterrupt as e:
        sys.exit(1)

if __name__ == '__main__':
    main()
