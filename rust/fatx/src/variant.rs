/// Which console's flavour of FATX a filesystem is.
///
/// The two are the same filesystem, but the Xbox 360 stores every multi-byte
/// on-disk value big-endian, counts timestamps from 1980 rather than 2000, uses
/// the standard FAT widths for the hour and minute fields, and stores the date
/// before the time in each timestamp pair.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Variant {
    /// Detect from the partition signature.
    #[default]
    Auto,
    /// Original Xbox: little-endian on disk.
    Xbox,
    /// Xbox 360: big-endian on disk.
    X360,
}

impl Variant {
    /// Identify the variant from a raw, unswapped signature word.
    ///
    /// The signature is byte-identical in both flavours: the original Xbox
    /// writes the bytes 'F','A','T','X' and reads them little-endian, the 360
    /// writes 'X','T','A','F' and reads them big-endian, and both yield the
    /// same value. So whichever way round the raw word matches identifies the
    /// disk's byte order.
    pub fn from_raw_signature(raw: u32, signature: u32) -> Option<Self> {
        if raw == signature {
            Some(Variant::Xbox)
        } else if raw.swap_bytes() == signature {
            Some(Variant::X360)
        } else {
            None
        }
    }

    /// Whether values read off this disk need their bytes swapped.
    ///
    /// The on-disk structures are declared with little-endian field types, so
    /// this is true exactly when the disk is big-endian.
    pub fn needs_swap(&self) -> bool {
        matches!(self, Variant::X360)
    }

    /// The year timestamps on this filesystem count from.
    pub fn epoch(&self) -> u16 {
        match self {
            Variant::X360 => 1980,
            _ => 2000,
        }
    }
}

impl std::str::FromStr for Variant {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "auto" => Ok(Variant::Auto),
            "xbox" => Ok(Variant::Xbox),
            "x360" => Ok(Variant::X360),
            other => Err(format!(
                "unknown variant '{other}' (expected auto, xbox or x360)"
            )),
        }
    }
}
