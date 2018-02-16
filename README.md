# tixfsgen

This is a \*NIX command-line tool for use with the TIX operating system. It
converts a directory into a TIXFS filesystem, in Intel hex format. That file can
be combined with the ROM or upgrade file to be the initial filesystem image.

## Building

`make` to build and `sudo make install` to install like normal.

## Usage

`tixfsgen <hex-file> <root-dir>` will create an Intel hex format file containing
the TIXFS filesystem from the filesystem in `<root-dir>`. `-[ugdD]<host>:<tix>`
options map the user, groul, device minor, and device major IDs from the value
(or user or group name) <host> to the ID number <tix> in the generated filesystem.

## TODO

* Support symbolic links (have to wait for TIX to support them).

* Add command-line options to control the behavior. In particular:

| Option            | Description                                   |
| ----------------- | --------------------------------------------- |
| -m<model>         | Model of TI83/4 to determin amount of ROM     |
| -R                | Include only specified files (non-recursive)  |
| -s, -e            | Set the start and end pages of the filesystem |

* If a file has multiple hard links to it, only count those in the new
  filesystem. (Currently, this ignores hard links to avoid creating un-deletable
  files).
  
* Combine the major and minor device ID mappings since mapping minor IDs is
  not very useful without the major ID also being specified.

## License

This project is licensed under the MIT Public License. See LICENSE for more
information.

