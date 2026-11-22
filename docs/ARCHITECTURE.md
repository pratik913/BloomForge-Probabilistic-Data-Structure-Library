# BloomForge architecture

## Membership model

A Bloom filter stores no original keys. Each key is mapped to `k` positions in an `m`-bit vector. Insertion sets all positions. Lookup returns absent as soon as one bit is zero; otherwise it returns possibly present.

## Sizing

For expected item count `n` and target false-positive probability `p`:

- `m = ceil(-n * ln(p) / (ln(2)^2))`
- `k = round((m / n) * ln(2))`, clamped to 1..64

The implementation rounds the allocation to 64-bit words. The configured FPP is a target around the configured capacity, not a guarantee after unlimited insertions.

## Hash strategy

Two 64-bit hashes are computed once per operation. Double hashing derives probe `i` as `(h1 + i*h2) mod m`. The second hash is forced odd to avoid a zero step. Hashes are non-cryptographic and are not intended to resist chosen-input attacks.

## Concurrency

Each storage word is an atomic 64-bit integer. Writers use `fetch_or`; readers use atomic loads. This avoids locks and prevents concurrent inserts from overwriting each other's bits. Statistics are relaxed atomics because they are observational counters, not synchronization primitives.

## Persistence

Snapshot fields: magic, format version, expected capacity, target FPP, bit count, hash count, packed words, CRC-32. Loading validates the header, dimensions, exact length, and checksum before publishing the filter object.

## Operational guidance

Choose capacity from peak expected unique keys for the filter's lifetime. Monitor bit utilization and estimated FPP. Rebuild into a larger filter when occupancy/FPP rises beyond the application's acceptable threshold. Always use the authoritative datastore to confirm positive membership results.


## v3 snapshot coordination

Writers acquire shared access to a snapshot gate before setting atomic bits. This preserves parallel insertions while allowing `save()` to acquire exclusive access and capture all words without an insertion changing the image halfway through. Queries remain lock-free with respect to this gate and may observe an insertion in progress. The snapshot format encodes fields explicitly in little-endian order and validates checksum, dimensions, configuration, and trailing padding bits on load.
