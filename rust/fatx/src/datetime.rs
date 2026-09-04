use crate::variant::Variant;

pub struct Date {
    year: u16,
    /// 1 = January
    month: u8,
    day: u8,
}

impl Date {
    pub fn from_fatx_encoding(encoded: u16, variant: Variant) -> Self {
        Self {
            year: (((encoded >> 9) & 0x7f) + variant.epoch()),
            month: ((encoded >> 5) & 0xf) as u8,
            day: (encoded & 0x1f) as u8,
        }
    }

    pub fn year(&self) -> u16 {
        self.year
    }
    /// Returns the month, where 1 = January
    pub fn month(&self) -> u8 {
        self.month
    }
    pub fn day(&self) -> u8 {
        self.day
    }
}

pub struct Time {
    hour: u8,
    minute: u8,
    second: u8,
}

impl Time {
    /// The Xbox 360 uses the standard FAT field widths, 5 bits of hour and 6
    /// of minute. The original Xbox uses 4 and 5, which cannot represent an
    /// hour past 15 or a minute past 31.
    pub fn from_fatx_encoding(encoded: u16, variant: Variant) -> Self {
        let (hour_mask, minute_mask) = match variant {
            Variant::X360 => (0x1f, 0x3f),
            _ => (0xf, 0x1f),
        };
        Self {
            hour: ((encoded >> 11) & hour_mask) as u8,
            minute: ((encoded >> 5) & minute_mask) as u8,
            second: ((encoded & 0x1f) * 2) as u8,
        }
    }

    pub fn hour(&self) -> u8 {
        self.hour
    }
    pub fn minute(&self) -> u8 {
        self.minute
    }
    pub fn second(&self) -> u8 {
        self.second
    }
}

pub struct DateTime {
    date: Date,
    time: Time,
}

impl DateTime {
    pub fn from_fatx_encoding(date_encoded: u16, time_encoded: u16, variant: Variant) -> Self {
        Self {
            date: Date::from_fatx_encoding(date_encoded, variant),
            time: Time::from_fatx_encoding(time_encoded, variant),
        }
    }

    pub fn year(&self) -> u16 {
        self.date.year
    }
    /// Returns the month, where 1 = January
    pub fn month(&self) -> u8 {
        self.date.month
    }
    pub fn day(&self) -> u8 {
        self.date.day
    }
    pub fn hour(&self) -> u8 {
        self.time.hour
    }
    pub fn minute(&self) -> u8 {
        self.time.minute
    }
    pub fn second(&self) -> u8 {
        self.time.second
    }
}
