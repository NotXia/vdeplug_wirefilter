# Unstable connection
We can use Wirefilter to simulate a very unstable Internet connection through a Markov chain.

## Prerequisited
[vdeplug_slirp](https://github.com/virtualsquare/vdeplug_slirp) is required for this example.

## Setup
Creare a rc file:
```
# conf.rc

markov-numnodes 6
markov-time 3000

bandwidth 10M[1] 500K[2] 100K[3] 10K[4] 1K[5]

setedge 0,1,40
setedge 0,0,20
setedge 0,5,40

setedge 1,0,40
setedge 1,1,20
setedge 1,2,40

setedge 2,1,40
setedge 2,2,20
setedge 2,3,40

setedge 3,2,40
setedge 3,3,20
setedge 3,4,40

setedge 4,3,40
setedge 4,4,20
setedge 4,5,40

setedge 5,4,40
setedge 5,5,20
setedge 5,6,40
```

Then create a namespace connected to slirp and require an IP address:
```
vdens -R 10.0.2.3 wirefilter://[rc=conf.rc/nofifo]{slirp://}
/sbin/udhcpc -i vde0
```

You can now start testing the performances of the network.
For example by opening a browser and running a speed test:
```
firefox
```
Note: other Firefox instances should be closed.