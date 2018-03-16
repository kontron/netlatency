#!/usr/bin/env python
# Copyright (c) 2018, Kontron Europe GmbH
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import print_function

import argparse
import json
import matplotlib.pyplot as plt
import numpy as np
import sys


def plot(args, data):
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

    if args.outfile:
        plt.savefig(args.outfile)
    else:
        plt.show()


def main(args=None):
    parser = argparse.ArgumentParser(
        description='reportgen')
    parser.add_argument('infile', nargs='?', type=argparse.FileType('r'),
                       default=sys.stdin)
    parser.add_argument('outfile', nargs='?', type=str, default=None)
    args = parser.parse_args(args)

    try:
        for line in args.infile:
            try:
                j = json.loads(line)

                if j['type'] == 'histogram':
                    plot(args, j['object'])
                    break
            except ValueError:
                pass

        print "No valid histogram data found"
        sys.exit(1)

    except KeyboardInterrupt as e:
        sys.exit(1)

if __name__ == '__main__':
    main()
