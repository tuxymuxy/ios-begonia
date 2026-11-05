# Native iOS storage

The system remains `/home/user/ios-begonia/system.apfs`, a read-only regular file
inside the existing postmarketOS ext4 filesystem. No GPT changes were made.

A separate `/home/user/ios-begonia/data.hfs` reserves **16 GiB** for writable
app containers, databases, settings, and caches. It is a fully allocated HFS+
journaled image, not a new physical partition. Preparation writes every byte
before formatting; sparse/unwritten extents are unsuitable for direct native I/O.
It is pinned with Linux's immutable inode flag; deliberately removing it later
requires clearing that flag first. Do not relocate or defragment either mapped
image without regenerating and verifying its extent map.

The optional XNU data device is `/dev/begonia-data`, selected by
`begonia_data=1 var_hfs=begonia-data` and an explicit private ADT extent map.
Without that option, the root disk stays read-only and `/private/var` uses RAM.
The data driver checks exact image size, full contiguous logical coverage,
physical bounds, self-overlap, and overlap with the system image. The SCSI
submission path independently restricts WRITE(10) to the data extents; partial
4 KiB writes use read/modify/write under the shared storage lock. Writes request
FUA, and cache synchronization requests issue SYNCHRONIZE CACHE.

Status: native v12 mounted the HFS+ image at `/private/var`, wrote the test
marker, and recovered to Linux. A read-only hfsfuse inspection then found the
marker on disk, confirming persistence across reboot. Extent-policy host tests
pass. Long migrations and reuse of the saved databases remain under test. The
bounded supervisor resets without a filesystem unmount; the subsequent Darwin
mount must replay the HFS+ journal. The read-only Linux inspection did not
modify or replay that journal.

The native kernel accesses these files' physical blocks only with Linux shut
down. Never run equivalent raw writes while postmarketOS has its ext4 root
mounted. Apple system assets, firmware, extent maps and device backups remain
local under ignored directories and are not published.
