//! Visual theme and small styled building blocks for SmartKnob Studio.
use egui::{
    Color32, CornerRadius, FontId, Frame, Margin, Pos2, Rect, Response, RichText, Sense, Shadow,
    Stroke, TextStyle, Ui, Vec2, epaint::Mesh,
};

pub const BG: Color32 = Color32::from_rgb(9, 12, 20);
pub const PANEL: Color32 = Color32::from_rgb(13, 18, 30);
pub const CARD: Color32 = Color32::from_rgb(19, 26, 41);
pub const CARD_HI: Color32 = Color32::from_rgb(27, 36, 56);
pub const BORDER: Color32 = Color32::from_rgb(36, 48, 73);
pub const TEXT: Color32 = Color32::from_rgb(230, 235, 245);
pub const MUTED: Color32 = Color32::from_rgb(138, 150, 173);
/// Brand teal, also the firmware's default arc/LED color.
pub const ACCENT: Color32 = Color32::from_rgb(45, 226, 180);
pub const ACCENT_2: Color32 = Color32::from_rgb(139, 92, 246);
pub const DANGER: Color32 = Color32::from_rgb(240, 93, 108);
pub const WARN: Color32 = Color32::from_rgb(245, 184, 74);

const RADIUS: u8 = 8;

pub fn apply(ctx: &egui::Context) {
    ctx.set_theme(egui::Theme::Dark);
    ctx.all_styles_mut(|style| {
        use FontId as F;
        style.text_styles = [
            (TextStyle::Heading, F::proportional(21.)),
            (TextStyle::Body, F::proportional(14.5)),
            (TextStyle::Button, F::proportional(14.)),
            (TextStyle::Small, F::proportional(11.5)),
            (TextStyle::Monospace, F::monospace(12.5)),
        ]
        .into();
        let s = &mut style.spacing;
        s.item_spacing = Vec2::new(8., 8.);
        s.button_padding = Vec2::new(12., 6.);
        s.interact_size = Vec2::new(40., 30.);
        s.slider_width = 150.;
        s.slider_rail_height = 6.;
        s.combo_width = 180.;
        s.indent = 14.;
        let v = &mut style.visuals;
        v.dark_mode = true;
        v.override_text_color = None;
        v.panel_fill = PANEL;
        v.window_fill = CARD;
        v.window_stroke = Stroke::new(1., BORDER);
        v.window_corner_radius = CornerRadius::same(12);
        v.menu_corner_radius = CornerRadius::same(10);
        v.window_shadow = Shadow {
            offset: [0, 8],
            blur: 24,
            spread: 0,
            color: Color32::from_black_alpha(140),
        };
        v.popup_shadow = v.window_shadow;
        v.extreme_bg_color = Color32::from_rgb(10, 15, 26);
        v.faint_bg_color = CARD;
        v.hyperlink_color = ACCENT;
        v.warn_fg_color = WARN;
        v.error_fg_color = DANGER;
        v.slider_trailing_fill = true;
        v.handle_shape = egui::style::HandleShape::Circle;
        v.collapsing_header_frame = false;
        v.selection.bg_fill = ACCENT.linear_multiply(0.35);
        v.selection.stroke = Stroke::new(1.5, ACCENT);
        let r = CornerRadius::same(RADIUS);
        let w = &mut v.widgets;
        w.noninteractive.bg_fill = CARD;
        w.noninteractive.weak_bg_fill = CARD;
        w.noninteractive.bg_stroke = Stroke::new(1., BORDER);
        w.noninteractive.fg_stroke = Stroke::new(1., TEXT);
        w.noninteractive.corner_radius = r;
        w.inactive.bg_fill = CARD_HI;
        w.inactive.weak_bg_fill = CARD_HI;
        w.inactive.bg_stroke = Stroke::new(1., BORDER);
        w.inactive.fg_stroke = Stroke::new(1., TEXT);
        w.inactive.corner_radius = r;
        w.hovered.bg_fill = Color32::from_rgb(35, 48, 74);
        w.hovered.weak_bg_fill = Color32::from_rgb(35, 48, 74);
        w.hovered.bg_stroke = Stroke::new(1., ACCENT.linear_multiply(0.7));
        w.hovered.fg_stroke = Stroke::new(1.5, Color32::WHITE);
        w.hovered.corner_radius = r;
        w.hovered.expansion = 1.;
        w.active.bg_fill = ACCENT.linear_multiply(0.45);
        w.active.weak_bg_fill = ACCENT.linear_multiply(0.45);
        w.active.bg_stroke = Stroke::new(1., ACCENT);
        w.active.fg_stroke = Stroke::new(1.5, Color32::WHITE);
        w.active.corner_radius = r;
        w.open.bg_fill = CARD_HI;
        w.open.weak_bg_fill = CARD_HI;
        w.open.bg_stroke = Stroke::new(1., ACCENT.linear_multiply(0.7));
        w.open.fg_stroke = Stroke::new(1., TEXT);
        w.open.corner_radius = r;
    });
}

