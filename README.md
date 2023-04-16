# vdeplug_wirefilter
This libvdeplug plugin module allows to manipulate the properties of the virtual wire between two VDE plugs.

## Install vdeplug_wirefilter
```
mkdir build
cd build
cmake ..
make
sudo make install
```

## Usage example
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

More examples can be found [here](./examples).

Refer to the man page (libvdeplug_wirefilter) for more information.