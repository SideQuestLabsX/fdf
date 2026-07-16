# Syntax

A document is a list of entries. Each entry has an identifier followed by either a scalar
value after `=`, a map in `{ }`, or an array in `[ ]`. The top level is a map.

Whitespace outside strings doesn't matter.

## Entries

```fdf
key = value     // scalar
mapName{ ... }  // map
arrName[ ... ]  // array
```

Identifiers are capped at 30 characters.

## Comments

```fdf
// line comment
/* block
   comment */
/*# file comment, must be the first thing in the file #*/
```

A file comment is a block comment whose body starts with `#`. Entry comments go inline
after the value or on the line above, and stay attached to their entry through a round
trip.

See also [Types](Types.md) and [Containers](Containers.md).