/// Frame for the side and top panels.
pub fn panel_frame() -> Frame {
    Frame::new().fill(PANEL).inner_margin(Margin::same(14))
}

/// Raised, rounded content card.
pub fn card<R>(ui: &mut Ui, add: impl FnOnce(&mut Ui) -> R) -> R {
    Frame::new()
        .fill(CARD)
        .stroke(Stroke::new(1., BORDER))
        .corner_radius(CornerRadius::same(14))
        .inner_margin(Margin::same(14))
        .shadow(Shadow {
            offset: [0, 4],
            blur: 16,
            spread: 0,
            color: Color32::from_black_alpha(90),
        })
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            add(ui)
        })
        .inner
}

/// Small uppercase section label with an optional right-aligned note.
pub fn section(ui: &mut Ui, title: &str, note: Option<String>) {
    ui.horizontal(|ui| {
        ui.label(
            RichText::new(title.to_uppercase())
                .size(11.5)
                .strong()
                .color(MUTED),
        );
        if let Some(note) = note {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label(RichText::new(note).size(11.5).color(MUTED));
            });
        }
    });
}

/// Filled accent button for the main action in a group.
pub fn primary(ui: &mut Ui, text: &str) -> Response {
    ui.add(
        egui::Button::new(RichText::new(text).strong().color(BG))
            .fill(ACCENT)
            .stroke(Stroke::NONE)
            .corner_radius(CornerRadius::same(RADIUS)),
    )
}

/// Outlined button for destructive or stopping actions.
pub fn danger(ui: &mut Ui, enabled: bool, text: &str) -> Response {
    ui.add_enabled(
        enabled,
        egui::Button::new(RichText::new(text).color(DANGER))
            .fill(DANGER.linear_multiply(0.08))
            .stroke(Stroke::new(1., DANGER.linear_multiply(0.6)))
            .corner_radius(CornerRadius::same(RADIUS)),
    )
}

/// Rounded status pill with a colored dot.
pub fn pill(ui: &mut Ui, text: &str, color: Color32) -> Response {
    let galley = ui
        .painter()
        .layout_no_wrap(text.to_owned(), FontId::proportional(12.5), TEXT);
    let size = Vec2::new(galley.size().x + 30., 24.);
    let (rect, response) = ui.allocate_exact_size(size, Sense::hover());
    let p = ui.painter();
    p.rect(
        rect,
        CornerRadius::same(12),
        color.linear_multiply(0.12),
        Stroke::new(1., color.linear_multiply(0.55)),
        egui::StrokeKind::Inside,
    );
    let dot = Pos2::new(rect.left() + 12., rect.center().y);
    p.circle_filled(dot, 7., color.linear_multiply(0.25));
    p.circle_filled(dot, 3.5, color);
    p.galley(
        Pos2::new(rect.left() + 22., rect.center().y - galley.size().y / 2.),
        galley,
        TEXT,
    );
    response
}

