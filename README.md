# Netlatency

The netlatency toolset is used to measure the latency and jitter parameters of an ethernet connection. The netlatency-tx generates UDP packets with embedded system timestamp (tx-user) and sequence number. The netlantency-rx captures these packets and dumps the collected receiving information such as hardware timestamp from the linux stack and the receiving system time (rx-user). netlatency-ry can detect receiving errors (dropped packets or sequence error).

## netlatency-tx

### Synopsis

    Usage:
      netlatency-tx [OPTION...] DEVICE - transmit timestamped test packets

    Help Options:
      -?, --help            Show help options

    Application Options:
      -d, --destination     Destination MAC address
      -h, --histogram       Create histogram data
      -i, --interval        Interval in milli seconds (default is 1000msec)
      -c, --count           Transmit packet count
      -m, --memlock         Configure memlock (default is 1)
      -P, --padding         Set the packet size
      -p, --prio            Set scheduler priority (default is 99)
      -Q, --queue-prio      Set skb priority
      -v, --verbose         Be verbose
      -V, --version         Show version inforamtion and exit

    This tool sends ethernet test packets.

### Outputs

    // not implemented yet
    {
      "type": "settings",
      "object": {
        "interval": 1000,
      }
    }

## netlatency-rx

### Synopsis

    Usage:
      netlatency-rx [OPTION...] DEVICE - receive timestamped test packets

    Help Options:
      -?, --help          Show help options

    Application Options:
      -v, --verbose       Be verbose
      -q, --quiet         Suppress error messages
      -c, --count         Receive packet count
      -s, --socket        Write packet results to socket
      -h, --histogram     Write packet histogram in JSON format
      -e, --ethertype     Set ethertype to filter(Default is 0x0808, ETH_P_ALL is 0x3)
      -f, --rxfilter      Set hw rx filterfilter
      -p, --ptp           Set hw rx filterfilter
      -V, --version       Show version inforamtion and exit

    This tool receives and analyzes incoming ethernet test packets.


### Output

    {
      "type": "rx-packet",
      "object": {
        "sequence-number": 1,
        "packet-size": 64,
        "tx-user-timestamp": "",
        "rx-hw-timestamp" "",
        "rx-user-timestamp" "",
      }
    }

    {
      "type": "rx-error",
      "object": {
        "dropped-packets": 1,
        "sequence-error": true,
      }
    }


    // not implemented yet
    {
      "type": "rx-packet-tx-timestamp",
      "object": {
        "sequence-number": 1,
        "tx-hw-timestamp": "",
      }
    }



## Helper: latency

	usage: latency [-h] [infile]

### Input

output from netlatency-rx

### Output

    {
      "type": "latency",
      "object": {
        "latency-user-hw": 10    // latency from tx user space to rx hw in
                                 // nanoseconds
        "latency-user-user": 100 // latency from tx user space to rx user space
                                 // in nanoseconds
      }
    }



## Helper: histogen

	usage: histogen [-h] [-c COUNT] [--width WIDTH] [infile]

### Input

output from latency

## Output
    {
      "type": "histogram",
      "object": {
        "count": 100,
        "time_error":,
        "min":,
        "max":,
        "outliers": ,
        "start-timestamp",
        "histogram": [0, 0, ...., 0],
      }
    }


## Helper: reportgen

reportgen creates a plot from the output of histogen. Input is STDIN by default and also can be
set by '-'. If no output is specified matplotlib will open a plot window (only if DISPLAY is available). Otherwise a PNG image is created.


    usage: reportgen [-h] [infile] [outfile]

## Usage Examples


### Receive testpackets and send with socat to UDP port

On receive servers site:

    $ ./netlatency-rx enp2s0  -v |  socat - udp-sendto:127.0.0.1:5000


On client site:

    $ socat - udp4-listen:5000,reuseaddr,fork


### Receive testpackets, calc latency, generate histogram and plot on screen

    $ netlatency-rx enp2s0 -c 10000 -v | latency -  | histogen  | reportgen

### Receive testpackets, calc latency, generate histogram and plot in file

    $ netlatency-rx enp2s0 -c 10000 -v | latency -  | histogen  | reportgen - /tmp/plot.png
