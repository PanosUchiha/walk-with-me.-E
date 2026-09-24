# zlib puff

Unmodified `puff.c` and `puff.h` from zlib v1.3.1, puff version 2.3.
Copyright (C) 2002-2013 Mark Adler; zlib license (full notice in `puff.h`).

Sources:
- https://github.com/madler/zlib/blob/v1.3.1/contrib/puff/puff.c
- https://github.com/madler/zlib/blob/v1.3.1/contrib/puff/puff.h

SHA-256:
- puff.c: `5b9d75aeb5baf3575415bc6ade3f2a02e50b6b971b3f8b4fda2b03543bc6e52f`
- puff.h: `969b7be2a930db0cdcb19b0e5b29ae6741f5a8f663b6dba6d647e12ec60cfa8e`

Used to read compressed Skyrim dialogue metadata. The caller serializes puff
calls because its fixed-code tables initialize lazily. Files are compiled as
C++ without source edits; no additional runtime DLL is required.
