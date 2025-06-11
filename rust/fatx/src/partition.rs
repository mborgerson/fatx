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
];

impl PartitionMapEntry {
    pub fn from_letter(letter: &str) -> Option<&PartitionMapEntry> {
        DEFAULT_PARTITION_LAYOUT
            .iter()
            .find(|&entry| entry.letter == letter)
    }
}
