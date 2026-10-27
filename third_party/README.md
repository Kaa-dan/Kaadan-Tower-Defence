# third_party

Vendored code that is not ours. Committed as-is, never edited, never fetched at configure time.
Excluded from clang-format on purpose — reformatting it would make the next upgrade a diff
against our whitespace instead of against upstream.

| File | Version | Source | SHA-256 |
|---|---|---|---|
| `stb_image.h` | v2.30 | [nothings/stb@013ac3b](https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h) | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` |

`stb_image.h` is public domain (or MIT, at the user's choice). It is compiled in exactly one
translation unit, `engine/image/Image.cpp`, configured to PNG only with no file I/O. See
[ADR 0014](../docs/decisions/0014-image-decoding.md).

To upgrade: replace the file, update this row, and run the `asan` preset — the decoder parses
input shaped like untrusted data, which is what that preset exists for.
