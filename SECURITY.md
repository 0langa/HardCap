# Security Policy

## Supported Versions

Security fixes target the latest released version.

## Reporting a Vulnerability

Please report security issues privately by emailing the maintainer listed on the GitHub profile for this repository. Include:

- A description of the issue and impact.
- Steps to reproduce or a minimal proof of concept.
- Affected Windows version and HardCap version.

Please do not file public issues for vulnerabilities until a fix is available.

## Scope

HardCap runs with administrator privileges so it can inspect and assign processes to Windows Job Objects. Security-sensitive areas include process selection, saved rule parsing, unelevated launches, Job Object assignment, and cleanup of active limits.
