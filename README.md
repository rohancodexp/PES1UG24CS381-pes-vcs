PES-VCS Lab Report
Author: Rohan M

SRN: PES1UG24CS381

Batch: 2024–2028 | Section G

Phase 1: Object Storage Foundation
In this phase, I implemented the content-addressable storage system. Data is stored by its SHA-256 hash to ensure integrity and deduplication.

📸 Screenshot 1A: Output of ./test_objects showing all tests passing.

📸 Screenshot 1B: Directory sharding in .pes/objects.

Phase 2: Tree Objects
I implemented tree serialization to represent directory structures. Trees map filenames to their respective blob hashes.

📸 Screenshot 2A: Output of ./test_tree showing all tests passing.

📸 Screenshot 2B: Hex dump (xxd) of a raw tree object.

Phase 3: Staging Area (Index)
The index acts as the "middleman" between the working directory and the repository. I implemented atomic writes to ensure the index is never corrupted.

📸 Screenshot 3A: Output of pes status showing staged files.

📸 Screenshot 3B: Text format of the .pes/index file.

Phase 4: Commits and History
The final phase ties trees together with metadata (author, timestamp, parent pointer) to create a traceable history.

📸 Screenshot 4A: Output of pes log showing the commit history.

📸 Screenshot 4B: Object store growth after multiple commits.

📸 Screenshot 4C: The reference chain from HEAD to branch.

Final Integration Test
📸 Output of ./test_sequence.sh

Phase 5: Branching and Checkout Analysis
Q5.1: To implement pes checkout <branch>, the .pes/HEAD must point to the new ref, and the working directory must be updated to match the target tree. This is complex because uncommitted local changes must be protected from being overwritten.

Q5.2: A "dirty" directory is detected by comparing file metadata (mtime/size) in the index against the disk. If a file is modified locally and differs from the target branch's tree, a conflict exists.

Q5.3: In "Detached HEAD," commits are made but no branch name points to them. If you move away, they become "orphaned." They can be recovered by finding their hashes in the logs and creating a new branch at that hash.

Phase 6: Garbage Collection Analysis
Q6.1: I would use a Mark-and-Sweep algorithm. Starting from all refs, I'd "mark" every reachable object in a Hash Set. Any object in .pes/objects not in the set is "swept" (deleted).

Q6.2: Concurrent GC is dangerous because a new commit might be writing an object that isn't linked to a tree yet; the GC might delete it as "unreachable." Git avoids this by only pruning objects older than a certain grace period (e.g., 2 weeks).