/// Compact tile showing a live value under a muted caption.
pub fn stat(ui: &mut Ui, caption: &str, value: String, color: Color32, width: f32) {
    Frame::new()
        .fill(CARD_HI)
        .stroke(Stroke::new(1., BORDER))
        .corner_radius(CornerRadius::same(10))
        .inner_margin(Margin::symmetric(12, 8))
        .show(ui, |ui| {
            ui.set_width(width);
            ui.vertical(|ui| {
                ui.spacing_mut().item_spacing.y = 2.;
                ui.label(
                    RichText::new(caption.to_uppercase())
                        .size(10.5)
                        .color(MUTED),
                );
                ui.label(RichText::new(value).size(20.).strong().color(color));
            });
        });
}

/// Horizontal two-color gradient fill.
pub fn gradient(painter: &egui::Painter, rect: Rect, left: Color32, right: Color32) {
    let mut mesh = Mesh::default();
    mesh.colored_vertex(rect.left_top(), left);
    mesh.colored_vertex(rect.right_top(), right);
    mesh.colored_vertex(rect.left_bottom(), left);
    mesh.colored_vertex(rect.right_bottom(), right);
    mesh.add_triangle(0, 1, 2);
    mesh.add_triangle(1, 2, 3);
    painter.add(egui::Shape::mesh(mesh));
}

/// Soft radial glow: a ring mesh fading from `alpha` at `inner` to
/// transparent at `outer`, so it blends smoothly into the background.
pub fn glow(
    painter: &egui::Painter,
    center: Pos2,
    inner: f32,
    outer: f32,
    color: Color32,
    alpha: f32,
) {
    const SEGMENTS: u32 = 96;
    const RINGS: u32 = 8;
    let mut mesh = Mesh::default();
    for ring in 0..=RINGS {
        let t = ring as f32 / RINGS as f32;
        let r = inner + (outer - inner) * t;
        // Ease out so the falloff looks like light, not a band.
        let a = alpha * (1. - t) * (1. - t);
        let c = color.gamma_multiply(a);
        for i in 0..SEGMENTS {
            let angle = i as f32 / SEGMENTS as f32 * std::f32::consts::TAU;
            mesh.colored_vertex(center + Vec2::angled(angle) * r, c);
        }
    }
    for ring in 0..RINGS {
        for i in 0..SEGMENTS {
            let j = (i + 1) % SEGMENTS;
            let (a, b) = (ring * SEGMENTS + i, ring * SEGMENTS + j);
            let (c, d) = (a + SEGMENTS, b + SEGMENTS);
            mesh.add_triangle(a, b, c);
            mesh.add_triangle(b, d, c);
        }
    }
    painter.add(egui::Shape::mesh(mesh));
}

/// App logo: a knob with a teal-to-violet indicator arc.
pub fn logo(ui: &mut Ui, size: f32) {
    let (rect, _) = ui.allocate_exact_size(Vec2::splat(size), Sense::hover());
    let p = ui.painter();
    let c = rect.center();
    let r = size * 0.42;
    p.circle_filled(c, r, CARD_HI);
    p.circle_stroke(c, r, Stroke::new(1., BORDER));
    let steps = 24;
    let start = std::f32::consts::PI * 0.75;
    let sweep = std::f32::consts::PI * 1.5;
    for i in 0..steps {
        let t0 = i as f32 / steps as f32;
        let t1 = (i + 1) as f32 / steps as f32;
        let a0 = start + sweep * t0;
        let a1 = start + sweep * t1;
        let col = lerp(ACCENT, ACCENT_2, t0);
        p.line_segment(
            [
                c + Vec2::angled(a0) * (r - 3.),
                c + Vec2::angled(a1) * (r - 3.),
            ],
            Stroke::new(2.5, col),
        );
    }
    p.circle_filled(c + Vec2::angled(-0.9) * (r * 0.45), 2.5, ACCENT);
}

pub fn lerp(a: Color32, b: Color32, t: f32) -> Color32 {
    let m = |x: u8, y: u8| (x as f32 + (y as f32 - x as f32) * t).round() as u8;
    Color32::from_rgb(m(a.r(), b.r()), m(a.g(), b.g()), m(a.b(), b.b()))
}
