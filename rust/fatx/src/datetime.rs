const FATX_EPOCH: u16 = 2000;
pub struct Date {
    year: u16,
    /// 1 = January
    month: u8,
    day: u8,
}

impl Date {
    pub fn from_fatx_encoding(encoded: u16) -> Self {
        Self {
            year: (((encoded >> 9) & 0x7f) + FATX_EPOCH),
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
    pub fn from_fatx_encoding(encoded: u16) -> Self {
        Self {
            hour: ((encoded >> 11) & 0xf) as u8,
            minute: ((encoded >> 5) & 0x1f) as u8,
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
    pub fn from_fatx_encoding(date_encoded: u16, time_encoded: u16) -> Self {
        Self {
            date: Date::from_fatx_encoding(date_encoded),
            time: Time::from_fatx_encoding(time_encoded),
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
