# BloomForge v3 changelog

## Correctness and portability
- Versioned snapshot format bumped to v2.
- All serialized integral and floating-point fields are encoded in little-endian order.
- Snapshot loading validates FPP configuration and canonical trailing padding bits.
- Snapshot save has a coherent point-in-time contract: active writers finish before collection, and new writers wait until the snapshot copy completes.

## Concurrency contract
- Multiple writers may insert concurrently.
- Queries may race with insertion; a query concurrent with an insertion can return `false` before all of that key's bits are published. A completed insertion followed by a query observes the inserted key.
- Save is coordinated with writers, but does not turn a sequence of multiple inserts into a transaction.

## Deliberate limitations
- The built-in non-cryptographic hash is intended for trusted application keys, not attacker-controlled inputs.
- Snapshot CRC32 detects accidental corruption; it is not an authenticity mechanism.
- Atomic rename durability across power loss is filesystem/platform dependent.
- This library is not a distributed Bloom Filter and does not provide deletion.
