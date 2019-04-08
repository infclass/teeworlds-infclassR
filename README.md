# Teeworlds InfClassR
Slightly modified version of original [InfClass by necropotame](https://github.com/necropotame/teeworlds-infclass).
## Additional dependencies
[GeoLite2++](https://www.ccoderun.ca/GeoLite2++/api/) is used for IP geolocation
```bash
sudo apt install libmaxminddb-dev
```

## Building
Install [bam](https://github.com/matricks/bam) 0.4.0 build tool. Compile it from source or get [precompiled binaries](https://github.com/yavl/teeworlds-infclassR/tree/master/bin/bam) for your platform.
```
git clone https://github.com/yavl/teeworlds-infclassr
cd teeworlds-infclassr
```
Copy bam executable into teeworlds-infclassr directory.

With ```sv_maprotation``` you  can define what maps will be played in which order, for example:<br/>
```sv_maprotation "infc_skull infc_warehouse infc_damascus"```<br/>
With ```sv_rounds_per_map``` you can define how many rounds each map should be played before changing to the next map.<br/>
If ```inf_maprotation_random``` is set to 1 than a random map will be chosen after a map finished.

### on Ubuntu
```bash
sudo apt install libicu-dev libmaxminddb-dev
./bam server_debug
```

### on macOS
via [Homebrew](https://brew.sh):
```bash
brew install icu4c libmaxminddb
./bam server_debug_x86_64
```

### on Windows
GCC should be installed, e.g. [Mingw-w64](https://mingw-w64.org).
```
bam config stackprotector=true nogeolocation=true
bam server_debug
```

## Server commands
Read the [wiki](https://github.com/yavl/teeworlds-infclassR/wiki)

This product includes GeoLite2 data created by MaxMind, available from
[https://maxmind.com](https://www.maxmind.com).
