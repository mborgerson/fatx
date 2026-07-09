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

    pub fn to_fatx_encoding(&self) -> u16 {
        (self.year.saturating_sub(FATX_EPOCH).min(0x7f) << 9)
            | ((self.month as u16 & 0xf) << 5)
            | (self.day as u16 & 0x1f)
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
            // NOTE: hour occupies 5 bits (0-23) and minute 6 bits (0-59) in the
            // on-disk FAT time word. Earlier masks of 0xf / 0x1f silently
            // corrupted hours >= 16 and minutes >= 32.
            hour: ((encoded >> 11) & 0x1f) as u8,
            minute: ((encoded >> 5) & 0x3f) as u8,
            second: ((encoded & 0x1f) * 2) as u8,
        }
    }

    pub fn to_fatx_encoding(&self) -> u16 {
        ((self.hour as u16 & 0x1f) << 11)
            | ((self.minute as u16 & 0x3f) << 5)
            | ((self.second as u16 / 2) & 0x1f)
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

    /// (date_word, time_word) in on-disk encoding.
    pub fn to_fatx_encoding(&self) -> (u16, u16) {
        (self.date.to_fatx_encoding(), self.time.to_fatx_encoding())
    }

    /// Current UTC time (civil-from-days algorithm; no external deps).
    pub fn now() -> Self {
        let secs = std::time::SystemTime::now()
            .duration_since(std::time::SystemTime::UNIX_EPOCH)
            .map(|d| d.as_secs() as i64)
            .unwrap_or(0);
        let days = secs.div_euclid(86_400);
        let rem = secs.rem_euclid(86_400);
        // Howard Hinnant's civil_from_days.
        let z = days + 719_468;
        let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
        let doe = z - era * 146_097;
        let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
        let y = yoe + era * 400;
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        let mp = (5 * doy + 2) / 153;
        let d = (doy - (153 * mp + 2) / 5 + 1) as u8;
        let m = (if mp < 10 { mp + 3 } else { mp - 9 }) as u8;
        let year = (y + i64::from(m <= 2)) as u16;
        DateTime {
            date: Date {
                year: year.max(FATX_EPOCH),
                month: m,
                day: d,
            },
            time: Time {
                hour: (rem / 3600) as u8,
                minute: ((rem % 3600) / 60) as u8,
                second: (rem % 60) as u8,
            },
        }
    }
}
