# Syntax

A document is a list of entries. An entry has an identifier and then either a scalar
value (after `=`) or a container, a map `{ }` or array `[ ]`. The top level is itself a
map.

Whitespace outside strings is insignificant, so you can lay things out however reads
best.

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

A file comment is a block comment whose body starts with `#`. Entry comments may be
inline (trailing) or leading, and the parser keeps them attached to their entry so they
survive a round-trip.

See also [Types](Types.md) and [Containers](Containers.md).
