use egui::{Color32, Pos2, Rect, Sense, Vec2};
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
    #[cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]
    follow_device: bool,
}
fn color(c: u32) -> Color32 {
    Color32::from_rgb((c >> 16) as u8, (c >> 8) as u8, c as u8)
}
fn color_edit(ui: &mut egui::Ui, label: &str, c: &mut u32) {
    ui.horizontal(|ui| {
        ui.label(label);
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
            follow_device: false,
        }
    }
}
impl Editor {
    pub fn new(ctx: &egui::Context) -> Self {
        ctx.set_theme(egui::Theme::Dark);
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
        egui::Panel::top("toolbar").show(root, |ui| {
            ui.horizontal(|ui| {
                ui.heading("SmartKnob Studio");
                ui.separator();
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
            });
        });
        egui::Panel::left("views")
            .default_size(205.)
            .resizable(true)
            .show(root, |ui| {
                ui.heading("Views");
                let mut choice = self.view;
                for (i, v) in self.project.views.iter().enumerate() {
                    if ui
                        .selectable_label(i == self.view, format!("{}  {}", v.id, v.name))
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
                ui.horizontal(|ui| {
                    if ui
                        .add_enabled(
                            self.project.views.len() < MAX_VIEWS,
                            egui::Button::new("Add"),
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
                });
                if ui
                    .add_enabled(
                        self.project.views.len() > 1,
                        egui::Button::new("Delete view"),
                    )
                    .clicked()
                {
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
                ui.separator();
                if ui.button("Use as start view").clicked() {
                    self.project.start_view = self.project.views[self.view].id;
                }
                ui.label(format!("Start view: {}", self.project.start_view));
                ui.separator();
                ui.heading("Widgets");
                for (i, w) in self.project.views[self.view].widgets.iter().enumerate() {
                    if ui
                        .selectable_label(
                            self.selected == Some(i),
                            format!("{}  {}", w.id, w.kind.label()),
                        )
                        .clicked()
                    {
                        self.selected = Some(i);
                    }
                }
                egui::ComboBox::from_id_salt("add_widget")
                    .selected_text("Add widget…")
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
                        if ui.button("Delete").clicked() {
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
        egui::Panel::right("properties")
            .default_size(280.)
            .show(root, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    ui.heading("Properties");
                    let choices: Vec<_> = self
                        .project
                        .views
                        .iter()
                        .map(|v| (v.id, v.name.clone()))
                        .collect();
                    let v = &mut self.project.views[self.view];
                    ui.label("View name");
                    ui.text_edit_singleline(&mut v.name);
                    color_edit(ui, "Background", &mut v.background);
                    for (label, target) in [
                        ("Short press", &mut v.on_press),
                        ("Long press", &mut v.on_long_press),
                    ] {
                        egui::ComboBox::from_id_salt(label)
                            .selected_text(format!(
                                "{label}: {}",
                                choices
                                    .iter()
                                    .find(|c| c.0 == *target)
                                    .map(|c| c.1.as_str())
                                    .unwrap_or("Stay")
                            ))
                            .show_ui(ui, |ui| {
                                ui.selectable_value(target, 0, "Stay");
                                for (id, name) in &choices {
                                    ui.selectable_value(target, *id, name);
                                }
                            });
                    }
                    ui.separator();
                    ui.collapsing("Motor feedback", |ui| {
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
                        ui.add(egui::Slider::new(&mut h.width_deg, 1.0..=180.).text("Spacing °"));
                        ui.add(
                            egui::Slider::new(&mut h.strength, 0.0..=1.).text("Detent strength"),
                        );
                        ui.add(egui::Slider::new(&mut h.endstop, 0.0..=1.).text("End stops"));
                        ui.add(egui::Slider::new(&mut h.snap, 0.5..=1.5).text("Snap threshold"));
                        ui.add(egui::Slider::new(&mut h.damping, 0.0..=1.).text("Damping"));
                        ui.horizontal(|ui| {
                            ui.label("Initial");
                            ui.add(
                                egui::DragValue::new(&mut h.initial).range(-1_000_000..=1_000_000),
                            );
                        });
                        if matches!(h.mode, Mode::Bounded | Mode::Switch) {
                            ui.horizontal(|ui| {
                                ui.label("Min");
                                ui.add(
                                    egui::DragValue::new(&mut h.min).range(-1_000_000..=1_000_000),
                                );
                                ui.label("Max");
                                ui.add(
                                    egui::DragValue::new(&mut h.max).range(-1_000_000..=1_000_000),
                                );
                            });
                        }
                        if h.mode == Mode::Magnetic {
                            ui.label("Detent positions (comma separated)");
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
                    ui.collapsing("72-LED ring", |ui| {
                        let l = &mut v.leds;
                        egui::ComboBox::from_id_salt("led_mode")
                            .selected_text(l.mode.label())
                            .show_ui(ui, |ui| {
                                for mode in LedMode::ALL {
                                    ui.selectable_value(&mut l.mode, mode, mode.label());
                                }
                            });
                        ui.add(egui::Slider::new(&mut l.brightness, 0..=255).text("Brightness"));
                        color_edit(ui, "Main", &mut l.color);
                        color_edit(ui, "Accent / flash", &mut l.accent);
                        ui.add(egui::Slider::new(&mut l.offset, 0..=71).text("Ring offset"));
                        ui.checkbox(&mut l.reverse, "Reverse direction");
                        ui.checkbox(&mut l.press_flash, "Flash on press");
                        ui.checkbox(&mut l.mode_flash, "Flash on view change");
                        ui.small("Brightness is limited by a device current budget.");
                    });
                    if let Some(i) = self.selected
                        && let Some(w) = v.widgets.get_mut(i)
                    {
                        ui.separator();
                        ui.heading(w.kind.label());
                        ui.horizontal(|ui| {
                            ui.label("X");
                            ui.add(egui::DragValue::new(&mut w.x).range(0..=239));
                            ui.label("Y");
                            ui.add(egui::DragValue::new(&mut w.y).range(0..=239));
                        });
                        ui.horizontal(|ui| {
                            ui.label("W");
                            ui.add(egui::DragValue::new(&mut w.w).range(1..=240));
                            ui.label("H");
                            ui.add(egui::DragValue::new(&mut w.h).range(1..=240));
                        });
                        if w.kind == WidgetKind::Circle {
                            w.h = w.w;
                        }
                        w.x = w.x.min((240 - w.w) as i16);
                        w.y = w.y.min((240 - w.h) as i16);
                        color_edit(ui, "Color", &mut w.color);
                        if matches!(w.kind, WidgetKind::Label | WidgetKind::Value) {
                            ui.label(if w.kind == WidgetKind::Value {
                                "Value suffix"
                            } else {
                                "Text"
                            });
                            ui.text_edit_singleline(&mut w.text);
                            egui::ComboBox::from_id_salt("font")
                                .selected_text(format!("Font {}", w.font_size))
                                .show_ui(ui, |ui| {
                                    for n in [16, 24, 32] {
                                        ui.selectable_value(&mut w.font_size, n, n.to_string());
                                    }
                                });
                        }
                    }
                    ui.separator();
                    ui.heading("Device");
                    #[cfg(target_arch = "wasm32")]
                    {
                        if ui.button("Connect USB").clicked() {
                            super::browser::connect();
                        }
                        if ui.button("Install firmware").clicked() {
                            super::browser::install_firmware();
                        }
                        ui.horizontal(|ui| {
                            if ui.button("Test views").clicked() {
                                match self.project.bundle() {
                                    Ok(b) => super::browser::upload(&b, false),
                                    Err(e) => self.message = e,
                                }
                            }
                            if ui.button("Upload & save").clicked() {
                                match self.project.bundle() {
                                    Ok(b) => super::browser::upload(&b, true),
                                    Err(e) => self.message = e,
                                }
                            }
                        });
                        if ui.button("Show selected view").clicked() {
                            super::browser::command(
                                5,
                                &self.project.views[self.view].id.to_le_bytes(),
                            );
                        }
                        ui.horizontal(|ui| {
                            if ui.button("Calibrate motor").clicked() {
                                super::browser::command(7, &[]);
                            }
                            if ui.button("Tare press").clicked() {
                                super::browser::command(8, &[]);
                            }
                        });
                        ui.small("Leave the knob untouched during calibration.");
                        ui.horizontal(|ui| {
                            if ui.button("Stop motor").clicked() {
                                super::browser::command(10, &[]);
                            }
                            if ui.button("Enable motor").clicked() {
                                super::browser::command(11, &[]);
                            }
                        });
                        ui.checkbox(&mut self.follow_device, "Follow device state");
                        ui.label(super::browser::status());
                        let state = super::browser::telemetry().to_vec();
                        if state.len() >= 8 {
                            ui.small(format!(
                                "Faults: {} · calibration: {} · max loop: {:.0} µs",
                                state[3], state[4], state[6]
                            ));
                            if state[3] as u8 & 1 != 0 {
                                ui.small("Encoder read fault: motor disabled until valid readings return and the fault is cleared.");
                                let diagnostic = super::browser::diagnostics();
                                if !diagnostic.is_empty() {
                                    ui.add(egui::Label::new(diagnostic).selectable(true));
                                }
                            }
                            if self.follow_device
                                && let Some(i) = self
                                    .project
                                    .views
                                    .iter()
                                    .position(|v| v.id == state[0] as u16)
                            {
                                if self.view != i {
                                    self.selected = None;
                                    self.drag_origin = None;
                                }
                                self.view = i;
                                self.position = state[1] as i32;
                                self.sub = state[2];
                                self.voltage = state[5];
                            }
                        }
                    }
                });
            });
        egui::Panel::bottom("status").show(root, |ui| {
            match self.project.validate() {
                Ok(()) => {
                    ui.colored_label(Color32::from_rgb(69, 214, 176), "Project valid");
                }
                Err(e) => {
                    ui.colored_label(Color32::LIGHT_RED, e);
                }
            }
            if !self.message.is_empty() {
                ui.label(&self.message);
            }
            ui.small("JSON is saved locally in this browser. Export a file for backup.");
        });
        self.preview(root, ms);
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
    fn preview(&mut self, root: &mut egui::Ui, ms: u32) {
        #[cfg(target_arch = "wasm32")]
        let ctx = root.ctx().clone();
        egui::CentralPanel::default().show(root, |ui| {
            ui.heading("Live preview");
            ui.label(
                "Drag widgets to arrange the view. Rotate below to test values and navigation.",
            );
            let side = (ui.available_width().min(ui.available_height() - 150.)).clamp(240., 480.);
            let (area, _) = ui.allocate_exact_size(Vec2::splat(side), Sense::hover());
            let scale = side / 300.;
            let screen = Rect::from_center_size(area.center(), Vec2::splat(240. * scale));
            #[cfg(target_arch = "wasm32")]
            if let Ok(bundle) = self.project.bundle() {
                let v = &self.project.views[self.view];
                if !self.follow_device {
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
                        self.texture =
                            Some(ctx.load_texture("lvgl", image, egui::TextureOptions::LINEAR));
                    }
                }
                self.ring = super::browser::ring().to_vec();
                self.last_bundle = bundle;
            }
            let painter = ui.painter();
            painter.circle_filled(area.center(), side * 0.495, Color32::from_rgb(25, 30, 38));
            if let Some(t) = &self.texture {
                painter.image(
                    t.id(),
                    screen,
                    Rect::from_min_max(Pos2::ZERO, Pos2::new(1., 1.)),
                    Color32::WHITE,
                );
            } else {
                painter.circle_filled(
                    area.center(),
                    120. * scale,
                    color(self.project.views[self.view].background),
                );
                painter.text(
                    area.center(),
                    egui::Align2::CENTER_CENTER,
                    "Loading LVGL preview…",
                    egui::FontId::proportional(16.),
                    Color32::WHITE,
                );
            }
            for i in 0..72 {
                let a = i as f32 * std::f32::consts::TAU / 72. - std::f32::consts::FRAC_PI_2;
                let p = area.center() + Vec2::new(a.cos(), a.sin()) * (137. * scale);
                let c = if self.ring.len() == 216 {
                    Color32::from_rgb(self.ring[i * 3], self.ring[i * 3 + 1], self.ring[i * 3 + 2])
                } else {
                    Color32::from_gray(35)
                };
                painter.circle_filled(p, 3. * scale, c);
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
                    0.,
                    egui::Stroke::new(1.5, Color32::from_rgb(69, 214, 176)),
                    egui::StrokeKind::Outside,
                );
            }
            ui.add(egui::Slider::new(&mut self.angle_deg, -720.0..=720.).text("Rotation °"));
            ui.horizontal(|ui| {
                if ui.button("− detent").clicked() {
                    self.angle_deg -= self.project.views[self.view].haptics.width_deg;
                }
                if ui.button("+ detent").clicked() {
                    self.angle_deg += self.project.views[self.view].haptics.width_deg;
                }
                if ui.button("Press").clicked() {
                    self.navigate(false, ms);
                }
                if ui.button("Long press").clicked() {
                    self.navigate(true, ms);
                }
                if ui.button("Reset rotation").clicked() {
                    self.angle_deg = 0.;
                    self.last_bundle.clear();
                    #[cfg(target_arch = "wasm32")]
                    super::browser::reset();
                }
            });
            ui.label(format!(
                "Position {} · fraction {:.2} · motor request {:.2} V",
                self.position, self.sub, self.voltage
            ));
            ui.small("Visual preview uses LVGL. Feel feedback on the connected knob.");
        });
    }
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
