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

void SetValue(value);                    // bool, integer, float, string, Version, Timestamp...
                                         // pass std::span for a pack, or edit components through
                                         // GetValue(); the component count stays fixed
bool SetIdentifier(std::string_view);
void SetType(Type);
void Resize(uint32_t);                   // grow/shrink a bool/int/uint/float/version/string pack; tail zero/empty

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
