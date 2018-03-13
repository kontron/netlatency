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


## netlatency-tx

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


### JSON outputs

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
      "type": "settings",
      "object": {
        "interval": 1000,
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
