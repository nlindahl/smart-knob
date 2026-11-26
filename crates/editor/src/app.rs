use crate::theme;
use egui::{Color32, Pos2, Rect, RichText, Sense, Vec2};
use view_model::{
    Haptics, LedMode, MAX_VIEWS, MAX_WIDGETS, Mode, Project, View, Widget, WidgetKind,
};

pub struct Editor {
    pub project: Project,
    view: usize,
    selected: Option<usize>,
    undo: Vec<Project>,
    redo: Vec<Project>,
    angle_deg: f32,
    position: i32,
    sub: f32,
    voltage: f32,
    flash_until: u32,
    texture: Option<egui::TextureHandle>,
    last_bundle: Vec<u8>,
    ring: Vec<u8>,
    message: String,
    drag_origin: Option<(i16, i16)>,
    detent_text: String,
    saved: String,
    /// Mirror edits to the connected device and follow its state.
    #[cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]
    live: bool,
    /// Live sync is on and a device is connected (updated every frame).
    live_active: bool,
    /// Last view reported by the device, to follow only its own changes.
    #[cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]
    device_view: u16,
    /// Last view the device was asked to show.
    #[cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]
    synced_view: u16,
}
fn color(c: u32) -> Color32 {
    Color32::from_rgb((c >> 16) as u8, (c >> 8) as u8, c as u8)
}
fn color_edit(ui: &mut egui::Ui, label: &str, c: &mut u32) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(label).color(theme::MUTED));
        let mut rgb = [(*c >> 16) as u8, (*c >> 8) as u8, *c as u8];
        if ui.color_edit_button_srgb(&mut rgb).changed() {
            *c = (u32::from(rgb[0]) << 16) | (u32::from(rgb[1]) << 8) | u32::from(rgb[2]);
        }
    });
}
impl Default for Editor {
    fn default() -> Self {
        let project = Project::default();
        Self {
            saved: serde_json::to_string(&project).unwrap(),
            project,
            view: 0,
            selected: None,
            undo: vec![],
            redo: vec![],
            angle_deg: 0.,
            position: 0,
            sub: 0.,
            voltage: 0.,
            flash_until: 0,
            texture: None,
            last_bundle: vec![],
            ring: vec![],
            message: String::new(),
            drag_origin: None,
            detent_text: String::new(),
            live: true,
            live_active: false,
            device_view: 0,
            synced_view: 0,
        }
    }
}
impl Editor {
    pub fn new(ctx: &egui::Context) -> Self {
        theme::apply(ctx);
        let mut app = Self::default();
        #[cfg(target_arch = "wasm32")]
        if let Some(storage) = web_sys::window().and_then(|w| w.local_storage().ok().flatten())
            && let Ok(Some(json)) = storage.get_item("smartknob.project.v1")
            && let Ok(p) = serde_json::from_str::<Project>(&json)
            && p.validate().is_ok()
        {
            app.project = p;
            app.saved = json;
        }
        app.detent_text = app.project.views[0]
            .haptics
            .detents
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join(", ");
        app
    }
    fn checkpoint(&mut self, old: Project) {
        if old != self.project {
            if self.undo.len() >= 100 {
                self.undo.remove(0);
            }
            self.undo.push(old);
            self.redo.clear();
        }
    }
    fn reset_selection(&mut self) {
        self.view = self.view.min(self.project.views.len().saturating_sub(1));
        self.selected = None;
        self.drag_origin = None;
        self.angle_deg = 0.;
        self.last_bundle.clear();
        #[cfg(target_arch = "wasm32")]
        super::browser::reset();
        self.detent_text = self.project.views[self.view]
            .haptics
            .detents
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join(", ");
    }
    fn navigate(&mut self, long: bool, ms: u32) {
        let view = &self.project.views[self.view];
        if view.leds.press_flash {
            self.flash_until = ms + 180;
        }
        #[cfg(target_arch = "wasm32")]
        if let Ok(b) = self.project.bundle() {
            super::browser::simulate(&b, view.id, self.angle_deg.to_radians(), true, long);
        }
        let next = if long {
            view.on_long_press
        } else {
            view.on_press
        };
        if let Some(i) = self.project.views.iter().position(|v| v.id == next) {
            self.view = i;
            self.reset_selection();
            if self.project.views[i].leds.mode_flash {
                self.flash_until = ms + 180;
            }
        }
    }
    pub fn ui(&mut self, root: &mut egui::Ui) {
        let ctx = root.ctx().clone();
        let ms = (ctx.input(|i| i.time) * 1000.) as u32;
        ctx.request_repaint_after(std::time::Duration::from_millis(33));
        #[cfg(target_arch = "wasm32")]
        {
            let json = super::browser::take_import();
            if !json.is_empty() {
                match serde_json::from_str::<Project>(&json)
                    .map_err(|e| e.to_string())
                    .and_then(|p| {
                        p.validate()?;
                        Ok(p)
                    }) {
                    Ok(p) => {
                        let old = std::mem::replace(&mut self.project, p);
                        self.checkpoint(old);
                        self.reset_selection();
                        self.message = "Project imported".into();
                    }
                    Err(e) => self.message = e,
                }
            }
        }
        #[cfg(target_arch = "wasm32")]
        {
            self.live_active = self.live && super::browser::connected();
        }
        let before = self.project.clone();
        let mut history_action = false;
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::COMMAND, egui::Key::Z))
            && let Some(p) = self.undo.pop()
        {
            self.redo.push(std::mem::replace(&mut self.project, p));
            self.reset_selection();
            history_action = true;
        }
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::COMMAND, egui::Key::Y))
            && let Some(p) = self.redo.pop()
        {
            self.undo.push(std::mem::replace(&mut self.project, p));
            self.reset_selection();
            history_action = true;
        }
        egui::Panel::top("toolbar")
            .frame(theme::panel_frame().inner_margin(egui::Margin::symmetric(16, 10)))
            .show(root, |ui| {
                ui.horizontal(|ui| {
                    theme::logo(ui, 30.);
                    ui.label(
                        RichText::new("SmartKnob")
                            .size(20.)
                            .strong()
                            .color(theme::TEXT),
                    );
                    ui.label(RichText::new("Studio").size(20.).color(theme::ACCENT));
                    ui.add_space(18.);
                    if ui.button("New").clicked() {
                        self.project = Project::default();
                        self.reset_selection();
                    }
                    if ui.button("Import JSON").clicked() {
                        #[cfg(target_arch = "wasm32")]
                        super::browser::import_project();
                    }
                    if ui.button("Export JSON").clicked() {
                        #[cfg(target_arch = "wasm32")]
                        super::browser::download(
                            "smartknob.json",
                            serde_json::to_string_pretty(&self.project)
                                .unwrap()
                                .as_bytes(),
                            "application/json",
                        );
                    }
                    if ui.button("Export bundle").clicked() {
                        match self.project.bundle() {
                            Ok(_b) => {
                                #[cfg(target_arch = "wasm32")]
                                super::browser::download(
                                    "smartknob.skv",
                                    &_b,
                                    "application/octet-stream",
                                );
                                self.message = "Bundle exported".into();
                            }
                            Err(e) => self.message = e,
                        }
                    }
                    ui.add_space(8.);
                    if ui
                        .add_enabled(!self.undo.is_empty(), egui::Button::new("Undo"))
                        .clicked()
                    {
                        let p = self.undo.pop().unwrap();
                        self.redo.push(std::mem::replace(&mut self.project, p));
                        self.reset_selection();
                        history_action = true;
                    }
                    if ui
                        .add_enabled(!self.redo.is_empty(), egui::Button::new("Redo"))
                        .clicked()
                    {
                        let p = self.redo.pop().unwrap();
                        self.undo.push(std::mem::replace(&mut self.project, p));
                        self.reset_selection();
                        history_action = true;
                    }
                    if self.live_active {
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            theme::pill(ui, "Live on device", theme::ACCENT);
                        });
                    }
                });
                // Accent line under the header.
                let r = ui.max_rect();
                let line = Rect::from_min_max(
                    Pos2::new(r.left() - 16., r.bottom() + 9.),
                    Pos2::new(r.right() + 16., r.bottom() + 11.),
                );
                theme::gradient(ui.painter(), line, theme::ACCENT, theme::ACCENT_2);
            });
        egui::Panel::left("views")
            .default_size(236.)
            .resizable(true)
            .frame(theme::panel_frame())
            .show(root, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    theme::section(
                        ui,
                        "Views",
                        Some(format!("{}/{}", self.project.views.len(), MAX_VIEWS)),
                    );
                    let mut choice = self.view;
                    for (i, v) in self.project.views.iter().enumerate() {
                        if view_row(ui, v, i == self.view, v.id == self.project.start_view)
                            .clicked()
                        {
                            choice = i;
                        }
                    }
                    if choice != self.view {
                        self.view = choice;
                        self.reset_selection();
                        if self.project.views[choice].leds.mode_flash {
                            self.flash_until = ms + 180;
                        }
                    }
                    ui.add_space(4.);
                    ui.horizontal_wrapped(|ui| {
                        if ui
                            .add_enabled(
                                self.project.views.len() < MAX_VIEWS,
                                egui::Button::new("+ Add"),
                            )
                            .clicked()
                        {
                            let id = (1..=u16::MAX)
                                .find(|id| self.project.views.iter().all(|v| v.id != *id))
                                .unwrap();
                            self.project.views.push(View::new(id, "New view"));
                            self.view = self.project.views.len() - 1;
                            self.reset_selection();
                        }
                        if ui
                            .add_enabled(
                                self.project.views.len() < MAX_VIEWS,
                                egui::Button::new("Duplicate"),
                            )
                            .clicked()
                        {
                            let mut v = self.project.views[self.view].clone();
                            v.id = (1..=u16::MAX)
                                .find(|id| self.project.views.iter().all(|v| v.id != *id))
                                .unwrap();
                            self.project.views.push(v);
                            self.view = self.project.views.len() - 1;
                            self.reset_selection();
                        }
                        if theme::danger(ui, self.project.views.len() > 1, "Delete").clicked() {
                            let id = self.project.views.remove(self.view).id;
                            let first = self.project.views[0].id;
                            if self.project.start_view == id {
                                self.project.start_view = first;
                            }
                            for v in &mut self.project.views {
                                if v.on_press == id {
                                    v.on_press = 0;
                                }
                                if v.on_long_press == id {
                                    v.on_long_press = 0;
                                }
                            }
                            self.reset_selection();
                        }
                    });
                    let is_start = self.project.views[self.view].id == self.project.start_view;
                    if ui
                        .add_enabled(!is_start, egui::Button::new("★ Use as start view"))
                        .clicked()
                    {
                        self.project.start_view = self.project.views[self.view].id;
                    }
                    ui.add_space(14.);
                    theme::section(
                        ui,
                        "Widgets",
                        Some(format!(
                            "{}/{}",
                            self.project.views[self.view].widgets.len(),
                            MAX_WIDGETS
                        )),
                    );
                    for (i, w) in self.project.views[self.view].widgets.iter().enumerate() {
                        if widget_row(ui, w, self.selected == Some(i)).clicked() {
                            self.selected = Some(i);
                        }
                    }
                    egui::ComboBox::from_id_salt("add_widget")
                        .selected_text("+ Add widget…")
                        .show_ui(ui, |ui| {
                            for kind in WidgetKind::ALL {
                                if ui
                                    .add_enabled(
                                        self.project.views[self.view].widgets.len() < MAX_WIDGETS,
                                        egui::Button::new(kind.label()),
                                    )
                                    .clicked()
                                {
                                    let ws = &mut self.project.views[self.view].widgets;
                                    let id = (1..=u16::MAX)
                                        .find(|id| ws.iter().all(|w| w.id != *id))
                                        .unwrap();
                                    let mut w = Widget::new(id, kind);
                                    if kind == WidgetKind::Circle {
                                        w.w = 40;
                                    }
                                    if kind == WidgetKind::Arc {
                                        w.x = 20;
                                        w.y = 20;
                                        w.w = 200;
                                        w.h = 200;
                                    }
                                    ws.push(w);
                                    self.selected = Some(ws.len() - 1);
                                    ui.close();
                                }
                            }
                        });
                    if let Some(i) = self.selected {
                        ui.horizontal(|ui| {
                            if theme::danger(ui, true, "Delete").clicked() {
                                self.project.views[self.view].widgets.remove(i);
                                self.selected = None;
                            }
                            if ui.add_enabled(i > 0, egui::Button::new("Back")).clicked() {
                                self.project.views[self.view].widgets.swap(i, i - 1);
                                self.selected = Some(i - 1);
                            }
                            if ui
                                .add_enabled(
                                    i + 1 < self.project.views[self.view].widgets.len(),
                                    egui::Button::new("Front"),
                                )
                                .clicked()
                            {
                                self.project.views[self.view].widgets.swap(i, i + 1);
                                self.selected = Some(i + 1);
                            }
                        });
                    }
                });
            });
        egui::Panel::right("properties")
            .default_size(320.)
            .frame(theme::panel_frame())
            .show(root, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    let choices: Vec<_> = self
                        .project
                        .views
                        .iter()
                        .map(|v| (v.id, v.name.clone()))
                        .collect();
                    let v = &mut self.project.views[self.view];
                    theme::card(ui, |ui| {
                        theme::section(ui, "View", Some(format!("ID {}", v.id)));
                        ui.add(
                            egui::TextEdit::singleline(&mut v.name)
                                .hint_text("View name")
                                .desired_width(f32::INFINITY),
                        );
                        color_edit(ui, "Background", &mut v.background);
                        for (label, target) in [
                            ("Short press", &mut v.on_press),
                            ("Long press", &mut v.on_long_press),
                        ] {
                            ui.horizontal(|ui| {
                                ui.label(RichText::new(label).color(theme::MUTED));
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt(label)
                                            .width(150.)
                                            .selected_text(
                                                choices
                                                    .iter()
                                                    .find(|c| c.0 == *target)
                                                    .map(|c| c.1.as_str())
                                                    .unwrap_or("Stay")
                                                    .to_owned(),
                                            )
                                            .show_ui(ui, |ui| {
                                                ui.selectable_value(target, 0, "Stay");
                                                for (id, name) in &choices {
                                                    ui.selectable_value(target, *id, name);
                                                }
                                            });
                                    },
                                );
                            });
                        }
                    });
                    ui.add_space(6.);
                    theme::card(ui, |ui| {
                        egui::CollapsingHeader::new(
                            RichText::new("Motor feedback").strong().color(theme::TEXT),
                        )
                        .default_open(true)
                        .show(ui, |ui| {
                            let h = &mut v.haptics;
                            let old = h.mode;
                            egui::ComboBox::from_id_salt("haptic_mode")
                                .selected_text(h.mode.label())
                                .show_ui(ui, |ui| {
                                    for mode in Mode::ALL {
                                        ui.selectable_value(&mut h.mode, mode, mode.label());
                                    }
                                });
                            if old != h.mode {
                                *h = Haptics::preset(h.mode);
                                self.detent_text = h
                                    .detents
                                    .iter()
                                    .map(ToString::to_string)
                                    .collect::<Vec<_>>()
                                    .join(", ");
                            }
                            ui.horizontal(|ui| {
                                ui.label(RichText::new("Presets").color(theme::MUTED));
                                if ui.button("Fine").clicked() {
                                    *h = Haptics::default();
                                }
                                if ui.button("Coarse").clicked() {
                                    *h = Haptics {
                                        width_deg: 30.,
                                        ..Haptics::default()
                                    };
                                }
                            });
                            ui.add(
                                egui::Slider::new(&mut h.width_deg, 1.0..=180.).text("Spacing °"),
                            );
                            ui.add(
                                egui::Slider::new(&mut h.strength, 0.0..=1.)
                                    .text("Strength")
                                    .fixed_decimals(2),
                            );
                            ui.add(
                                egui::Slider::new(&mut h.endstop, 0.0..=1.)
                                    .text("End stops")
                                    .fixed_decimals(2),
                            );
                            ui.add(
                                egui::Slider::new(&mut h.snap, 0.5..=1.5)
                                    .text("Snap")
                                    .fixed_decimals(2),
                            );
                            ui.add(
                                egui::Slider::new(&mut h.damping, 0.0..=1.)
                                    .text("Damping")
                                    .fixed_decimals(2),
                            );
                            ui.add(
                                egui::Slider::new(&mut h.click, 0.0..=1.)
                                    .text("Click")
                                    .fixed_decimals(2),
                            )
                            .on_hover_text(
                                "Tick played on each detent (1.0 = 2.5 V). Independent of strength.",
                            );
                            ui.horizontal(|ui| {
                                ui.label(RichText::new("Initial").color(theme::MUTED));
                                ui.add(
                                    egui::DragValue::new(&mut h.initial)
                                        .range(-1_000_000..=1_000_000),
                                );
                            });
                            if matches!(h.mode, Mode::Bounded | Mode::Switch) {
                                ui.horizontal(|ui| {
                                    ui.label(RichText::new("Min").color(theme::MUTED));
                                    ui.add(
                                        egui::DragValue::new(&mut h.min)
                                            .range(-1_000_000..=1_000_000),
                                    );
                                    ui.label(RichText::new("Max").color(theme::MUTED));
                                    ui.add(
                                        egui::DragValue::new(&mut h.max)
                                            .range(-1_000_000..=1_000_000),
                                    );
                                });
                            }
                            if h.mode == Mode::Magnetic {
                                ui.label(
                                    RichText::new("Detent positions (comma separated)")
                                        .color(theme::MUTED),
                                );
                                if ui.text_edit_singleline(&mut self.detent_text).changed() {
                                    match self
                                        .detent_text
                                        .split(',')
                                        .map(|s| s.trim().parse::<i32>())
                                        .collect::<Result<Vec<_>, _>>()
                                    {
                                        Ok(ds) => {
                                            h.detents = ds;
                                            self.message.clear();
                                        }
                                        Err(_) => {
                                            self.message =
                                                "Enter integer positions separated by commas".into()
                                        }
                                    }
                                }
                            }
                        });
                    });
                    ui.add_space(6.);
                    theme::card(ui, |ui| {
                        egui::CollapsingHeader::new(
                            RichText::new("72-LED ring").strong().color(theme::TEXT),
                        )
                        .show(ui, |ui| {
                            let l = &mut v.leds;
                            egui::ComboBox::from_id_salt("led_mode")
                                .selected_text(l.mode.label())
                                .show_ui(ui, |ui| {
                                    for mode in LedMode::ALL {
                                        ui.selectable_value(&mut l.mode, mode, mode.label());
                                    }
                                });
                            ui.add(
                                egui::Slider::new(&mut l.brightness, 0..=255).text("Brightness"),
                            );
                            color_edit(ui, "Main", &mut l.color);
                            color_edit(ui, "Accent / flash", &mut l.accent);
                            ui.add(egui::Slider::new(&mut l.offset, 0..=71).text("Ring offset"));
                            ui.checkbox(&mut l.reverse, "Reverse direction");
                            ui.checkbox(&mut l.press_flash, "Flash on press");
                            ui.checkbox(&mut l.mode_flash, "Flash on view change");
                            ui.label(
                                RichText::new("Brightness is limited by a device current budget.")
                                    .small()
                                    .color(theme::MUTED),
                            );
                        });
                    });
                    if let Some(i) = self.selected
                        && let Some(w) = v.widgets.get_mut(i)
                    {
                        ui.add_space(6.);
                        theme::card(ui, |ui| {
                            theme::section(ui, w.kind.label(), Some(format!("ID {}", w.id)));
                            ui.horizontal(|ui| {
                                ui.label(RichText::new("X").color(theme::MUTED));
                                ui.add(egui::DragValue::new(&mut w.x).range(0..=239));
                                ui.label(RichText::new("Y").color(theme::MUTED));
                                ui.add(egui::DragValue::new(&mut w.y).range(0..=239));
                                ui.label(RichText::new("W").color(theme::MUTED));
                                ui.add(egui::DragValue::new(&mut w.w).range(1..=240));
                                ui.label(RichText::new("H").color(theme::MUTED));
                                ui.add(egui::DragValue::new(&mut w.h).range(1..=240));
                            });
                            if w.kind == WidgetKind::Circle {
                                w.h = w.w;
                            }
                            w.x = w.x.min((240 - w.w) as i16);
                            w.y = w.y.min((240 - w.h) as i16);
                            color_edit(ui, "Color", &mut w.color);
                            if matches!(w.kind, WidgetKind::Label | WidgetKind::Value) {
                                ui.add(
                                    egui::TextEdit::singleline(&mut w.text)
                                        .hint_text(if w.kind == WidgetKind::Value {
                                            "Value suffix"
                                        } else {
                                            "Text"
                                        })
                                        .desired_width(f32::INFINITY),
                                );
                                egui::ComboBox::from_id_salt("font")
                                    .selected_text(format!("Font {} px", w.font_size))
                                    .show_ui(ui, |ui| {
                                        for n in [16, 24, 32] {
                                            ui.selectable_value(
                                                &mut w.font_size,
                                                n,
                                                format!("{n} px"),
                                            );
                                        }
                                    });
                            }
                        });
                    }
                    ui.add_space(6.);
                    theme::card(ui, |ui| {
                        theme::section(ui, "Device", None);
                        #[cfg(not(target_arch = "wasm32"))]
                        ui.label(
                            RichText::new("USB is available in the browser build.")
                                .color(theme::MUTED),
                        );
                        #[cfg(target_arch = "wasm32")]
                        self.device_card(ui);
                    });
                });
            });
        egui::Panel::bottom("status")
            .frame(theme::panel_frame().inner_margin(egui::Margin::symmetric(16, 8)))
            .show(root, |ui| {
                ui.horizontal(|ui| {
                    // Only speak up when the project can't be exported or uploaded.
                    if let Err(e) = self.project.validate() {
                        theme::pill(ui, &e, theme::DANGER);
                    }
                    if !self.message.is_empty() {
                        ui.label(RichText::new(&self.message).color(theme::TEXT));
                    }
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.label(
                            RichText::new(
                                "Saved locally in this browser · export a file for backup",
                            )
                            .small()
                            .color(theme::MUTED),
                        );
                    });
                });
            });
        self.preview(root, ms);
        #[cfg(target_arch = "wasm32")]
        if self.live_active {
            // The bridge debounces, so edits upload once they settle.
            if let Ok(bundle) = self.project.bundle() {
                super::browser::live_sync(&bundle);
            }
            let id = self.project.views[self.view].id;
            if id != self.synced_view {
                super::browser::live_select(id);
                self.synced_view = id;
            }
        } else {
            self.synced_view = 0;
        }
        if !history_action && self.drag_origin.is_none() {
            self.checkpoint(before);
        }
        if let Ok(json) = serde_json::to_string(&self.project)
            && json != self.saved
        {
            #[cfg(target_arch = "wasm32")]
            if let Some(storage) = web_sys::window().and_then(|w| w.local_storage().ok().flatten())
            {
                let _ = storage.set_item("smartknob.project.v1", &json);
            }
            self.saved = json;
        }
    }
    #[cfg(target_arch = "wasm32")]
    fn device_card(&mut self, ui: &mut egui::Ui) {
        let connected = super::browser::connected();
        ui.horizontal_wrapped(|ui| {
            if connected {
                theme::pill(ui, "Connected", theme::ACCENT);
            } else {
                theme::pill(ui, "Not connected", theme::MUTED);
            }
            if self.live_active && super::browser::live_pending() {
                theme::pill(ui, "Syncing…", theme::WARN);
            }
        });
        ui.label(
            RichText::new(super::browser::status())
                .small()
                .color(theme::MUTED),
        );
        ui.horizontal_wrapped(|ui| {
            if !connected && theme::primary(ui, "Connect USB").clicked() {
                super::browser::connect();
            }
            if ui.button("Install firmware").clicked() {
                super::browser::install_firmware();
            }
        });
        ui.checkbox(&mut self.live, "Live sync with device");
        ui.label(
            RichText::new(
                "Edits appear on the knob as you make them (RAM only). Turning or pressing the knob updates the editor.",
            )
            .small()
            .color(theme::MUTED),
        );
        ui.horizontal_wrapped(|ui| {
            if !self.live && ui.button("Test views").clicked() {
                match self.project.bundle() {
                    Ok(b) => super::browser::upload(&b, false),
                    Err(e) => self.message = e,
                }
            }
            if theme::primary(ui, "Save to device").clicked() {
                match self.project.bundle() {
                    Ok(b) => super::browser::upload(&b, true),
                    Err(e) => self.message = e,
                }
            }
            if !self.live && ui.button("Show selected view").clicked() {
                super::browser::command(5, &self.project.views[self.view].id.to_le_bytes());
            }
        });
        ui.add_space(2.);
        theme::section(ui, "Motor", None);
        ui.horizontal_wrapped(|ui| {
            if ui.button("Calibrate motor").clicked() {
                super::browser::command(7, &[]);
            }
            if ui.button("Tare press").clicked() {
                super::browser::command(8, &[]);
            }
            if theme::danger(ui, true, "Stop motor").clicked() {
                super::browser::command(10, &[]);
            }
            if ui.button("Enable motor").clicked() {
                super::browser::command(11, &[]);
            }
        });
        ui.label(
            RichText::new("Leave the knob untouched during calibration.")
                .small()
                .color(theme::MUTED),
        );
        let state = super::browser::telemetry().to_vec();
        if connected && state.len() >= 8 {
            let faults = state[3] as u8;
            ui.horizontal_wrapped(|ui| {
                let (text, color) = match state[4] as u8 {
                    2 => ("Calibrated", theme::ACCENT),
                    1 => ("Calibrating…", theme::WARN),
                    _ => ("Not calibrated", theme::MUTED),
                };
                theme::pill(ui, text, color);
                if faults == 0 {
                    theme::pill(ui, "No faults", theme::ACCENT);
                } else {
                    theme::pill(ui, &format!("Faults {faults}"), theme::DANGER);
                }
                theme::pill(ui, &format!("Loop {:.0} µs", state[6]), theme::ACCENT_2);
            });
            if faults & 1 != 0 {
                ui.label(
                    RichText::new(
                        "Encoder read fault: motor disabled until valid readings return and the fault is cleared.",
                    )
                    .small()
                    .color(theme::DANGER),
                );
            }
            let diagnostic = super::browser::diagnostics();
            if diagnostic.contains("field=2 ") {
                ui.label(
                    RichText::new("Encoder warning: weak magnetic field. Check magnet alignment if tracking is unreliable.")
                        .small()
                        .color(theme::WARN),
                );
            } else if diagnostic.contains("field=1 ") {
                ui.label(
                    RichText::new("Encoder warning: strong magnetic field. Check magnet alignment if tracking is unreliable.")
                        .small()
                        .color(theme::WARN),
                );
            }
            if !diagnostic.is_empty() {
                egui::CollapsingHeader::new(RichText::new("Diagnostics").color(theme::MUTED)).show(
                    ui,
                    |ui| {
                        ui.add(
                            egui::Label::new(RichText::new(diagnostic).monospace().small())
                                .selectable(true),
                        );
                    },
                );
            }
            if self.live_active {
                // Follow the device only when it changes view by itself
                // (knob press), so editor clicks aren't overridden while
                // the select request is in flight.
                let device_view = state[0] as u16;
                if device_view != self.device_view {
                    self.device_view = device_view;
                    if let Some(i) = self.project.views.iter().position(|v| v.id == device_view)
                        && i != self.view
                    {
                        self.view = i;
                        self.selected = None;
                        self.drag_origin = None;
                        self.synced_view = device_view;
                    }
                }
                self.position = state[1] as i32;
                self.sub = state[2];
                self.voltage = state[5];
            }
        } else {
            self.device_view = 0;
        }
    }
    fn preview(&mut self, root: &mut egui::Ui, ms: u32) {
        #[cfg(target_arch = "wasm32")]
        let ctx = root.ctx().clone();
        egui::CentralPanel::default()
            .frame(egui::Frame::new().fill(theme::BG).inner_margin(egui::Margin::same(20)))
            .show(root, |ui| {
                let v = &self.project.views[self.view];
                ui.horizontal(|ui| {
                    ui.label(RichText::new(&v.name).size(24.).strong().color(theme::TEXT));
                    theme::pill(ui, &mode_summary(&v.haptics), theme::ACCENT_2);
                });
                ui.label(
                    RichText::new(if self.live_active {
                        "Live: edits show on the knob, and the preview follows the knob."
                    } else {
                        "Drag widgets to arrange the view. Rotate below to test values and navigation."
                    })
                    .color(theme::MUTED),
                );
                let side =
                    (ui.available_width().min(ui.available_height() - 190.)).clamp(240., 520.);
                let (area, _) = ui.allocate_exact_size(
                    Vec2::new(ui.available_width(), side),
                    Sense::hover(),
                );
                let area = Rect::from_center_size(area.center(), Vec2::splat(side));
                let scale = side / 300.;
                let screen = Rect::from_center_size(area.center(), Vec2::splat(240. * scale));
                #[cfg(target_arch = "wasm32")]
                if let Ok(bundle) = self.project.bundle() {
                    let v = &self.project.views[self.view];
                    if !self.live_active {
                        let sim = super::browser::simulate(
                            &bundle,
                            v.id,
                            self.angle_deg.to_radians(),
                            false,
                            false,
                        )
                        .to_vec();
                        if sim.len() >= 3 {
                            self.position = sim[0] as i32;
                            self.sub = sim[1];
                            self.voltage = sim[2];
                        }
                    }
                    let pixels = super::browser::render(
                        &bundle,
                        v.id,
                        self.position,
                        self.sub,
                        ms,
                        self.flash_until,
                    )
                    .to_vec();
                    if pixels.len() == 240 * 240 * 4 {
                        let image = egui::ColorImage::from_rgba_unmultiplied([240, 240], &pixels);
                        if let Some(texture) = self.texture.as_mut() {
                            texture.set(image, egui::TextureOptions::LINEAR);
                        } else {
                            self.texture = Some(ctx.load_texture(
                                "lvgl",
                                image,
                                egui::TextureOptions::LINEAR,
                            ));
                        }
                    }
                    self.ring = super::browser::ring().to_vec();
                    self.last_bundle = bundle;
                }
                let painter = ui.painter();
                let c = area.center();
                // Ambient glow in the average lit LED color, else the accent.
                let glow_color = average_ring(&self.ring).unwrap_or(theme::ACCENT);
                theme::glow(painter, c, side * 0.5, side * 0.68, glow_color, 0.32);
                // Machined bezel: layered rings with a light rim.
                painter.circle_filled(c, side * 0.5, Color32::from_rgb(20, 26, 38));
                painter.circle_stroke(
                    c,
                    side * 0.5,
                    egui::Stroke::new(2., Color32::from_rgb(58, 70, 94)),
                );
                painter.circle_filled(c, side * 0.47, Color32::from_rgb(12, 16, 25));
                painter.circle_stroke(
                    c,
                    side * 0.47,
                    egui::Stroke::new(1., Color32::from_rgb(34, 43, 62)),
                );
                if let Some(t) = &self.texture {
                    painter.image(
                        t.id(),
                        screen,
                        Rect::from_min_max(Pos2::ZERO, Pos2::new(1., 1.)),
                        Color32::WHITE,
                    );
                } else {
                    painter.circle_filled(
                        c,
                        120. * scale,
                        color(self.project.views[self.view].background),
                    );
                    painter.text(
                        c,
                        egui::Align2::CENTER_CENTER,
                        "Loading LVGL preview…",
                        egui::FontId::proportional(16.),
                        Color32::WHITE,
                    );
                }
                painter.circle_stroke(
                    c,
                    121. * scale,
                    egui::Stroke::new(1.5, Color32::from_black_alpha(160)),
                );
                for i in 0..72 {
                    let a = i as f32 * std::f32::consts::TAU / 72. - std::f32::consts::FRAC_PI_2;
                    let p = c + Vec2::new(a.cos(), a.sin()) * (137. * scale);
                    let lit = if self.ring.len() == 216 {
                        Color32::from_rgb(
                            self.ring[i * 3],
                            self.ring[i * 3 + 1],
                            self.ring[i * 3 + 2],
                        )
                    } else {
                        Color32::BLACK
                    };
                    let level = lit.r().max(lit.g()).max(lit.b());
                    if level > 8 {
                        // Boost so dim firmware values still read as lit,
                        // and add a halo.
                        let boost = 255. / level as f32;
                        let bright = Color32::from_rgb(
                            (lit.r() as f32 * boost) as u8,
                            (lit.g() as f32 * boost) as u8,
                            (lit.b() as f32 * boost) as u8,
                        );
                        let a = 0.25 + 0.75 * level as f32 / 255.;
                        painter.circle_filled(p, 7. * scale, bright.gamma_multiply(0.18 * a));
                        painter.circle_filled(p, 4.5 * scale, bright.gamma_multiply(0.35 * a));
                        painter.circle_filled(p, 2.8 * scale, bright.gamma_multiply(a));
                    } else {
                        painter.circle_filled(p, 2.6 * scale, Color32::from_rgb(30, 37, 52));
                    }
                }
                let pointer = ui.input(|i| i.pointer.hover_pos());
                let mut hit = None;
                if let Some(p) = pointer
                    && screen.contains(p)
                {
                    for (i, w) in self.project.views[self.view]
                        .widgets
                        .iter()
                        .enumerate()
                        .rev()
                    {
                        let rect = Rect::from_min_size(
                            screen.min + Vec2::new(w.x as f32, w.y as f32) * scale,
                            Vec2::new(w.w as f32, w.h as f32) * scale,
                        );
                        if rect.contains(p) {
                            hit = Some(i);
                            break;
                        }
                    }
                }
                let response = ui.interact(screen, ui.id().with("canvas"), Sense::click_and_drag());
                if response.clicked() {
                    self.selected = hit;
                }
                if response.drag_started()
                    && let Some(i) = hit
                {
                    self.selected = Some(i);
                    let w = &self.project.views[self.view].widgets[i];
                    self.drag_origin = Some((w.x, w.y));
                    self.undo.push(self.project.clone());
                    self.redo.clear();
                }
                if let (Some(i), Some((x, y))) = (self.selected, self.drag_origin) {
                    if response.dragged() {
                        let d = response.total_drag_delta().unwrap_or(Vec2::ZERO) / scale;
                        let w = &mut self.project.views[self.view].widgets[i];
                        w.x = (x as f32 + d.x).round().clamp(0., (240 - w.w) as f32) as i16;
                        w.y = (y as f32 + d.y).round().clamp(0., (240 - w.h) as f32) as i16;
                    }
                    if response.drag_stopped() {
                        self.drag_origin = None;
                    }
                }
                if let Some(i) = self.selected
                    && let Some(w) = self.project.views[self.view].widgets.get(i)
                {
                    let rect = Rect::from_min_size(
                        screen.min + Vec2::new(w.x as f32, w.y as f32) * scale,
                        Vec2::new(w.w as f32, w.h as f32) * scale,
                    );
                    ui.painter().rect_stroke(
                        rect,
                        4.,
                        egui::Stroke::new(1.5, theme::ACCENT),
                        egui::StrokeKind::Outside,
                    );
                    for corner in [
                        rect.left_top(),
                        rect.right_top(),
                        rect.left_bottom(),
                        rect.right_bottom(),
                    ] {
                        ui.painter().circle_filled(corner, 3.5, theme::ACCENT);
                    }
                }
                ui.add_space(10.);
                let controls_width = side.max(460.);
                ui.vertical_centered(|ui| {
                    ui.set_max_width(controls_width);
                    theme::card(ui, |ui| {
                        if self.live_active {
                            ui.horizontal_wrapped(|ui| {
                                theme::pill(ui, "Live from the knob", theme::ACCENT);
                                ui.label(
                                    RichText::new("Turn or press the knob to drive the preview.")
                                        .color(theme::MUTED),
                                );
                            });
                        } else {
                            ui.horizontal(|ui| {
                                ui.spacing_mut().slider_width = (controls_width - 150.).max(160.);
                                ui.add(
                                    egui::Slider::new(&mut self.angle_deg, -720.0..=720.)
                                        .text("Rotation °"),
                                );
                            });
                        }
                        ui.horizontal_wrapped(|ui| {
                            if !self.live_active {
                                if ui.button("− Detent").clicked() {
                                    self.angle_deg -=
                                        self.project.views[self.view].haptics.width_deg;
                                }
                                if ui.button("+ Detent").clicked() {
                                    self.angle_deg +=
                                        self.project.views[self.view].haptics.width_deg;
                                }
                            }
                            if theme::primary(ui, "Press").clicked() {
                                self.navigate(false, ms);
                            }
                            if ui.button("Long press").clicked() {
                                self.navigate(true, ms);
                            }
                            if !self.live_active && ui.button("Reset").clicked() {
                                self.angle_deg = 0.;
                                self.last_bundle.clear();
                                #[cfg(target_arch = "wasm32")]
                                super::browser::reset();
                            }
                        });
                        ui.horizontal(|ui| {
                            let w = (controls_width - 84.) / 3.;
                            theme::stat(ui, "Position", self.position.to_string(), theme::TEXT, w);
                            theme::stat(
                                ui,
                                "Fraction",
                                format!("{:+.2}", self.sub),
                                theme::ACCENT,
                                w,
                            );
                            theme::stat(
                                ui,
                                "Motor request",
                                format!("{:+.2} V", self.voltage),
                                theme::ACCENT_2,
                                w,
                            );
                        });
                        ui.label(
                            RichText::new(
                                "Visual preview runs the firmware's LVGL renderer. Feel feedback on the connected knob.",
                            )
                            .small()
                            .color(theme::MUTED),
                        );
                    });
                });
            });
    }
}
/// Short description of a view's haptics, e.g. "Detents · 6°".
fn mode_summary(h: &Haptics) -> String {
    match h.mode {
        Mode::Free => "Free spin".into(),
        Mode::Switch => "Switch".into(),
        Mode::Bounded => format!("Bounded {}…{} · {:.0}°", h.min, h.max, h.width_deg),
        Mode::Magnetic => format!("Magnetic · {} stops", h.detents.len()),
        Mode::Detent => format!("Detents · {:.0}°", h.width_deg),
    }
}
/// Average color of the lit LEDs, for the ambient glow.
fn average_ring(ring: &[u8]) -> Option<Color32> {
    if ring.len() != 216 {
        return None;
    }
    let (mut r, mut g, mut b, mut n) = (0u32, 0u32, 0u32, 0u32);
    for px in ring.as_chunks::<3>().0 {
        if px.iter().any(|&c| c > 8) {
            r += px[0] as u32;
            g += px[1] as u32;
            b += px[2] as u32;
            n += 1;
        }
    }
    if n == 0 {
        return None;
    }
    let max = (r / n).max(g / n).max(b / n).max(1);
    let k = 255. / max as f32;
    Some(Color32::from_rgb(
        ((r / n) as f32 * k) as u8,
        ((g / n) as f32 * k) as u8,
        ((b / n) as f32 * k) as u8,
    ))
}
/// Two-line list row for a view: name plus haptic summary.
fn view_row(ui: &mut egui::Ui, v: &View, selected: bool, start: bool) -> egui::Response {
    let (rect, response) =
        ui.allocate_exact_size(Vec2::new(ui.available_width(), 46.), Sense::click());
    let p = ui.painter();
    let hovered = response.hovered();
    if selected {
        p.rect_filled(rect, 10., theme::CARD_HI);
        let bar = Rect::from_min_size(rect.left_top() + Vec2::new(0., 8.), Vec2::new(3., 30.));
        p.rect_filled(bar, 2., theme::ACCENT);
        p.rect_stroke(
            rect,
            10.,
            egui::Stroke::new(1., theme::ACCENT.linear_multiply(0.35)),
            egui::StrokeKind::Inside,
        );
    } else if hovered {
        p.rect_filled(rect, 10., theme::CARD);
    }
    // ID badge.
    let badge = Rect::from_center_size(
        Pos2::new(rect.left() + 24., rect.center().y),
        Vec2::splat(26.),
    );
    p.rect_filled(
        badge,
        8.,
        if selected {
            theme::ACCENT.linear_multiply(0.2)
        } else {
            theme::CARD_HI
        },
    );
    p.text(
        badge.center(),
        egui::Align2::CENTER_CENTER,
        v.id.to_string(),
        egui::FontId::proportional(13.),
        if selected {
            theme::ACCENT
        } else {
            theme::MUTED
        },
    );
    let text_x = rect.left() + 46.;
    p.text(
        Pos2::new(text_x, rect.top() + 14.),
        egui::Align2::LEFT_CENTER,
        &v.name,
        egui::FontId::proportional(14.5),
        if selected || hovered {
            Color32::WHITE
        } else {
            theme::TEXT
        },
    );
    p.text(
        Pos2::new(text_x, rect.top() + 32.),
        egui::Align2::LEFT_CENTER,
        mode_summary(&v.haptics),
        egui::FontId::proportional(11.5),
        theme::MUTED,
    );
    if start {
        p.text(
            Pos2::new(rect.right() - 12., rect.center().y),
            egui::Align2::RIGHT_CENTER,
            "★",
            egui::FontId::proportional(14.),
            theme::WARN,
        );
    }
    response.on_hover_cursor(egui::CursorIcon::PointingHand)
}
/// One-line list row for a widget with its color swatch.
fn widget_row(ui: &mut egui::Ui, w: &Widget, selected: bool) -> egui::Response {
    let (rect, response) =
        ui.allocate_exact_size(Vec2::new(ui.available_width(), 32.), Sense::click());
    let p = ui.painter();
    if selected {
        p.rect_filled(rect, 8., theme::CARD_HI);
        p.rect_stroke(
            rect,
            8.,
            egui::Stroke::new(1., theme::ACCENT.linear_multiply(0.35)),
            egui::StrokeKind::Inside,
        );
    } else if response.hovered() {
        p.rect_filled(rect, 8., theme::CARD);
    }
    let swatch = Rect::from_center_size(
        Pos2::new(rect.left() + 16., rect.center().y),
        Vec2::splat(12.),
    );
    p.rect_filled(swatch, 3., color(w.color));
    p.rect_stroke(
        swatch,
        3.,
        egui::Stroke::new(1., theme::BORDER),
        egui::StrokeKind::Outside,
    );
    p.text(
        Pos2::new(rect.left() + 32., rect.center().y),
        egui::Align2::LEFT_CENTER,
        w.kind.label(),
        egui::FontId::proportional(14.),
        theme::TEXT,
    );
    let detail = if w.text.is_empty() {
        format!("#{}", w.id)
    } else {
        format!("“{}”", w.text)
    };
    p.text(
        Pos2::new(rect.right() - 10., rect.center().y),
        egui::Align2::RIGHT_CENTER,
        detail,
        egui::FontId::proportional(11.5),
        theme::MUTED,
    );
    response.on_hover_cursor(egui::CursorIcon::PointingHand)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn editing_and_navigation() {
        let mut e = Editor::default();
        let before = e.project.clone();
        e.project.views[0].name = "Edited".into();
        e.checkpoint(before.clone());
        assert_eq!(e.undo[0], before);
        e.navigate(false, 100);
        assert_eq!(e.view, 1);
        e.navigate(true, 200);
        assert_eq!(e.view, 0);
        e.project.validate().unwrap();
    }
    #[test]
    fn ui_can_render_without_browser() {
        let ctx = egui::Context::default();
        let mut e = Editor::new(&ctx);
        let mut output = ctx.run_ui(egui::RawInput::default(), |ui| e.ui(ui));
        output.textures_delta.clear();
        e.project.validate().unwrap();
    }
}
