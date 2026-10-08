# Third-party components

- Qt 6 Widgets is dynamically linked. Qt's LGPL/GPL/commercial licensing applies
  to the selected distribution. The Qt SDK is a build dependency outside these
  sources. Installed runtime libraries and notices must accompany redistributed
  builds. See https://www.qt.io/licensing/ and https://doc.qt.io/qt-6/lgpl.html.
- `backend/vendor/mgz` is the existing local mgz 1.8.51 fork. Fork source:
  https://github.com/Kjir/aoc-mgz/ at commit
  `b4a30d8539c2fed4cbfc7b8cfec874e65cdc50a2`. Upstream:
  https://github.com/happyleavesaoc/aoc-mgz/ (MIT). Its original license is retained
  under `backend/vendor/mgz-1.8.51.dist-info/licenses/LICENSE`.
- Construct 2.8.16, aocref 2.0.38 and tabulate 0.10.0 are copied with their
  existing distribution metadata and available licenses from the same parser
  environment. They are dependencies of the existing parser, not new decoders.
- `backend/vendor_helpers` contains unchanged copies from Rome-at-War-AI,
  revision `07da2dee659b7422dc0dce862def8a93bda60a37`, under the repository MIT
  license retained in `licenses/rome-at-war-ai-MIT.txt`. No AI-specific
  episode/source analyzer is called. The standalone application's GPLv3 license
  is in `LICENSE`.

Local modifications to the bundled parser were already present in the source
parser environment. The adapter fingerprints its complete Python/reference
content rather than treating the upstream package version as sufficient identity.
