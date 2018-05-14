# Netlatency

The netlatency toolset is used to measure the latency and jitter parameters
of an ethernet connection. The netlatency-tx generates UDP packets with
embedded system timestamp (tx-user) and sequence number. The netlantency-rx
captures these packets and dumps the collected receiving information such
as timestamp from the linux network stack and the receiving system time
(rx-user). netlatency-ry can detect receiving errors (dropped packets or
sequence error).


The following timestamp values can be accessed on the netlatency-rx.

| Timestamp          | Description                                             |
| ------------------ | ------------------------------------------------------- |
| interval-start     | The interval start timestamp                            |
| tx-wakeup          | The wakeup timestamp of netlatency-tx                   |
| tx-program         | The timestamp when calling the send function            |
| tx-kernel-netsched | Linux kernel timestamp SOF_TIMESTAMPING_TX_SCHED        |
| tx-kernel-driver   | Linux kernel timestamp SOF_TIMESTAMPING_TX_SOFTWARE     |
| rx-hardware        | Linux kernel timestamp SOF_TIMESTAMPING_RX_HARDWARE     |
| rx-program         | Timestamp when handling the testpacket in netlatency-rx |


For linux kernel timestamp please refer to the kernel documentation:

https://www.kernel.org/doc/Documentation/networking/timestamping.txt

## Shortcomings

There is no byte order translation. Therefore, the sender and receiver
application must run on the same CPU architecture.

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
        "tx-user-target-timestamp": "",
        "rx-hw-timestamp" "",
        "rx-user-timestamp" "",
      }
    }

    {
      "type": "rx-packet",
      "object": {
        "sequence-number": 1,
        "stream-id": result->stream_id,
        "interval-usec": result->interval_usec,
        "offset-usec": result->offset_usec,
        "packet-size": result->packet_size,
        "timestamps": {
          "names": [
            "interval-start":,
            "tx-wakeup",
            "tx-program",
            "tx-kernel-netsched",
            "tx-kernel-driver",
            "rx-hardware",
            "rx-kernel-driver",
            "rx-program",
          ]
          "values": [
            <TIMESTAMP>,
            <TIMESTAMP>,
            <TIMESTAMP>,
            <TIMESTAMP>,
            <TIMESTAMP>,
            <TIMESTAMP>,
            <TIMESTAMP>,
            <TIMESTAMP>,
          ],
        }
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


## Helper: nl-calc

The nl-calc tool stores the testpacket results of nl-rx and builds information
for histogram analysis. Including Application accurancy/latency, full transmit
latency (from application to RX hardware) and rx jitter.


## Helper: nl-report

The nl-report tool displays the data generated by nl-calc in graphs.


## Helper: nl-trace

The nl-trace displays the output by nl-rx in a box-plot graphic. Each type of
timestamp is collected and the data depicted. Hence a time distribution of the
latency till the point of record can be seen.


For closer information about meaning of box-plot take a look at:

https://en.wikipedia.org/wiki/Box_plot


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
