# Third-party notices

## MinHook

GZFX development diagnostics use MinHook v1.3.4 for bounded DXGI swap-chain acquisition hooks.

MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE.


## Skyrim Script Extender (SKSE)

GZFX 0.0.31+ is a native SKSE plugin and targets the public SKSE plugin ABI.
SKSE is installed separately by the user and is not redistributed in the GZFX
package. GZFX does not link or bundle CommonLibSSE-NG in the 0.0.31 migration.

Initial compatibility target:

- SKSE 2.3.1
- Skyrim Steam runtime 1.7.104

See the official SKSE distribution and source project for SKSE's own license
and redistribution terms.


## CommonLibSSE-GG

GZFX 0.0.32+ statically links the MIT-licensed CommonLibSSE-GG continuation,
pinned to commit:

    2053e94fd1c147b36eae2b4338118552fba407e2

Repository:

    eddoursul/CommonLibSSE-GG

GZFX intentionally does not link the later GPL-3.0-or-later CommonLibSSE-NG
line.

MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
