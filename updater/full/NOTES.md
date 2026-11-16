# Full EX-word UPDADN2 experiment

This build embeds the **entire, unchanged, gint-linked Explorer executable** inside one `UPDADN2.BIN`. It is not the small read-only demo and not a native firmware update. It includes the usual Explorer, USB remote, native EX-word filesystem operations, CJF and bitmap previews, and the normal gint drivers.

The updater entry is physical P1 address `0x8c190000`. The file begins with the official 128-byte CY10x updater header. A 4 KiB bootstrap page follows it. The app's loadable ROM and data bytes begin at `0x8c191080`. The bootstrap maps this physical page at virtual `0x70000000`, so that the existing gint entry can execute at `0x70000080`.

Before writing any TLB entries, the bootstrap checks that translation is enabled, that eight contiguous 4 KiB pages of user RAM are mapped at `0x74000000`, and that there are enough completely free UTLB entries to map the app. It does not overwrite active OS mappings. If a check fails, the LCD turns solid red and the bootstrap halts without running Explorer. After Explorer returns, the bootstrap removes the mappings it added and turns the LCD blue.

This relies on the particular DATAPLUS OS memory and service environment that gint-exword was written for. The bootloader is **not proven to work from EX-word E Test Menu**. In that environment the OS dispatcher or the expected RAM mapping may not exist. Even a passing header check or successful cross-compile does not establish that the file is safe to use on hardware. The Test Menu can flash firmware before handing over control, even if the payload does not request flashing.

The full image contains **file writing and deletion functionality**. Unlike the smaller `updater/boot.c` image, it is **not read-only**.

The build is generated in CI with the normal Explorer ELF plus the freestanding bootstrap in `updater/full/`. The Python packer verifies the official header, physical entry, ELF entry, app size, alignment, metadata and payload hash. The runtime bootstrap checks the metadata and makes a conservative MMU mapping attempt.

Do **not** deploy this image to a dictionary without a complete backup and a proven recovery path. Hardware boot compatibility, USB state restoration, native syscalls and interrupt handling remain unverified outside the normal add-in loader.
