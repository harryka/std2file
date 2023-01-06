# Introduction
read stdout pipe file data from business app process,then save thoes output to file at user defined path.

# Usage
```
Usage: 
    std2file [-b base dir path]
    [-p] prefix of log file name
    [-a] all files max size
    [-s] single file max size
    [-d] dump process step debug info

example:
    business_app | std2file -b ~/debug_logs/
```

# Compile
```
mkdir build
cd build
cmake ..
make
```

