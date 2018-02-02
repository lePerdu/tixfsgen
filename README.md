# tixfsgen

This is a \*NIX command-line tool for use with the TIX operating system. It
converts a directory into a TIXFS filesystem, in a binary format. That file can
be combined with the ROM or upgrade file to act as the initial filesystem image.

## Building

`make` to build and `sudo make install` to install like normal.

## Usage

`tixfsgen <hex-file> <root-dir>` will create an Intel hex format file containing
the TIXFS filesystem from the filesystem in `<root-dir>`.

## TODO

* Support symbolic links (have to wait for TIX to support them).

* Add command-line options to control the behavior. In particular:
| Option            | Description                                   |
| ----------------- | --------------------------------------------- |
| -u<host>:<guest>  | Translate UID <host> to the UID <guest>       |
| -g<host>:<guest>  | Translate GID <host> to the GID <guest>       |
| -D<host>:<guest>  | Translate major device ID <host> to <guest>   |
| -d<host>:<guest>  | Translate minor device ID <host> to <guest>   |
| -m<model>         | Model of TI83/4 to determin amount of ROM     |

* If a file has multiple hard links to it, only count those in the new
  filesystem. (Currently, this ignores hard links to avoid creating un-deletable
  files).

## License

This project is licensed under the MIT Public License. See LICENSE for more
information.

