# Containers

Maps use `{ }`, arrays use `[ ]`. Either can be written on one line or many. Commas are
optional when entries sit on their own lines and required between entries on the same
line. A trailing comma is allowed.

```fdf
single{ resolution = 1920|1080, fullscreen = true, volume = 75 }

multi
{
    resolution = 1920|1080
    fullscreen = true
    volume     = 75
}

tags[ "config", "map", "example" ]
```

Containers can nest:

```fdf
items
[
    { id = 1, name = "Potion" }
    { id = 2, name = "Elixir" }
    { id = 3 }              // a left-out field doesn't exist, it is not null
]

grid
[
    [ 10, 20, 30 ]
    [ 40, 50, 60 ]
]
```
