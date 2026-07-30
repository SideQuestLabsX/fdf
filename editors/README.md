# Editor support

`fdf.tmLanguage.json` is the canonical grammar. The VS Code package includes it directly and
`package.py` converts it to plist for Sublime Text, TextMate and JetBrains.

| Asset | Use |
|---|---|
| `fdf-<version>.vsix` | VS Code |
| `fdf.sublime-package` | Sublime Text |
| `fdf.tmbundle.zip` | JetBrains IDEs and TextMate |
| `fdf.tmLanguage.json` | Tools that load TextMate JSON directly |

VS Code reads `language-configuration.json` directly. `package.py` converts its comment and
indentation rules into `.tmPreferences` for the plist packages. JetBrains documents syntax
highlighting only, so its handling of those preferences is untested.

## Installing

| Editor | Install |
|---|---|
| VS Code | Run `code --install-extension fdf-<version>.vsix` |
| Sublime Text | Open `Preferences > Browse Packages`, move up to the data directory and copy `fdf.sublime-package` into its `Installed Packages` directory |
| JetBrains | Open `Settings > Editor > TextMate Bundles` and add the unpacked `fdf.tmbundle` directory |
| TextMate | Unzip the asset and double click `fdf.tmbundle` |

## Status

The grammar is best effort and may disagree with the parser. Use `fdf-validate` to check files.

## Building the packages

```sh
python3 editors/package.py --version 0.2.1 --out dist
(cd editors && npx --yes @vscode/vsce@3.9.2 package --out ../dist/fdf-0.2.1.vsix)
python3 editors/verify.py dist 0.2.1
```

`package.json` carries the version and the release workflow refuses to publish when it disagrees
with the release version.
