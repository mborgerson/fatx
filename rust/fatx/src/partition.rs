pub struct PartitionMapEntry {
    pub letter: &'static str,
    pub offset_bytes: u64,
    pub size_bytes: u64,
}

pub const DEFAULT_PARTITION_LAYOUT: &[PartitionMapEntry] = &[
    PartitionMapEntry {
        letter: "x",
        offset_bytes: 0x00080000,
        size_bytes: 0x02ee00000,
    },
    PartitionMapEntry {
        letter: "y",
        offset_bytes: 0x2ee80000,
        size_bytes: 0x02ee00000,
    },
    PartitionMapEntry {
        letter: "z",
        offset_bytes: 0x5dc80000,
        size_bytes: 0x02ee00000,
    },
    PartitionMapEntry {
        letter: "c",
        offset_bytes: 0x8ca80000,
        size_bytes: 0x01f400000,
    },
    PartitionMapEntry {
        letter: "e",
        offset_bytes: 0xabe80000,
        size_bytes: 0x1312d6000,
    },
    // Extended (non-retail) partition commonly used in homebrew. Runs to the
    // end of the disk.
    PartitionMapEntry {
        letter: "f",
        offset_bytes: 0x1dd156000,
        size_bytes: u64::MAX,
    },
];

/// Xbox 360 partition map.
///
/// The 360 has no partition table and no drive letters: partitions live at
/// fixed offsets and are named. Only the FATX partitions are listed; the two
/// cache partitions that precede them are not FATX.
///
/// Note that 0x120eb0000, widely cited as the data partition, is in fact the
/// backwards compatibility partition. "data" is the user content partition.
pub const X360_PARTITION_LAYOUT: &[PartitionMapEntry] = &[
    PartitionMapEntry {
        letter: "sysext",
        offset_bytes: 0x10c080000,
        size_bytes: 0x0ce30000,
    },
    PartitionMapEntry {
        letter: "sysext2",
        offset_bytes: 0x118eb0000,
        size_bytes: 0x08000000,
    },
    PartitionMapEntry {
        letter: "compat",
        offset_bytes: 0x120eb0000,
        size_bytes: 0x10000000,
    },
    PartitionMapEntry {
        letter: "data",
        offset_bytes: 0x130eb0000,
        size_bytes: u64::MAX,
    },
];

impl PartitionMapEntry {
    pub fn from_letter(letter: &str) -> Option<&PartitionMapEntry> {
        DEFAULT_PARTITION_LAYOUT
            .iter()
            .find(|&entry| entry.letter == letter)
    }

    /// Look up an Xbox 360 partition by name.
    pub fn from_x360_name(name: &str) -> Option<&PartitionMapEntry> {
        X360_PARTITION_LAYOUT
            .iter()
            .find(|&entry| entry.letter == name)
    }
}
