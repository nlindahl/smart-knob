mod app;
pub use app::Editor;

#[cfg(target_arch = "wasm32")]
mod browser {
    use super::*;
    use wasm_bindgen::prelude::*;
    #[wasm_bindgen(js_namespace = smartknob)]
    extern "C" {
        pub fn render(
            bundle: &[u8],
            view: u16,
            position: i32,
            sub: f32,
            ms: u32,
            flash: u32,
        ) -> js_sys::Uint8Array;
        pub fn simulate(
            bundle: &[u8],
            view: u16,
            angle: f32,
            pressed: bool,
            long_press: bool,
        ) -> js_sys::Float32Array;
        pub fn ring() -> js_sys::Uint8Array;
        pub fn install_firmware();
        pub fn reset();
        pub fn download(name: &str, bytes: &[u8], mime: &str);
        pub fn import_project();
        pub fn take_import() -> String;
        pub fn connect();
        pub fn upload(bundle: &[u8], save: bool);
        pub fn command(op: u8, payload: &[u8]);
        pub fn status() -> String;
        pub fn telemetry() -> js_sys::Float32Array;
        pub fn diagnostics() -> String;
    }
    #[wasm_bindgen]
    pub async fn start(canvas_id: &str) -> Result<(), JsValue> {
        let document = web_sys::window().unwrap().document().unwrap();
        let canvas = document
            .get_element_by_id(canvas_id)
            .ok_or_else(|| JsValue::from_str("Canvas missing"))?
            .dyn_into::<web_sys::HtmlCanvasElement>()?;
        eframe::WebRunner::new()
            .start(
                canvas,
                eframe::WebOptions::default(),
                Box::new(|cc| Ok(Box::new(Editor::new(&cc.egui_ctx)))),
            )
            .await
    }
    impl eframe::App for Editor {
        fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
            Editor::ui(self, ui);
        }
    }
}
