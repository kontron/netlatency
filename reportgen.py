#!/usr/bin/env python

import json
import numpy as np
import matplotlib.pyplot as plt

def main():
    f = open('/tmp/out-tx')
    j = json.load(f)

    max_val = j['max']
    min_val = j['min']
    outliers = j['outliers']
    y = j['histogram']
    x = range(0, len(j['histogram']))

    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.grid(True)
    ax.set_xlabel('latency')
    ax.set_ylabel('count')
    ax.semilogy(x, y)

    plt.title('min: %s, max: %s oultiers: %s' % (min_val, max_val, outliers))
    plt.show()

if __name__ == '__main__':
    main()
