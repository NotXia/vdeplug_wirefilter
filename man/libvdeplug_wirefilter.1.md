# NAME
`libvdeplug_wirefilter` -- vdeplug nested module: modify wire properties


# SYNOPSIS
libvdeplug_wirefilter.so


# DESCRIPTION
Wirefilter is a libvdeplug nested module that allows to manipulate the properties of the packet flow passing through the plugin.

This module of libvdeplug4 can be used in any program supporting vde like
`vde_plug`, `vdens`, `kvm`, `qemu`, `user-mode-linux` and `virtualbox`.

The vde_plug_url syntax of this module is the following:

&nbsp;&nbsp;&nbsp; `wirefilter://`[ `[`*OPTIONS*`]` ]`{` *vde nested url* `}`


# OPTIONS

## Wire properties
Wire properties are set with the following format:

&nbsp;&nbsp;&nbsp; [*direction*]value[+*variation*][*multiplier*][*algorithm*]

- *direction* specifies the direction of the value (**LR** for left-to-right and **RL** for right-to-left, both if not set).\
- *variation* sets a random variation on the value (0 if not set).\
- *multiplier* specifies the unit of measure of the value (**K**, **M** or **G**, no multiplier applied by default).\
- *algorithm* specifies the type of distribution for the variation (**U** for uniform and **N** for Gaussian normal, uniform by default).

`delay`
: adds extra delay (in milliseconds).

`dup`
: probability (0-100) of duplicated packets.
: Note: 100% causes each packet to be duplicated infinitly.

`loss` 
: probability (0-100) of packets loss.

`lostburst` 
: if not zero, uses Gilbert model for bursty loss.
: This is the mean length of lost packet bursts (a two state Markov chain):
: the probability to exit from the faulty state is 1/lostburst and the probability to 
: enter the faulty state is loss/(lostburst-(1-loss)). The loss rate converges to the value of `loss`.

`mtu` 
: maximum allowed size (in bytes) for packets. This value is the same for both directions.

`bufsize` 
: maximum size (in bytes) of the packets queue. Exceeding packets are discarded. This value is the same for both directions.

`bandwidth` 
: channel bandwidth in bytes/sec.
: Sender is not prevented from sending packets, delivery is delayed to limit the bandwidth to the desired value (like a bottleneck along the path).

`speed` 
: interface speed in bytes/sec.
: Input is blocked for the tramission time of the packet, thus the sender is prevented from sending too fast.

`nofifo` 
: if set (as flag), it is not guaranteed that packets are delivered in order (e.g. if delayed with different values).

`noise` 
: number of bits damaged/one megabyte.

## Blink

`blink=path`
: if set, a PF_UNIX/DATAGRAM socket is created at the specified path and for each packet a log message will be sent.
: Each log has format:

        id direction length (e.g. 6768 LR 44)

`blinkid=id` 
: sets the id to be sent for each packet log with `blink`. Defaults to Wirefilter pid.

## Management
`mgmt=path` 
: creates an unix socket to manage the parameters. Can be accessed with `vdeterm` and used as a remote terminal.

`mgmtmode=0700` 
: access mode of the management socket.

`rc=path` 
: configuration file loaded at Wirefilter startup. It uses the same syntax of the management interface.

## Other
`pidfile=path` 
: saves Wirefilter pid into the specified file.

## Markov mode
Wirefilter provides a more complex set of parameters using a Markov chain to emulate different states of the link and the transitions between states.\
Each state is represented by a node. Markov chain parameters can be set with management commands or rc files only. In fact, due to the large number of parameters the command line would have been unreadable.

`markov-numnodes n`
: defines the number of different states. All the parameters of the connection can be defined node by node. Nodes are numbered starting from zero (to n-1).

    e.g.:

        delay 100+10N[4]

    It is possible to resize the Markov chain at run-time. New nodes are unreachable and do not have any edge to other states (i.e. each new node has a loopback edge to the node itself with 100% probability). When reducing the number of nodes, the weight of the edges towards deleted nodes is added to the loopback edge. When the current node of the emulation is deleted, node 0 becomes the current node. (The emulation always starts from node 0).

`markov-time ms`
: time period (ms) for the markov chain computation. Each ms microseconds a random number generator decides which is the next state (default value=100ms).

`markov-name n,name`
: assign a name to a node of the markov chain.

`markov-setnode n`
: manually set the current node to the node n.

`setedge n1,n2,w`
: define an edge between n1 and n2; w is the weight (probability percentage) of the edge. The loopback edge (from a node to itself) is always computed as 100% minus the sum of the weights of outgoing edges.

`showedges [ n ]`
: list the edges from node n (or from the current node when the command has no parameters). Null weight edges are omitted.

`showcurrent`
: show the current Markov state.

`showinfo [ n ]`
: show status and information on state (node) n. If the parameter is omitted it shows the status and information on the current state.

`markov-debug [ n ]`
: set the debug level for the current management connection. In the actual implementation when n is greater than zero each change of markov node causes the output of a debug trace. Debug tracing get disabled when n is zero or the parameter is missing.


# EXAMPLES
Open two terminals.\
In the first terminal run:
```
vdens vxvde://234.0.0.1
ip addr add 10.0.0.1/24 dev vde0
ip link set vde0 up
```

In the second terminal run:
```
vdens wirefilter://[delay="LR100"]{vxvde://234.0.0.1}
ip addr add 10.0.0.2/24 dev vde0
ip link set vde0 up
ping 10.0.0.1
```
Each packet will have a RTT of ~100 ms.


# SEE ALSO
**vde_plug**(1), **vdeterm**(1).