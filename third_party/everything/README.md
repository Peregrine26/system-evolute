# Everything SDK wrapper

CrossDesk bundles only the official Everything SDK IPC wrapper. It does not
bundle, install, configure, or start the Everything application itself.

- Source: https://www.voidtools.com/Everything-SDK.zip
- Binary source: verified DeskBox reference snapshot
  `fb6e1c4aaf811a5cc8b0748ced4a0639631176cb`, which records retrieval on 2026-08-25
- License: MIT, reproduced in `LICENSE.txt`
- `Everything64.dll` SHA-256:
  `81B5BE18126ACD2C2B913F8F4A821E476B18393CDD3DEBD03387C50AFD8DB88F`
- `EverythingARM64.dll` SHA-256:
  `8531EA393677DD8FD37BED7420AC93344CD458B9A1324BA65C4A75D024D61886`

CMake copies one architecture-matching file beside the Windows 64-bit executable
as `EverythingSdk.dll`, together with `licenses/Everything/LICENSE.txt`.
The C++ implementation loads its C API with Qt's `QLibrary`. Search falls back
to mapped folders when the DLL or the separately installed Everything server
is unavailable. No C# or Rust application runtime is required.
