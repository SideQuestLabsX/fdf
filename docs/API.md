# API

Everything lives in `namespace fdf`. A document is a tree of `Entry` nodes owned through
`fdf::UniqueEntryPtr` (a `unique_ptr` with a custom deleter). The root is a map.

## Types and free functions

```cpp
using UniqueEntryPtr = std::unique_ptr<Entry, EntryDeleter>;

UniqueEntryPtr NewEntry();                                  // fresh, empty root map

template<auto DIAG = nullptr>
UniqueEntryPtr ParseFile(const std::filesystem::path&);     // null on failure
template<auto DIAG = nullptr>
UniqueEntryPtr ParseBuffer(std::string_view);               // constexpr-friendly

template<Style STYLE = {}>
bool WriteFile(const Entry&, const std::filesystem::path&, bool bCreateIfNotExists = true);
template<Style STYLE = {}>
fdf::String WriteBuffer(const Entry& root);
```

`ParseBuffer`, `WriteBuffer`, `NewEntry` and the whole `Entry` interface are `constexpr`
and work inside a `consteval` function. The optional
`DIAG` template argument is a compile-time diagnostic callback. See
[Diagnostics](Diagnostics.md).

`WriteFile(..., false)` only overwrites an existing regular file. The default creates the
file and missing parent directories. Bare filenames use the current directory.

`Type` is the tag every entry carries

```cpp
enum class Type : uint8_t { Map, Array, Null, Nil = Null, Bool, Int, Float,
                            String, Hex, Version, Timestamp };
```

## Reading values

`GetChild` accepts a dotted path containing map keys and array indices. It returns
`nullptr` when the path doesn't exist.

```cpp
Entry*       GetChild(path...);          // "window.title", "items.0.name"
Entry*       GetDirectChild(key);        // single level, by name or index
std::string_view GetIdentifier()  const; // this node's key
fdf::String  GetFullIdentifier()  const; // dotted path from the root
fdf::String& GetComment();               // + const overload, converts to std::string_view
Type         GetType()            const;
uint32_t     GetChildCount()      const;
```

`GetValue<T>()` reads the scalar payload. Every scalar type comes back as a `std::span` over its
components (one element for a plain scalar, more for a pack); a timestamp is decoded into a
`Timestamp` struct on demand.

```cpp
auto name = e->GetChild("name")->GetValue<fdf::String>()[0];     // "MyGame"
auto px   = e->GetChild("pos")->GetValue<int64_t>();             // pack span: [100, 100]
auto flag = e->GetChild("fullscreen")->GetValue<bool>()[0];      // true
auto vers = e->GetChild("versions")->GetValue<Version>();
auto when = e->GetChild("created")->GetValue<Timestamp>();       // decoded fields
```

`GetValue<uint64_t>()` returns `fdf::UIntSpan` (or `ConstUIntSpan`), a span-alike unsigned view
over the same `Int` storage: indexing, iteration, `front`/`back`/`first`/`last`/`subspan` and
element writes all work. Each element is bit-cast, so values above `2^63-1` read back exactly.
`data()` hands out a raw `uint64_t*` and is the one runtime-only member.

`Version` is 16 bytes. `major` is limited to `0..2147483647`; the other components use the
full `uint32_t` range. `bHasRevision` distinguishes `1.2.3` from `1.2.3.0`. Packs may mix both
forms: `1.0.0|2.0.0.0`.

`Hex` stores decoded bytes in source order. `0x` is the empty byte string. `Read(value, offset)` and
`Write(value, offset)` convert between the stored big-endian bytes and a host value, returning `bool`
for success. Integral reads use the available bytes and zero-extend, floats need their full width,
and a `bool` is one byte (nonzero reads true).

Offsets run `0` to `Size()` on both sides. `Size()` itself reads zero bytes, so it decodes to `0`,
and writes there append. An overwrite may run past the end and extend the value, but an offset past
`Size()` has nothing to overwrite and is rejected, so a write never leaves a gap.

| Member | Meaning |
|---|---|
| `Bytes()` | byte span over the value |
| `Size()` | byte count |
| `DigitCount()` | `Size() * 2` |
| `IsEmpty()` | `Size() == 0` |
| `MaxSize()` | largest byte count a `Hex` can hold |

`Hex(std::span<const std::byte>)` and `Assign(std::span<const std::byte>)` copy raw bytes in.

A class type joins `Read`/`Write` through cursor ADL hooks: `WriteHex(fdf::HexWriter&, const T&)`
and `ReadHex(fdf::HexReader&, T&)`. Cursors advance on each call, so hooks chain member
transfers without offset arithmetic:

```cpp
struct Rgb { uint8_t r, g, b; };
constexpr bool ReadHex(fdf::HexReader& reader, Rgb& v) noexcept
{
    return reader.Read(v.r) && reader.Read(v.g) && reader.Read(v.b);
}
constexpr bool WriteHex(fdf::HexWriter& writer, const Rgb& v) noexcept
{
    return writer.Write(v.r) && writer.Write(v.g) && writer.Write(v.b);
}
```

Cursor reads want the scalar's full width. Zero-extension stays on direct `Hex::Read`. A failed hook
leaves the value unchanged: an overwrite stages its bytes past the current end and splices them into
place only on success.

`Assign` replaces, `Decode` appends and `Decode` at an offset overwrites, under the same offset rule.
All accept an optional `0x`/`0X` prefix and leave the value untouched when a digit is invalid. There
is no `string_view` constructor, since bad digits would have no way to reach the caller. Decode into
a default-constructed `Hex` and check the result.

