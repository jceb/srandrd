# Change Log
All notable changes to this project will be documented in this file.
This project adheres to [Semantic Versioning](http://semver.org/).

## [UNRELEASED]
### Added
- Explain the content of the environment variables
- Implement list command to list outputs and EDIDs

### Changed
- Cache and send EDID and XINERAMA_SCREENID on disconnect
- Separate `SRANDRD_ACTION` into `SRANDRD_OUTPUT` and `SRANDRD_EVENT`

### Fixed
- Fix a number of linter warnings

## [0.5.0] - 2016-06-05
### Added
- EDID variable provided in `SRANDRD_ACTION`
- Xinerama screen id provided in `SRANDRD_SCREENID`
