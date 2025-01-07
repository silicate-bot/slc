# slc replay format

A tiny and incredibly fast replay format for storing Geometry Dash replays.

### Warning!

This format is not meant to be universal; an `slc` replay from one bot may not work with another bot. If you find yourself constantly switching between bots, you may want to consider using bots with a more universal format, like `.gdr`.

## Example

```cpp
using InputType = slc::Input::InputType;

// Declare the meta by using a struct
struct ReplayMeta {
  uint64_t seed;
};

slc::Replay<ReplayMeta> replay;
// Set tps by directly changing the member
replay.m_tps = 480.0;

// Add inputs to the replay
replay.addInput(400, InputType::Jump, false, true);
replay.addInput(500, InputType::Jump, false, false);

// Change the TPS mid-replay
replay.addTPSInput(1000, 720.0);

// Save the replay
std::ostream file("out.slc");
replay.write(file);
```

## Features

- **Tiny**: The format is incredibly small, allowing huge savings in storage space.
- **Fast**: The format is incredibly fast to parse and write.
- **Extensible**: The format allows you to add your own metadata to the file. Adding metadata to inputs isn't possible due to it being unnecessary.
- **Comprehensive**: The format allows you to account for every single edge case, or input type you can think of.
- **Safe**: No such thing as a corrupted replay file.

## Motivation

Currently used replay formats are either incredibly slow or incredibly big, or both. Inefficient storage of replay data leads to lack of shareability
for very large files. Notoriously tiny formats (like `.ybot`) are incredibly slow to parse and write, due to them using variable length integers. The most popular replay format (`.gdr`) uses msgpack or json for serialization, which is incredibly slow and wasteful.
