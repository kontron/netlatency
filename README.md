# Netlatency

The netlatency toolset is used to measure the latency and jitter parameters of an ethernet connection.

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
        "histogram": [0, 0, ...., 0],
      }
    }


## Usage Examples

### Receive testpackets and send with socat to UDP port

    ./netlatency-rx enp2s0  -v |  socat - udp-sendto:127.0.0.1:5000


### Receive testpackets, calc latency, generate histogram and plot

    ./netlatency-rx enp2s0 -c 10000 -v | ./latency.py -  | ./histogen.py  | ./reportgen.py
