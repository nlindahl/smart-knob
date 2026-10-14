pub mod protocol;
use serde::{Deserialize, Serialize};

pub const VERSION: u8 = 1;
pub const MAX_VIEWS: usize = 8;
pub const MAX_WIDGETS: usize = 16;
pub const MAX_BUNDLE: usize = 24576;
pub const LED_COUNT: usize = 72;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum Mode {
    Free,
    Detent,
    Bounded,
    Magnetic,
    Switch,
}
impl Mode {
    pub const ALL: [Self; 5] = [
        Self::Free,
        Self::Detent,
        Self::Bounded,
        Self::Magnetic,
        Self::Switch,
    ];
    pub fn label(self) -> &'static str {
        match self {
            Self::Free => "Free spin",
            Self::Detent => "Detents",
            Self::Bounded => "Bounded",
            Self::Magnetic => "Magnetic",
            Self::Switch => "Switch",
        }
    }
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Haptics {
    pub mode: Mode,
    pub min: i32,
    pub max: i32,
    pub initial: i32,
    pub width_deg: f32,
    pub strength: f32,
    pub endstop: f32,
    pub snap: f32,
    pub damping: f32,
    pub detents: Vec<i32>,
}
impl Default for Haptics {
    fn default() -> Self {
        Self {
            mode: Mode::Detent,
            min: 0,
            max: 100,
            initial: 0,
            width_deg: 6.,
            strength: 0.65,
            endstop: 0.8,
            snap: 1.1,
            damping: 0.5,
            detents: vec![],
        }
    }
}
impl Haptics {
    pub fn preset(mode: Mode) -> Self {
        let mut h = Self {
            mode,
            ..Self::default()
        };
        match mode {
            Mode::Free => h.strength = 0.,
            Mode::Magnetic => h.detents = vec![0, 5, 10],
            Mode::Switch => {
                h.max = 1;
                h.width_deg = 60.;
                h.snap = 0.55;
            }
            _ => (),
        }
        h
    }
    pub fn validate(&self) -> Result<(), String> {
        for (name, v, lo, hi) in [
            ("width", self.width_deg, 1., 180.),
            ("strength", self.strength, 0., 1.),
            ("endstop", self.endstop, 0., 1.),
            ("snap", self.snap, 0.5, 1.5),
            ("damping", self.damping, 0., 1.),
        ] {
            if !v.is_finite() || v < lo || v > hi {
                return Err(format!("{name} must be between {lo} and {hi}"));
            }
        }
        if self.min < -1_000_000
            || self.max > 1_000_000
            || !(-1_000_000..=1_000_000).contains(&self.initial)
        {
            return Err("Position exceeds supported range".into());
        }
        if matches!(self.mode, Mode::Bounded | Mode::Switch)
            && (self.min > self.max || self.initial < self.min || self.initial > self.max)
        {
            return Err("Initial position must be within ordered bounds".into());
        }
        if self.mode == Mode::Switch && i64::from(self.max) - i64::from(self.min) != 1 {
            return Err("Switch needs exactly two positions".into());
        }
        if self.detents.len() > 16 || (self.mode == Mode::Magnetic && self.detents.is_empty()) {
            return Err("Magnetic mode needs 1–16 detent positions".into());
        }
        for (i, p) in self.detents.iter().enumerate() {
            if !(-1_000_000..=1_000_000).contains(p) || self.detents[..i].contains(p) {
                return Err("Detent positions must be unique and within range".into());
            }
        }
        Ok(())
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum LedMode {
    Off,
    Solid,
    Position,
    Progress,
    Rainbow,
}
impl LedMode {
    pub const ALL: [Self; 5] = [
        Self::Off,
        Self::Solid,
        Self::Position,
        Self::Progress,
        Self::Rainbow,
    ];
    pub fn label(self) -> &'static str {
        match self {
            Self::Off => "Off",
            Self::Solid => "Solid",
            Self::Position => "Position",
            Self::Progress => "Progress",
            Self::Rainbow => "Rainbow",
        }
    }
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Leds {
    pub mode: LedMode,
    pub brightness: u8,
    pub color: u32,
    pub accent: u32,
    pub offset: u8,
    pub reverse: bool,
    pub press_flash: bool,
    pub mode_flash: bool,
}
impl Default for Leds {
    fn default() -> Self {
        Self {
            mode: LedMode::Position,
            brightness: 64,
            color: 0x45d6b0,
            accent: 0x050a0d,
            offset: 0,
            reverse: false,
            press_flash: true,
            mode_flash: true,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum WidgetKind {
    Label,
    Value,
    Arc,
    Bar,
    Rect,
    Circle,
}
impl WidgetKind {
    pub const ALL: [Self; 6] = [
        Self::Label,
        Self::Value,
        Self::Arc,
        Self::Bar,
        Self::Rect,
        Self::Circle,
    ];
    pub fn label(self) -> &'static str {
        match self {
            Self::Label => "Label",
            Self::Value => "Value",
            Self::Arc => "Arc",
            Self::Bar => "Bar",
            Self::Rect => "Rectangle",
            Self::Circle => "Circle",
        }
    }
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Widget {
    pub id: u16,
    pub kind: WidgetKind,
    pub x: i16,
    pub y: i16,
    pub w: u16,
    pub h: u16,
    pub color: u32,
    pub font_size: u8,
    pub text: String,
}
impl Widget {
    pub fn new(id: u16, kind: WidgetKind) -> Self {
        Self {
            id,
            kind,
            x: 40,
            y: 100,
            w: 160,
            h: 40,
            color: 0xffffff,
            font_size: 24,
            text: if kind == WidgetKind::Label {
                "Label".into()
            } else {
                String::new()
            },
        }
    }
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct View {
    pub id: u16,
    pub name: String,
    pub background: u32,
    pub haptics: Haptics,
    pub leds: Leds,
    pub on_press: u16,
    pub on_long_press: u16,
    pub widgets: Vec<Widget>,
}
impl View {
    pub fn new(id: u16, name: &str) -> Self {
        Self {
            id,
            name: name.into(),
            background: 0x101820,
            haptics: Haptics::default(),
            leds: Leds::default(),
            on_press: 0,
            on_long_press: 0,
            widgets: vec![
                Widget {
                    id: 1,
                    kind: WidgetKind::Arc,
                    x: 20,
                    y: 20,
                    w: 200,
                    h: 200,
                    color: 0x45d6b0,
                    font_size: 16,
                    text: String::new(),
                },
                Widget {
                    id: 2,
                    kind: WidgetKind::Label,
                    x: 20,
                    y: 70,
                    w: 200,
                    h: 30,
                    color: 0xe4edf5,
                    font_size: 24,
                    text: name.into(),
                },
                Widget {
                    id: 3,
                    kind: WidgetKind::Value,
                    x: 45,
                    y: 110,
                    w: 150,
                    h: 45,
                    color: 0xffffff,
                    font_size: 32,
                    text: String::new(),
                },
            ],
        }
    }
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Project {
    pub version: u8,
    pub start_view: u16,
    pub views: Vec<View>,
}
fn text_valid(s: &str, max: usize) -> bool {
    s.len() <= max && s.bytes().all(|c| (32..=126).contains(&c))
}
impl Default for Project {
    fn default() -> Self {
        let presets = [
            ("Free spin", Mode::Free),
            ("Fine detents", Mode::Detent),
            ("Coarse detents", Mode::Detent),
            ("Bounded", Mode::Bounded),
            ("Magnetic", Mode::Magnetic),
            ("Switch", Mode::Switch),
        ];
        let views = presets
            .into_iter()
            .enumerate()
            .map(|(i, (name, mode))| {
                let mut v = View::new(i as u16 + 1, name);
                v.haptics = Haptics::preset(mode);
                if i == 2 {
                    v.haptics.width_deg = 30.;
                }
                v.on_press = if i == 5 { 1 } else { i as u16 + 2 };
                v.on_long_press = 1;
                v
            })
            .collect();
        Self {
            version: VERSION,
            start_view: 1,
            views,
        }
    }
}
impl Project {
    pub fn validate(&self) -> Result<(), String> {
        if self.version != VERSION {
            return Err("Unsupported project version".into());
        }
        if self.views.is_empty() || self.views.len() > MAX_VIEWS {
            return Err("Projects need 1–8 views".into());
        }
        let exists = |id| self.views.iter().any(|v| v.id == id);
        if !exists(self.start_view) {
            return Err("Start view does not exist".into());
        }
        for (i, v) in self.views.iter().enumerate() {
            if v.id == 0 || self.views[..i].iter().any(|p| p.id == v.id) {
                return Err("View IDs must be unique and nonzero".into());
            }
            if v.name.is_empty() || !text_valid(&v.name, 31) {
                return Err("View names need 1–31 printable ASCII characters".into());
            }
            v.haptics
                .validate()
                .map_err(|e| format!("{}: {e}", v.name))?;
            if v.background > 0xffffff
                || v.leds.color > 0xffffff
                || v.leds.accent > 0xffffff
                || v.leds.offset >= LED_COUNT as u8
            {
                return Err("Invalid color or LED offset".into());
            }
            if (v.on_press != 0 && !exists(v.on_press))
                || (v.on_long_press != 0 && !exists(v.on_long_press))
            {
                return Err("Navigation points to a missing view".into());
            }
            if v.widgets.len() > MAX_WIDGETS {
                return Err("Views support at most 16 widgets".into());
            }
            for (j, w) in v.widgets.iter().enumerate() {
                if w.id == 0 || v.widgets[..j].iter().any(|p| p.id == w.id) {
                    return Err("Widget IDs must be unique and nonzero".into());
                }
                if w.x < 0
                    || w.y < 0
                    || w.w == 0
                    || w.h == 0
                    || i32::from(w.x) + i32::from(w.w) > 240
                    || i32::from(w.y) + i32::from(w.h) > 240
                {
                    return Err("Widget must fit the 240×240 canvas".into());
                }
                if w.color > 0xffffff
                    || ![16, 24, 32].contains(&w.font_size)
                    || !text_valid(&w.text, 63)
                {
                    return Err("Invalid widget color, font, or text (63 printable ASCII characters maximum)".into());
                }
                if w.kind == WidgetKind::Circle && w.w != w.h {
                    return Err("Circle width and height must match".into());
                }
            }
        }
        Ok(())
    }
    pub fn bundle(&self) -> Result<Vec<u8>, String> {
        self.validate()?;
        let mut e = minicbor::Encoder::new(Vec::new());
        let err = |e: minicbor::encode::Error<std::convert::Infallible>| e.to_string();
        e.array(3)
            .map_err(err)?
            .u8(VERSION)
            .map_err(err)?
            .u16(self.start_view)
            .map_err(err)?
            .array(self.views.len() as u64)
            .map_err(err)?;
        for v in &self.views {
            e.array(8)
                .map_err(err)?
                .u16(v.id)
                .map_err(err)?
                .str(&v.name)
                .map_err(err)?
                .u32(v.background)
                .map_err(err)?;
            let h = &v.haptics;
            e.array(10)
                .map_err(err)?
                .u8(h.mode as u8)
                .map_err(err)?
                .i32(h.min)
                .map_err(err)?
                .i32(h.max)
                .map_err(err)?
                .i32(h.initial)
                .map_err(err)?;
            for n in [h.width_deg, h.strength, h.endstop, h.snap, h.damping] {
                e.f32(n).map_err(err)?;
            }
            e.array(h.detents.len() as u64).map_err(err)?;
            for n in &h.detents {
                e.i32(*n).map_err(err)?;
            }
            let l = &v.leds;
            e.array(8)
                .map_err(err)?
                .u8(l.mode as u8)
                .map_err(err)?
                .u8(l.brightness)
                .map_err(err)?
                .u32(l.color)
                .map_err(err)?
                .u32(l.accent)
                .map_err(err)?
                .u8(l.offset)
                .map_err(err)?
                .bool(l.reverse)
                .map_err(err)?
                .bool(l.press_flash)
                .map_err(err)?
                .bool(l.mode_flash)
                .map_err(err)?;
            e.u16(v.on_press)
                .map_err(err)?
                .u16(v.on_long_press)
                .map_err(err)?
                .array(v.widgets.len() as u64)
                .map_err(err)?;
            for w in &v.widgets {
                e.array(9)
                    .map_err(err)?
                    .u16(w.id)
                    .map_err(err)?
                    .u8(w.kind as u8)
                    .map_err(err)?
                    .i16(w.x)
                    .map_err(err)?
                    .i16(w.y)
                    .map_err(err)?
                    .u16(w.w)
                    .map_err(err)?
                    .u16(w.h)
                    .map_err(err)?
                    .u32(w.color)
                    .map_err(err)?
                    .u8(w.font_size)
                    .map_err(err)?
                    .str(&w.text)
                    .map_err(err)?;
            }
        }
        let payload = e.into_writer();
        let mut bundle = b"SKV1".to_vec();
        bundle.extend_from_slice(&(payload.len() as u32).to_le_bytes());
        bundle.extend_from_slice(&crc32(&payload).to_le_bytes());
        bundle.extend(payload);
        if bundle.len() > MAX_BUNDLE {
            return Err("Bundle exceeds device capacity".into());
        }
        Ok(bundle)
    }
}
pub fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for b in bytes {
        crc ^= u32::from(*b);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb88320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn default_roundtrip_and_bundle() {
        let p = Project::default();
        p.validate().unwrap();
        assert_eq!(
            p,
            serde_json::from_str(&serde_json::to_string(&p).unwrap()).unwrap()
        );
        let b = p.bundle().unwrap();
        assert_eq!(&b[..4], b"SKV1");
        assert_eq!(
            crc32(&b[12..]),
            u32::from_le_bytes(b[8..12].try_into().unwrap())
        );
    }
    #[test]
    fn reject_dangling_links_and_nan() {
        let mut p = Project::default();
        p.views[0].on_press = 100;
        assert!(p.validate().is_err());
        p.views[0].on_press = 0;
        p.views[0].haptics.width_deg = f32::NAN;
        assert!(p.bundle().is_err());
    }
    #[test]
    fn reject_overflowing_switch_bounds() {
        let h = Haptics {
            mode: Mode::Switch,
            min: -1_000_000,
            max: i32::MIN,
            ..Haptics::default()
        };
        assert!(h.validate().is_err());
    }
    #[test]
    fn crc_vector() {
        assert_eq!(crc32(b"123456789"), 0xcbf43926);
    }
}
