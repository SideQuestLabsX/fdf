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
void WriteBuffer(const Entry& root, std::string& out);
```

`ParseBuffer`, `WriteBuffer`, `NewEntry` and the whole `Entry` interface are `constexpr`,
so you can parse, inspect and serialize inside a `consteval` function. The optional
`DIAG` template argument is a compile-time diagnostic callback, see
[Diagnostics](Diagnostics.md).

`Type` is the tag every entry carries

```cpp
enum class Type : uint8_t { Map, Array, Null, Nil = Null, Bool, Int, UInt, Float,
                            String, Hex, Version, Timestamp };
```

## Reading values

`GetChild` looks up by a dotted path, map keys and array indices both work. It returns
`nullptr` when the path doesn't exist

```cpp
Entry*       GetChild(path...);          // "window.title", "items.0.name"
Entry*       GetDirectChild(key);        // single level, by name or index
std::string_view GetIdentifier()  const; // this node's key
std::string  GetFullIdentifier()  const; // dotted path from the root
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
auto when = e->GetChild("created")->GetValue<Timestamp>();       // decoded fields
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

`ForEachFlags` are bit flags you can combine, `Recursive`, `Group` (visit similar types
together), `IncludeSelf`, plus `None` and `All`

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

void SetValue(value);                    // bool, integer, float, string, Timestamp...
                                         // std::span for a pack OR you could just GetValue() and
                                         // edit each member separately. (Can't change member count) 
bool SetIdentifier(std::string_view);
void SetType(Type);
void Resize(uint32_t);

bool RemoveChild(child | key | index);
bool ClearChildren();
UniqueEntryPtr OrphanChild(child | key | index);   // detach without destroying
std::vector<UniqueEntryPtr> OrphanChildren();
```

`SetValue(fdf::ArrayType{})` / `SetValue(fdf::MapType{})` turn a node into an empty
container, then `Emplace` its children

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
It mirrors most of the `std::string`'s API for edit and delegates most of the read operations to std::string_view.

Deliberate divergences from `std::string`:

- **`substr` returns a `std::string_view`** into the block, not a new `String`. That view, like
  any `string_view`, `data()`, `c_str()`, iterator or `operator[]` reference, dangles the moment
  the string is mutated or destroyed.
- **Everything is `noexcept` and nothing throws.** Out-of-range access asserts, so there is no
  `at()`.
- **Sizes are `size_t` on the interface, storage stays `uint32_t`,** so a size past 4GB asserts.
  `npos` is `std::string_view::npos`.
- **No allocator surface, no `shrink_to_fit`, no SSO.** The slab allocator makes the first two
  no-ops.
- **Free `operator+` covers every `String`/`string_view`/`char` mix,** with `String&&` overloads
  on either side that grow an existing buffer in place instead of allocating fresh.

```cpp
fdf::String s = fdf::String("game") + "-" + "config";   // "game-config", rvalue lhs reused
s.replace(0, 4, "asset");                               // "asset-config"
if(s.ends_with("config") && s.contains("-"))
    std::print("{}\n", s);
```

## Combining documents

Merge a second document into an existing tree. Comment conflicts are resolved by
`fdf::CommentCombineStrategy`

```cpp
template<auto DIAG = nullptr>
bool ParseCombineFile(const std::filesystem::path&, CommentCombineStrategy = UseNewIfExistingIsEmpty);
template<auto DIAG = nullptr>
bool ParseCombineBuffer(std::string_view, CommentCombineStrategy = UseNewIfExistingIsEmpty);
bool Combine(UniqueEntryPtr& other, CommentCombineStrategy = UseNewIfExistingIsEmpty);
```

| Strategy | Behavior |
|----------|----------|
| `UseExisting` | keep the current comment |
| `UseNew` | replace with the incoming comment |
| `UseNewIfExistingIsEmpty` | use incoming only when current is empty (default) |
| `Merge` | concatenate both comments |
| `Clear` | drop the comment |
