###### *Note that this is a 0.6 teeworlds mod (DDNet-compatible), if you are looking for 0.7 version, check [InfCroya](https://github.com/yavl/teeworlds-infcroya)*

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

### on Ubuntu / Mint / Debian
```bash
sudo apt install libicu-dev libmaxminddb-dev
./bam server_debug
```
Read the [wiki](https://github.com/yavl/teeworlds-infclassR/wiki) for more details.

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
