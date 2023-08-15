# FAT32
Implementation FAT32 filesystem with basic operations with disk.

Implementation ased on [this](http://elm-chan.org/docs/fat_e.html)
## Usage
./FAT32 <path to disk> or ./FAT32 with no parameters which created default disk file with 20mb size.

Commands:

format - format disk to FAT32.

cd <path> - open folder.

mkdir <folder name>  - create folder.

touch <file name> - create file.

## Building 
~~~bash
cd FAT32
mkdir build
cd build
cmake ..
make
~~~