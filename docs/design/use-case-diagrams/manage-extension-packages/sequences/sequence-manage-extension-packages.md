# Sequence: Extensions to Installed Index
```mermaid
sequenceDiagram
 actor U as user
  participant UI as Extensions
  participant Repo as Package Repository
  participant Access as Wi-Fi Access
  participant HTTP as HTTP Client
  participant Store as Package Storage
  participant Index as Installed Index
  U->>UI: Install/Update
  UI->>Repo: start_install_package(record)
  Repo->>Access: acquire(HttpDownload)
  Repo->>HTTP: download archive
  Repo->>Repo: SHA-256 + safe ZIP validation
  Repo->>Store: extract temp payload
  Repo->>Store: verify payload visible
  Repo->>Index: atomic save installed record
  Index-->>Repo: saved / failed
  Repo-->>UI: Succeeded / Failed + progress
```

## Scenarios and responsibilities

UI only submits package record; Repository orchestrates compatibility and installation transactions; Access provides network lease; HTTP downloads temporary archives; Package Storage manages isolation payload; Installed Index is the submission point for visible versions.

## Sequence constraints

Compatibility check and resource budget are completed before acquire/download. Decompression is allowed only after the download is completed and SHA-256 is verified. Safe ZIP verification and actual extract use the same normalized path rules to avoid "check passing, escape while writing".

## Submit and rollback

After Store verifies that the payload is readable, Index atomically saves the installed record. Succeeded is issued only if Index succeeds. Restore the previous index in case of failure and ensure that new payloads are not discovered; temporary cleanup failure is used as a maintenance diagnosis and does not change the business results.

## Cancel and retry

Cancellation invalidates the download/extract generation and releases the Lease. Retries can reuse explicitly verified caches, but not incomplete archives. Update retains previous until a new version is submitted to avoid intermediate windows with no available extensions.

## Testing

Covering Lease rejection, short download, hash error, path traversal, payload invisible, Index failure, late callback cancellation and previous recovery.