```cpp
Hex h;
bool bOk = h.Assign("0xFF5733");   // FF 57 33
bool bMore = h.Decode("ABC");      // FF 57 33 0A BC
bool bAt = h.Decode("99", 1);      // FF 99 33 0A BC
bool bBad = h.Decode("99", 9);     // false, nothing at offset 9 to overwrite
```

Digit width is cosmetic: `0xABC` and `0x0ABC` hold the same two bytes. Hex writes whole bytes, so an
odd-length literal comes back padded (`0xABC` → `0x0ABC`), and an odd digit count decodes with a
leading zero nibble.

```cpp
Hex& color = e->GetChild("color")->GetValue<Hex>()[0];   // 0xFF5733
uint32_t rgb = 0;
bool bRead = color.Read(rgb);                            // rgb = 0x00FF5733 on every host
bool bAppended = color.Write(uint8_t{0xAA});              // color = 0xFF5733AA
bool bWrote = color.Write(uint8_t{0x99}, 1);              // color = 0xFF9933AA
```

A string value is stored as an array of `fdf::String` (see [fdf::String](#fdfstring)).

```cpp
std::span<fdf::String> comps = e->GetChild("name")->GetValue<fdf::String>();
comps[0] = "renamed";                            // reallocs that component only
std::string_view view = comps[0];                // "renamed"
```

## Walking the tree

```cpp
std::span<Entry*>      GetChildren();
std::vector<Entry*>    GetChildrenRecursive();
size_t                 GetChildCountRecursive() const;

template<auto FLAGS = ForEachFlags::None>
void ForEach(auto&& callback);   // callback(Entry&) or callback(const Entry&)
```

`ForEachFlags` are bit flags, combined with `|`:

| Flag | Effect |
|------|--------|
| `None` | visit direct children only |
| `Recursive` | descend into nested containers |
| `Group` | visit entries of similar type together |
| `IncludeSelf` | visit the starting node too |
| `All` | all of the above |

```cpp
root->ForEach<fdf::ForEachFlags::Recursive>([](const fdf::Entry& e)
{
    // every node, depth-first
});
```

## Building and editing

```cpp
Entry* Emplace(std::string_view key);    // add a child, returns it ("" for array items)
Entry* AddChild(UniqueEntryPtr& e);      // adopt an existing node

void SetValue(value);                    // bool, integer, float, string, Version, Timestamp, Hex...
                                         // pass std::span for a pack, or edit components through
                                         // GetValue(); an rvalue String or Hex is moved
bool SetIdentifier(std::string_view);
void SetType(Type);
void Resize(uint32_t);                   // grow/shrink a bool/int/float/string/version/hex pack,
                                         // tail zero or empty. Timestamp packs ignore it

bool RemoveChild(child | key | index);
bool ClearChildren();
UniqueEntryPtr OrphanChild(child | key | index);   // detach without destroying
std::vector<UniqueEntryPtr> OrphanChildren();
```

`SetValue(fdf::ArrayType{})` / `SetValue(fdf::MapType{})` turn a node into an empty
container. Then use `Emplace` to add its children.

```cpp
UniqueEntryPtr root = fdf::NewEntry();

root->Emplace("name")->SetValue("MyGame");
root->Emplace("score")->SetValue(42);

if(Entry* arr = root->Emplace("levels"))
{
    arr->SetValue(fdf::ArrayType());
    arr->Emplace("")->SetValue(1);
    arr->Emplace("")->SetValue(2);
}
```

Writing the tree back out is covered in [Styling](Styling.md).

## fdf::String

The string value type. 8 bytes: a single pointer to a `[u32 size][u32 capacity][chars…][\0]` block.
It mirrors most of `std::string`'s mutable API and delegates most reads to `std::string_view`.
It can be converted to/from `std::string`.

Deliberate differences from `std::string`:

- `substr` returns a `std::string_view` into the block, not a new `String`. That view, like
  any `string_view`, `data()`, `c_str()`, iterator or `operator[]` reference, dangles the moment
  the string is mutated or destroyed.
- All operations are `noexcept`. Out-of-range access asserts, so there is no `at()`.
- Sizes are `size_t` on the interface, storage stays `uint32_t`, so a size past 4GB asserts.
  `npos` is `std::string_view::npos`.
- No allocator API, `shrink_to_fit`, or SSO. Slab buckets make `shrink_to_fit` a no-op.
- Free `operator+` covers every `String`/`string_view`/`char` mix, with `String&&` overloads
  on either side that grow an existing buffer in place instead of allocating fresh.

```cpp
fdf::String s = fdf::String("game") + "-" + "config";   // "game-config", rvalue lhs reused
s.replace(0, 4, "asset");                               // "asset-config"
if(s.ends_with("config") && s.contains("-"))
    std::print("{}\n", s);
```

## Combining documents

Merge a second document into an existing tree. `fdf::CommentCombineStrategy` resolves
comment conflicts.

```cpp
template<auto DIAG = nullptr>
bool ParseCombineFile(const std::filesystem::path&, CommentCombineStrategy = UseNewIfExistingIsEmpty);
template<auto DIAG = nullptr>
bool ParseCombineBuffer(std::string_view, CommentCombineStrategy = UseNewIfExistingIsEmpty);
bool Combine(UniqueEntryPtr& other, CommentCombineStrategy = UseNewIfExistingIsEmpty);
```

When `Combine` succeeds, it consumes `other`. Incoming values replace matching map keys,
while unrelated keys stay in place. Invalid or incompatible input returns `false` without
consuming `other`.

| Strategy | Behavior |
|----------|----------|
| `UseExisting` | keep the current comment |
| `UseNew` | replace with the incoming comment |
| `UseNewIfExistingIsEmpty` | use incoming only when current is empty (default) |
| `Merge` | concatenate both comments |
| `Clear` | drop the comment |
