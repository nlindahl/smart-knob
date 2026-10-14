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
            if op == protocol::STATE && response.len() == 28 {
                let u16_at = |i| u16::from_le_bytes(response[i..i + 2].try_into().unwrap());
                let float_at = |i| f32::from_le_bytes(response[i..i + 4].try_into().unwrap());
                println!(
                    "view={} position={} fraction={:.3} faults={} calibration={} loops={} voltage={:.3} max_loop_us={:.0} presses={}",
                    u16_at(0),
                    i32::from_le_bytes(response[2..6].try_into().unwrap()),
                    float_at(6),
                    response[10],
                    response[11],
                    u32::from_le_bytes(response[12..16].try_into().unwrap()),
                    float_at(16),
                    float_at(20),
                    u16_at(24)
                );
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
