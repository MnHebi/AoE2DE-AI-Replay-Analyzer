# Runtime notices

Qt 6 runtime libraries are dynamically linked. Qt license texts are retained in
`qt6/`. Corresponding Qt 6.8.3 source is available from
https://download.qt.io/archive/qt/6.8/6.8.3/single/ and
https://github.com/qt/qtbase/tree/v6.8.3 . Source references, build instructions
and the frontend sources are supplied with this project. Runtime DLLs may be
replaced with compatible builds; this application does not restrict debugging
or modification of those libraries.

The Microsoft Visual C++ runtime binaries are redistributable components copied
from the installed MSVC toolchain. Microsoft's license for those components
applies. The standard Windows system libraries and system fonts are not bundled.

Existing Python dependency license files and package metadata are retained under
`backend/vendor/*dist-info`. The frontend's GPLv3 license is in `../LICENSE`;
the reused helper modules' original MIT notice is in `rome-at-war-ai-MIT.txt`.
