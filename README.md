# slc replay format

A tiny and incredibly fast replay format for storing Geometry Dash replays.

### Warning!

This format is not meant to be universal; an `slc` replay from one bot may not work with another bot. If you find yourself constantly switching between bots, you may want to consider using bots with a more universal format, like `.gdr`.

## To-do

These are features that are going to be implemented / were implemented already.

- [X] Replay reading/parsing
- [X] Blob optimization
- [X] Custom meta object
- [X] TPS changing support

## Features

- **Tiny**: The format is incredibly small, allowing huge savings in storage space.
- **Fast**: The format is incredibly fast to parse and write.
- **Extensible**: The format allows you to add your own metadata to the file. Adding metadata to inputs isn't possible due to it being unnecessary.
- **Comprehensive**: The format allows you to account for every single edge case, or input type you can think of.
- **Safe**: No such thing as a corrupted replay file.

## Motivation

Currently used replay formats are either incredibly slow or incredibly big, or both. Inefficient storage of replay data leads to lack of shareability
for very large files. Notoriously tiny formats (like `.ybot`) are incredibly slow to parse and write, due to them using variable length integers. The most popular replay format (`.gdr`) uses msgpack or json for serialization, which is incredibly slow and wasteful.
