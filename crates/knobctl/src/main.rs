use clap::{Parser, Subcommand};
use std::{
    fs,
    io::{Read, Write},
    path::PathBuf,
    time::{Duration, Instant},
};
use view_model::{
    Project,
    protocol::{self, Frame},
};

#[derive(Parser)]
#[command(about = "Compile views and control a Zephyr SmartKnob over USB")]
struct Args {
    #[command(subcommand)]
    command: Command,
}
#[derive(Subcommand)]
enum Command {
    Demo {
        output: PathBuf,
    },
    Validate {
        project: PathBuf,
    },
    Compile {
        project: PathBuf,
        output: PathBuf,
    },
    Upload {
        bundle: PathBuf,
        #[arg(long)]
        port: String,
        #[arg(long)]
        save: bool,
    },
    State {
        #[arg(long)]
        port: String,
    },
    Select {
        id: u16,
        #[arg(long)]
        port: String,
    },
    Calibrate {
        #[arg(long)]
        port: String,
    },
    Tare {
        #[arg(long)]
        port: String,
    },
    Stop {
        #[arg(long)]
        port: String,
    },
    Enable {
        #[arg(long)]
        port: String,
    },
    Save {
        #[arg(long)]
        port: String,
    },
    /// Print encoder and motor diagnostics (calibration result, read errors).
    Diag {
        #[arg(long)]
        port: String,
    },
    /// Poll device state and print each change, e.g. while turning the knob.
    Watch {
        #[arg(long)]
        port: String,
        /// How long to watch, in seconds.
        #[arg(long, default_value_t = 30.)]
        seconds: f64,
    },
    Ports,
}
struct Device {
    port: Box<dyn serialport::SerialPort>,
    parser: protocol::Parser,
    seq: u16,
}
impl Device {
    fn open(port: &str) -> Result<Self, String> {
        let port = serialport::new(port, 115200)
            .timeout(Duration::from_millis(100))
            .open()
            .map_err(|e| e.to_string())?;
        let mut d = Self {
            port,
            parser: protocol::Parser::default(),
            seq: 1,
        };
        let hello = d.call(protocol::HELLO, vec![])?;
        println!("Connected: {}", String::from_utf8_lossy(&hello));
        Ok(d)
    }
    fn exchange(&mut self, frame: Frame) -> Result<Vec<u8>, String> {
        self.port
            .write_all(&frame.encode()?)
            .map_err(|e| e.to_string())?;
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut buf = [0u8; 1024];
        while Instant::now() < deadline {
            match self.port.read(&mut buf) {
                Ok(n) => {
                    for response in self.parser.feed(&buf[..n]) {
                        if response.seq == frame.seq && response.op == frame.op | 0x80 {
                            return response.response().map(|p| p.to_vec());
                        }
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => (),
                Err(e) => return Err(e.to_string()),
            }
        }
        Err("Device response timed out".into())
    }
    fn call(&mut self, op: u8, payload: Vec<u8>) -> Result<Vec<u8>, String> {
        let f = Frame {
            op,
            seq: self.seq,
            payload,
        };
        self.seq = self.seq.wrapping_add(1);
        self.exchange(f)
    }
}
/// Decoded 28-byte STATE reply.
struct State {
    view: u16,
    position: i32,
    fraction: f32,
    faults: u8,
    calibration: u8,
    loops: u32,
    voltage: f32,
    max_loop_us: f32,
    presses: u16,
}
impl State {
    fn parse(r: &[u8]) -> Option<Self> {
        if r.len() != 28 {
            return None;
        }
        let f32_at = |i: usize| f32::from_le_bytes(r[i..i + 4].try_into().unwrap());
        Some(Self {
            view: u16::from_le_bytes([r[0], r[1]]),
            position: i32::from_le_bytes(r[2..6].try_into().unwrap()),
            fraction: f32_at(6),
            faults: r[10],
            calibration: r[11],
            loops: u32::from_le_bytes(r[12..16].try_into().unwrap()),
            voltage: f32_at(16),
            max_loop_us: f32_at(20),
            presses: u16::from_le_bytes([r[24], r[25]]),
        })
    }
}
impl std::fmt::Display for State {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "view={} position={} fraction={:.3} faults={} calibration={} loops={} voltage={:.3} max_loop_us={:.0} presses={}",
            self.view,
            self.position,
            self.fraction,
            self.faults,
            self.calibration,
            self.loops,
            self.voltage,
            self.max_loop_us,
            self.presses
        )
    }
}
fn watch(port: &str, seconds: f64) -> Result<(), String> {
    let mut d = Device::open(port)?;
    let start = Instant::now();
    let mut last: Option<(u16, i32, u8, u16)> = None;
    let (mut lo, mut hi, mut samples) = (i32::MAX, i32::MIN, 0u32);
    let mut state = None;
    while start.elapsed().as_secs_f64() < seconds {
        let s = State::parse(&d.call(protocol::STATE, vec![])?).ok_or("Unexpected state reply")?;
        samples += 1;
        lo = lo.min(s.position);
        hi = hi.max(s.position);
        let key = (s.view, s.position, s.faults, s.presses);
        if last != Some(key) {
            println!(
                "{:6.2}s view={} position={} fraction={:+.2} voltage={:+.2} faults={} presses={}",
                start.elapsed().as_secs_f64(),
                s.view,
                s.position,
                s.fraction,
                s.voltage,
                s.faults,
                s.presses
            );
            last = Some(key);
        }
        state = Some(s);
        std::thread::sleep(Duration::from_millis(50));
    }
    if let Some(s) = state {
        println!("{samples} samples, position range {lo}..{hi}; final: {s}");
    }
    Ok(())
}
fn project(path: &PathBuf) -> Result<Project, String> {
    let p: Project = serde_json::from_slice(&fs::read(path).map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())?;
    p.validate()?;
    Ok(p)
}
fn run() -> Result<(), String> {
    match Args::parse().command {
        Command::Demo { output } => fs::write(
            output,
            serde_json::to_string_pretty(&Project::default()).unwrap(),
        )
        .map_err(|e| e.to_string())?,
        Command::Validate { project: path } => {
            let p = project(&path)?;
            println!(
                "Valid: {} views, {} bundle bytes",
                p.views.len(),
                p.bundle()?.len()
            );
        }
        Command::Compile {
            project: path,
            output,
        } => {
            let b = project(&path)?.bundle()?;
            fs::write(output, &b).map_err(|e| e.to_string())?;
            println!("Wrote {} bytes", b.len());
        }
        Command::Upload { bundle, port, save } => {
            let b = fs::read(bundle).map_err(|e| e.to_string())?;
            let mut d = Device::open(&port)?;
            let frames = protocol::upload_frames(&b, d.seq)?;
            for f in frames {
                d.seq = f.seq.wrapping_add(1);
                d.exchange(f)?;
            }
            if save {
                d.call(protocol::SAVE, vec![])?;
            }
            println!(
                "Views uploaded{}",
                if save {
                    " and saved"
                } else {
                    " for live preview"
                }
            );
        }
        Command::Diag { port } => {
            let text = Device::open(&port)?
                .call(protocol::DIAGNOSTICS, vec![])
                .map_err(|e| format!("{e} (firmware may predate diagnostics)"))?;
            println!("{}", String::from_utf8_lossy(&text));
        }
        Command::Watch { port, seconds } => watch(&port, seconds)?,
        Command::Ports => {
            for p in serialport::available_ports().map_err(|e| e.to_string())? {
                println!("{} {:?}", p.port_name, p.port_type);
            }
        }
        cmd => {
            let (port, op, payload) = match cmd {
                Command::State { port } => (port, protocol::STATE, vec![]),
                Command::Select { id, port } => (port, protocol::SELECT, id.to_le_bytes().to_vec()),
                Command::Calibrate { port } => (port, protocol::CALIBRATE, vec![]),
                Command::Tare { port } => (port, protocol::TARE, vec![]),
                Command::Stop { port } => (port, protocol::STOP, vec![]),
                Command::Enable { port } => (port, protocol::ENABLE, vec![]),
                Command::Save { port } => (port, protocol::SAVE, vec![]),
                _ => unreachable!(),
            };
            let response = Device::open(&port)?.call(op, payload)?;
            if let Some(s) = State::parse(&response).filter(|_| op == protocol::STATE) {
                println!("{s}");
            } else {
                println!("Command accepted");
            }
        }
    }
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("{e}");
        std::process::exit(1);
    }
}
