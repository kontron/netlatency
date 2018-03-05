#!/usr/bin/env python

import sys
import json
import numpy as np
import matplotlib.pyplot as plt

def main():
    f = open(sys.argv[1])
    j = json.load(f)

    max_val = j['max']
    min_val = j['min']
    counts = j['count']
    outliers = j['outliers']
    y = j['histogram']
    x = range(0, len(j['histogram']))

    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.grid(True)
    ax.set_xlabel('latency ($\mu$s)')
    ax.set_ylabel('count')
#    ax.semilogy(x, y)
    ax.bar(x, y)
    plt.yscale('log')

    plt.title('counts: %s, min: %s $\mu$s, max: %s $\mu$s outliers: %s' \
            % (counts, min_val, max_val, outliers))
    plt.show()

if __name__ == '__main__':
    main()
